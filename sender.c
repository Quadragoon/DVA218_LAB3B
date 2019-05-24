/* File: sender.c
 *
 * Author: Joel Risberg,Jan-Ove Leksell
 * 2019-may
 * 
 * Instructions: 
 * Run through a linux terminal from the folder containing the compiled file. 
 * (while the receiver app is already running), using the command './sender X' where 'X' is the debug level 
 * Debug Level:( from '0' -[Just about nothing] to '5' -[more] 
 * Specific debug modes are: 
 * Checksum calculation:------------------------------------- 15   
 * Roundtime, average time for sending / ACKing packets:----- 20    
 * Error generator:------------------------------------------ 25   
 * Reading packets from the receiver:------------------------ 30
 * 
 * Description: 
 * Request connection to the receiver, sends everything within the text file "message.txt" to the receiver through a TCP like implementation
 * using different helper threads to listen for messages from the receiver, managing a dynamic round time calculator and checking timeouts
 */

#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <zconf.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <math.h>
#include <semaphore.h>
#include <sys/stat.h>

#include "common.h"

void* ThreadedACKTimeout(timeoutHandlerData* timeoutData);

void* ThreadedSYNTimeout(timeoutHandlerData* timeoutData);

int socket_fd;
int connectionStatus = -1;
int KillThreads = 0;
byte windowSize = 20;
unsigned short frameSize = 100;

struct sockaddr_in receiverAddress;
struct sockaddr_in senderAddress;

sem_t ackSemaphore;

sem_t windowSemaphore;
int lowestSequenceAwaited = -1;

char* addressString = "127.0.0.1";

sem_t roundTimeSemaphore;
float roundTime = 0;
float averageRoundTime = 10000;
struct roundTimeHandler timeStamper[50];

byte desiredWindowSize;
unsigned short desiredFrameSize;
byte suggestedWindowSize;
unsigned short suggestedFrameSize;

#define MIN_ACCEPTED_WINDOW_SIZE 1
#define MAX_ACCEPTED_WINDOW_SIZE 16
#define MIN_ACCEPTED_FRAME_SIZE 1
#define MAX_ACCEPTED_FRAME_SIZE 65535

//Change MAX_MESSAGE_LENGTH in order to accommodate larger text files
#define MAX_MESSAGE_LENGTH 20000

//Change MAX_TIMEOUT_RETRIES in order to either allow less or more retries before a packet stops running timeouts and resends
#define MAX_TIMEOUT_RETRIES 99999

// Change BASE_AVERAGE in order to lower or raise the number of samples that the average runtime manager bases its results on
#define BASE_AVERAGE 5

//
#define TIMEOUT_USLEEP_TIME (averageRoundTime * 2)

//---------------------------------------------------------------------------------------------------------------

// Function that negotiates the three way handshake between the sender and receiver, negotiation window & frame size etc.
int NegotiateConnection(byte windowSizeToRequest, unsigned short frameSizeToRequest, ACKmngr* ACKsPointer)
{
    desiredWindowSize = windowSizeToRequest;
    desiredFrameSize = frameSizeToRequest;
    DEBUGMESSAGE(3, "Negotiating connection");

    // memset used to make sure that the address fields are filled with '0's
    memset(&receiverAddress, 0, sizeof(struct sockaddr_in));
    memset(&senderAddress, 0, sizeof(struct sockaddr_in));

    // Socket address format set to AF_INET for Internet use, 
    receiverAddress.sin_family = AF_INET;
    // Set port number to LISTENING_PORT (defined in common.c). The function htons converts from host byte order to network byte order.
    receiverAddress.sin_port = htons(LISTENING_PORT);

    // Get retval(return value) from inet_pton - (convert IPv4 and IPv6 addresses from text to binary form).  
    int retval = inet_pton(receiverAddress.sin_family, addressString, &(receiverAddress.sin_addr));

    if (retval == -1)
    {
        CRASHWITHERROR("NegotiateConnection() failed to parse IP address");
    }
    else if (retval == 0)
    {
        DEBUGMESSAGE(0, YELTEXT("Invalid IP address entered"));
    }

    unsigned int receiverAddressLength = sizeof(receiverAddress);

    // 'packet' struct defined in common.h
    packet packetToSend;

    byte packetData[3];
    packetData[0] = desiredWindowSize;
    byte* desiredFrameSizeBytes = (byte*) (&desiredFrameSize);
    packetData[1] = desiredFrameSizeBytes[0];
    packetData[2] = desiredFrameSizeBytes[1];
    WritePacket(&packetToSend, PACKETFLAG_SYN, (void*) packetData, 3, 0);

    timeoutHandlerData* timeoutData;
    if ((timeoutData = malloc(sizeof(timeoutHandlerData))) == NULL)
    {
        CRASHWITHERROR("malloc for timeoutHandlerData in NegotiateConnection() failed");
    }
    ACKsPointer->Table[0] = 0;
    timeoutData->sequenceNumber = 0;
    timeoutData->flags = PACKETFLAG_SYN;
    timeoutData->ACKsPointer = ACKsPointer;

    pthread_t timeoutThread;
    pthread_create(&timeoutThread, NULL, (void*) ThreadedSYNTimeout, timeoutData);
    SendPacket(socket_fd, &packetToSend, &receiverAddress, receiverAddressLength);
    return 0;
}

//---------------------------------------------------------------------------------------------------------------
// The function that continously updates the roundTime average, this value is then used as a base for the timeouts

float roundTimeManager()
{
    memset(timeStamper, 0, 50 * 8);
    float roundTimeTable[BASE_AVERAGE];
    memset(roundTimeTable, 0, sizeof(float) * BASE_AVERAGE);
    float lastReportedRoundTime = 0;
    int i = 0;
    int divider = 0; // Used when dividing the sum of all samples, necessary to avoid dividing by '0' and to alow averages based on less then the full sample group
    int roundTimeSemCounter = 0;
    DEBUGMESSAGE_EXACT(DEBUGLEVEL_ROUNDTIME, MAG
            "roundTimeManager thread up and running, waiting for roundTimeSemaphore\n"
            RESET);

    while (KillThreads != 1)
    {
        sem_wait(&roundTimeSemaphore);
        if (KillThreads == 1)
        {
            printf(RED"------------roundTimeManager KillThreads: ["RESET" %d "RED"]------------\n"RESET, KillThreads);
            printf(RED"------------roundTimeManager thread shutting down------------\n"RESET);
            usleep(1000);
            pthread_exit(NULL);
        }
        sem_getvalue(&roundTimeSemaphore, &roundTimeSemCounter);
        for (int a = 0; a < windowSize; a++)
        {
            if (roundTime != lastReportedRoundTime)
            {
                roundTimeTable[i] = roundTime;
                i++;
                if (i == BASE_AVERAGE)
                {
                    i = 0;
                }
                for (int u = 0; u < BASE_AVERAGE; u++)
                {
                    if (roundTimeTable[u] != 0)
                    {
                        divider++;
                        averageRoundTime += roundTimeTable[u];
                    }
                }
                if (divider > 0)
                {
                    averageRoundTime = (averageRoundTime / divider);
                    if (averageRoundTime < 300)
                    {
                        averageRoundTime = 300;
                    }
                    else if (averageRoundTime > 3000)
                    {
                        averageRoundTime = 300;
                    }
                    DEBUGMESSAGE_EXACT(DEBUGLEVEL_ROUNDTIME, CYN
                            "averageRoundTime set to: ["
                            RESET
                            " %.0f "
                            CYN
                            "]     using: ["
                            RESET
                            " %d "
                            CYN
                            "] samples.  SemCounter: ["
                            RESET
                            " %d "
                            CYN
                            "]\n"
                            RESET, averageRoundTime, divider, roundTimeSemCounter);
                    divider = 0;


                }
                lastReportedRoundTime = roundTime;
            }
            else
            {
                DEBUGMESSAGE_EXACT(DEBUGLEVEL_ROUNDTIME, CYN
                        "."
                        RESET);
            }
        }
        DEBUGMESSAGE_EXACT(DEBUGLEVEL_ROUNDTIME, "\n");
    }


    printf(RED"------------roundTimeManager thread shutting down------------\n"RESET);
    usleep(1000);
    pthread_exit(NULL);

}

//---------------------------------------------------------------------------------------------------------------
// The function that reads packets from the receiver, is run by a separate thread

void* ReadPackets(ACKmngr* ACKsPointer)
{
    DEBUGMESSAGE(3, "ReadPackets thread running\n");

    packet packetBuffer;
    unsigned int senderAddressLength = sizeof(senderAddress);

    while (KillThreads != 1)
    {
        ReceivePacket(socket_fd, &packetBuffer, &senderAddress, &senderAddressLength); // Thread gets stuck here on shutdown?
        sem_post(&roundTimeSemaphore); // Add 1 to roundTimeSemaphore
        if (KillThreads == 1)
        {
            printf(RED"------------ReadPackets KillThreads: ["RESET" %d "RED"]------------\n"RESET, KillThreads);
            printf(RED"------------ReadPackets thread shutting down------------\n"RESET);
            usleep(1000);
            pthread_exit(NULL);
        }
        if (packetBuffer.flags == PACKETFLAG_ACK)
        {
            unsigned short packetSequenceNumber = packetBuffer.sequenceNumber;
            sem_wait(&ackSemaphore);
            if (ACKsPointer->Table[packetSequenceNumber] == 0)
            {
                ACKsPointer->Table[packetSequenceNumber] = 1;
                ACKsPointer->Missing--;
                DEBUGMESSAGE(0, "ACK received for sequence %d. Missing ACKs: %d", packetSequenceNumber, ACKsPointer->Missing);

                DEBUGMESSAGE_EXACT(DEBUGLEVEL_READPACKETS, CYN
                        "ACK: ["
                        RESET
                        " %d "
                        CYN
                        "] Received     ACKs.Missing:["
                        RESET
                        " %d "
                        CYN
                        "]\n"
                        RESET, packetSequenceNumber, ACKsPointer->Missing);
                //-------------------------------------------- Updating the roundTime
                for (int i = 0; i < 50; i++)
                {
                    if (timeStamper[i].sequence == packetSequenceNumber)
                    {
                        gettimeofday(&(timeStamper[i].timeStampEnd), NULL); //--------------------------------------TIMESTAMPEND
                        roundTime = (timeStamper[i].timeStampEnd.tv_usec - timeStamper[i].timeStampStart.tv_usec);
                        break;
                    }
                }

                //--------------------------------------------

                while (ACKsPointer->Table[lowestSequenceAwaited] == 1)
                {
                    sem_post(&windowSemaphore);
                    ACKsPointer->Table[lowestSequenceAwaited] = -1; // No longer waiting for ACK on this sequenceNumber
                    lowestSequenceAwaited++;
                    DEBUGMESSAGE(0, GRNTEXT("Sliding window moving up. Lowest awaited is now: ")
                            "%d", lowestSequenceAwaited);
                }
            }
            else
            {
                DEBUGMESSAGE(3, YELTEXT("WARNING: ")
                        "Received ACK packet for sequenceNumber not waiting for ACK");
                DEBUGMESSAGE(3, "  Got sequenceNumber %d (which is on status %d)", packetSequenceNumber, ACKsPointer->Table[packetSequenceNumber]);
            }
            sem_post(&ackSemaphore);
        }
        else if (packetBuffer.flags & PACKETFLAG_SYN && packetBuffer.flags & PACKETFLAG_ACK)
        {
            suggestedWindowSize = packetBuffer.data[0];
            suggestedFrameSize = ntohs((packetBuffer.data[1] * 256) + packetBuffer.data[2]);
            ACKsPointer->Table[0] = -1;
            DEBUGMESSAGE(3, "SYN+ACK: Flags "
                    GRNTEXT("OK"));
            if (suggestedWindowSize == desiredWindowSize && suggestedFrameSize == desiredFrameSize)
            {
                DEBUGMESSAGE(2, "SYN+ACK: Data "
                        GRNTEXT("OK"));
                windowSize = suggestedWindowSize;
                frameSize = suggestedFrameSize;
                connectionStatus = 1; // connectionStatus set to "connected"
                if (sem_init(&windowSemaphore, 0, windowSize) == -1)
                {
                    CRASHWITHERROR("Semaphore initialization failed");
                }
                printf(GRN"Connection to Receiver Established!\n"RESET);

                usleep(5000);
            }
            else
            {
                ACKsPointer->Table[0] = -1;
                connectionStatus = -1; // connectionStatus set to "not connected"
                DEBUGMESSAGE(2, "SYN+ACK: Data "
                        REDTEXT("NOT OK."));
                DEBUGMESSAGE(0, "SYN+ACK: Suggested parameters don't match desired ones. Data corrupted?");
            }

        }
        else if (packetBuffer.flags & PACKETFLAG_SYN && packetBuffer.flags & PACKETFLAG_NAK)
        {
            suggestedWindowSize = packetBuffer.data[0];
            suggestedFrameSize = ntohs((packetBuffer.data[1] * 256) + packetBuffer.data[2]);
            DEBUGMESSAGE(3, "SYN+NAK: Flags "
                    GRNTEXT("OK"));
            if (suggestedWindowSize == desiredWindowSize && suggestedFrameSize == desiredFrameSize)
            {
                DEBUGMESSAGE(0, "SYN+NAK: Data "
                        REDTEXT("VERY NOT OK."));
                DEBUGMESSAGE(0, "SYN+NAK: Received NAK suggestion for desired parameters.\n"
                        YELTEXT("Something is wrong."));
            }
            else if (suggestedWindowSize >= MIN_ACCEPTED_WINDOW_SIZE &&
                     suggestedWindowSize <= MAX_ACCEPTED_WINDOW_SIZE &&
                     suggestedFrameSize >= MIN_ACCEPTED_FRAME_SIZE &&
                     suggestedFrameSize <= MAX_ACCEPTED_FRAME_SIZE)
            {
                DEBUGMESSAGE(0, "SYN+NAK: Renegotiating connection...");
                DEBUGMESSAGE(0, "SYN+NAK: Trying again with parameters window:%d and frame:%d",
                             suggestedWindowSize, suggestedFrameSize);

                ACKsPointer->Table[0] = -1;
                sleep(1);
                NegotiateConnection(suggestedWindowSize, suggestedFrameSize, ACKsPointer);
            }
            else
            {
                DEBUGMESSAGE(1, "SYN+ACK: Data "
                        REDTEXT("NOT OK."));
                DEBUGMESSAGE(0, "SYN+ACK: Suggested parameters out of bounds. Connection impossible.");
                ACKsPointer->Table[0] = -1;
            }
        }
        else if (packetBuffer.flags == PACKETFLAG_FIN)
        {
            // -------------------------------------------------------------TODO: Send FIN ACK HERE
            // -------------------------------------------------------------TODO: Send FIN ACK HERE
            // -------------------------------------------------------------TODO: Send FIN ACK HERE
            KillThreads = 1;
            printf(RED"------------ReadPackets KillThreads: ["RESET" %d "RED"]------------\n"RESET, KillThreads);
            printf(RED"------------ReadPackets thread shutting down------------\n"RESET);
            usleep(1000);
            pthread_exit(NULL);
        }

    }


    printf(RED"------------ReadPackets thread shutting down------------\n"RESET);
    usleep(1000);
    pthread_exit(NULL);
}
//---------------------------------------------------------------------------------------------------------------

void PrintMenu()
{
    int roundTimeSemCounter = 0;
    sem_getvalue(&roundTimeSemaphore, &roundTimeSemCounter);

    printf(YEL"--------------------------\n"RESET);
    printf(YEL"Welcome!  "RESET YEL"\n[%.1f] Roundtime Average"RESET" %.0d"YEL"us\n"RESET, averageRoundTime, roundTimeSemCounter);
    printf(YEL"Packet-   Loss:["RESET" %d "YEL"]    Corrupt:["RESET" %d "YEL"]\n", loss, corrupt);
    printf(YEL"--------------------------\n"RESET);
    printf(GRN"[ "RESET"1"GRN" ]: Connect to Receiver\n"RESET);
    printf(CYN"[ "RESET"2"CYN" ]: Send Message\n"RESET);
    printf(MAG"[ "RESET"3"MAG" ]: Preview Message\n"RESET);
    printf(RED"--------------------------\n"RESET);
    printf(RED"[ "RESET"4"RED" ]: Update Packet Loss chance\n"RESET);
    printf(RED"[ "RESET"5"RED" ]: Update Packet Corruption chance\n"RESET);
    printf(RED"[ "RESET"2049"RED" ]: End program\n"RESET);
    printf(YEL"--------------------------\n"RESET);
}
//---------------------------------------------------------------------------------------------------------------

void LoadMessageFromFile(char readstring[MAX_MESSAGE_LENGTH])
{
    system("clear"); // Clean up the console
    printf(YEL"---[ Message Preview ]--- \n"RESET);

    //---------------------------

    FILE* fp;
    int symbol;
    int tracker = 0;

    fp = fopen("message", "rb");
    if (fp != NULL)
    {
        while (tracker < MAX_MESSAGE_LENGTH)
        {
            symbol = fgetc(fp);
            if (feof(fp))
            { // Check for the end of the file
                break;
            }
            readstring[tracker] = (char) symbol;
            tracker++;
        }
        fclose(fp);
    }
    else
    {
        DEBUGMESSAGE(0, YELTEXT("\n -Couldn't open file 'message'- "));
    }
}

//---------------------------------------------------------------------------------------------------------------

void* ThreadedACKTimeout(timeoutHandlerData* timeoutData)
{
    // Transfer values into statically allocated memory so we can free the dynamic memory
    ACKmngr* ACKsPointer = timeoutData->ACKsPointer;
    int sequenceNumber = timeoutData->sequenceNumber;
    packet* dataBufferArray = timeoutData->dataBufferArray;
    int bufferSlot = timeoutData->bufferSlot;
    byte flags = timeoutData->flags;
    free(timeoutData);

    int numPreviousTimeouts = 0;

    DEBUGMESSAGE(3, "ThreadedACKTimeout for seq %d started", sequenceNumber);

    packet* packetToSend;
    if ((packetToSend = malloc(sizeof(packet))) == NULL)
    {
        printf("Sequencenumber for malloc fail: %d\n", sequenceNumber);
        CRASHWITHERROR("malloc() for packetToSend in ThreadedACKTimeout() failed");
    }

    usleep(TIMEOUT_USLEEP_TIME);
    while (ACKsPointer->Table[sequenceNumber] == 0 && numPreviousTimeouts <= MAX_TIMEOUT_RETRIES && KillThreads != 1)
    {
        numPreviousTimeouts++;
        WritePacket(packetToSend, flags, dataBufferArray[bufferSlot].data, frameSize, sequenceNumber);

        //---------------------------------------------------------------------------------------------------------------
        for (int i = 0; i < 50; i++)
        {
            if (timeStamper[i].sequence = sequenceNumber)
            {
                gettimeofday(&(timeStamper[i].timeStampStart), NULL); //--------------------------------------UPDATE TIMESTAMPSTART
                break;
            }
        }

        //---------------------------------------------------------------------------------------------------------------

        DEBUGMESSAGE(1, REDTEXT("TIMEOUT")" for packet #%d. Resending...", sequenceNumber);
        SendPacket(socket_fd, packetToSend, &receiverAddress, sizeof(receiverAddress));
        usleep(TIMEOUT_USLEEP_TIME);
    }
    if (numPreviousTimeouts >= MAX_TIMEOUT_RETRIES)
    {
        for (int i = 0; i < 5; i++)
        {
            printf(RED"MAX TIMEOUT RETRIES REACHED!\n"RESET);
        }
    }

    free(packetToSend);
    DEBUGMESSAGE_NONEWLINE(3, MAG
            "-Timeout thread ["
            RESET
            " %d "
            MAG
            "] Exit-"
            RESET
            "\n", sequenceNumber);
    pthread_exit(NULL);
}

//---------------------------------------------------------------------------------------------------------------

void* ThreadedSYNTimeout(timeoutHandlerData* timeoutData)
{
    // Transfer values into statically allocated memory so we can free the dynamic memory
    ACKmngr* ACKsPointer = timeoutData->ACKsPointer;
    int sequenceNumber = timeoutData->sequenceNumber;
    free(timeoutData);

    byte packetData[3];
    packetData[0] = desiredWindowSize;
    byte* desiredFrameSizeBytes = (byte*) (&desiredFrameSize);
    packetData[1] = desiredFrameSizeBytes[0];
    packetData[2] = desiredFrameSizeBytes[1];

    int numPreviousTimeouts = 0;

    DEBUGMESSAGE(3, "ThreadedSYNTimeout for seq %d started", sequenceNumber);

    packet* packetToSend;
    if ((packetToSend = malloc(sizeof(packet))) == NULL)
    {
        printf("Sequencenumber for malloc fail: %d\n", sequenceNumber);
        CRASHWITHERROR("malloc() for packetToSend in ThreadedSYNTimeout() failed");
    }

    usleep(TIMEOUT_USLEEP_TIME);
    while (ACKsPointer->Table[sequenceNumber] == 0 && numPreviousTimeouts <= MAX_TIMEOUT_RETRIES && KillThreads != 1)
    {
        numPreviousTimeouts++;
        WritePacket(packetToSend, PACKETFLAG_SYN, packetData, 3, sequenceNumber);

        //---------------------------------------------------------------------------------------------------------------
        for (int i = 0; i < 50; i++)
        {
            if (timeStamper[i].sequence = sequenceNumber)
            {
                gettimeofday(&(timeStamper[i].timeStampStart), NULL); //--------------------------------------UPDATE TIMESTAMPSTART
                break;
            }
        }

        //---------------------------------------------------------------------------------------------------------------

        SendPacket(socket_fd, packetToSend, &receiverAddress, sizeof(receiverAddress));
        usleep(TIMEOUT_USLEEP_TIME);
    }
    if (numPreviousTimeouts >= MAX_TIMEOUT_RETRIES)
    {
        for (int i = 0; i < 5; i++)
        {
            printf(RED"MAX TIMEOUT RETRIES REACHED!\n"RESET);
        }
    }

    free(packetToSend);
    DEBUGMESSAGE_NONEWLINE(3, MAG
            "-Timeout thread ["
            RESET
            " %d "
            MAG
            "] Exit-"
            RESET
            "\n", sequenceNumber);
    pthread_exit(NULL);
}

void SlidingWindow(char* readstring, ACKmngr* ACKsPointer)
{
    system("clear"); // Clean up the console
    DEBUGMESSAGE(2, YELTEXT("---[ Sending Message ]--- "));
    float messageDivided = 0;
    int packets = 0;
    int seq = 0; // Keeps track of what frame the sliding window is currently managing
    int stampID = 0;

    int messageTracker = 0; // Tracks where we are located in the message that is currently being chopped up.

    // Figure out how many packets we need to send
    int messageLength = strlen(readstring);
    messageDivided = (float) messageLength / (float) frameSize;
    packets = ceil((double) messageDivided);
    DEBUGMESSAGE(0, GRNTEXT("Message is [")
            " %d "
            GRNTEXT("] symbols long"), messageLength);
    DEBUGMESSAGE(0, GRNTEXT("Will split over [%.2f] rounded to [")
            " %d "
            GRNTEXT("] packets"), messageDivided, packets);
    DEBUGMESSAGE(0, YELTEXT("WindowSize is [")
            " %d "
            YELTEXT("] frames"), windowSize);

    unsigned int receiverAddressLength = sizeof(receiverAddress);

    packet* dataBufferArray;
    dataBufferArray = malloc(sizeof(packet) * (windowSize));
    if (dataBufferArray == NULL)
    {
        CRASHWITHERROR("malloc() for dataBufferArray in SlidingWindow() failed");
    }

    packet* packetToSend;
    if ((packetToSend = malloc(sizeof(packet))) == NULL)
    {
        CRASHWITHERROR("malloc() for packetToSend in SlidingWindow() failed");
    }

    int bufferSlot = 0;

    if (lowestSequenceAwaited == -1)
        lowestSequenceAwaited = seq;
    else
        seq = lowestSequenceAwaited;

    for (int i = 0; i < packets; i++)
    {
        sem_wait(&windowSemaphore);

        if (bufferSlot == windowSize)
            bufferSlot = 0; // if the condition is met, we would try to write outside our buffer. No good! Loop around!

        for (int j = messageTracker; j < (frameSize + messageTracker); j++)
        { // Fill up the outgoing packet with data
            dataBufferArray[bufferSlot].data[j - messageTracker] = readstring[j];
        }

        WritePacket(packetToSend, 0, (void*) (dataBufferArray[bufferSlot].data), frameSize, seq);

        DEBUGMESSAGE(3, BLUTEXT("----------------------Sending Packet:[")
                " %d "
                BLUTEXT("]   seq:[")
                " %d "
                BLUTEXT("]   messageTracker:[")
                " %d "
                BLUTEXT("]"),
                     packetToSend->sequenceNumber, seq, messageTracker);

        // Managing the semaphore responsible for letting lose the roundTimeManager--------------------
        sem_post(&roundTimeSemaphore); // Add 1 to roundTimeSemaphore
        //-------------------------------------------------------------

        //Providing timestamps for the roundTimeManager to use
        timeStamper[stampID].sequence = packetToSend->sequenceNumber;
        gettimeofday(&(timeStamper[stampID].timeStampStart), NULL); //--------------------------------------TIMESTAMPSTART
        //--------------------------------------------------------------

        stampID++;
        if (stampID == 50)
        {
            stampID = 0;
        }
        sem_wait(&ackSemaphore);

        timeoutHandlerData* timeoutHandler;
        if ((timeoutHandler = malloc(sizeof(timeoutHandlerData))) == NULL)
        {
            CRASHWITHERROR("malloc for timeoutHandler in SlidingWindow() failed");
        }
        timeoutHandler->bufferSlot = bufferSlot;
        timeoutHandler->sequenceNumber = seq;
        timeoutHandler->ACKsPointer = ACKsPointer;
        timeoutHandler->dataBufferArray = dataBufferArray;
        timeoutHandler->flags = packetToSend->flags;

        pthread_t timeoutThread;
        pthread_create(&timeoutThread, NULL, (void*) ThreadedACKTimeout, timeoutHandler);

        DEBUGMESSAGE(0, YELTEXT("Message: [")
                " %d "
                YELTEXT("] Sent     ")
                MAGTEXT("ACKs.Missing:[")
                " %d "
                MAGTEXT("]"),
                     packetToSend->sequenceNumber, ACKsPointer->Missing);

        SendPacket(socket_fd, packetToSend, &receiverAddress, receiverAddressLength);

        ACKsPointer->Table[seq] = 0;
        (ACKsPointer->Missing)++;

        sem_post(&ackSemaphore);

        seq++;
        bufferSlot++;
        messageTracker += frameSize;
    }
    DEBUGMESSAGE(0, CYNTEXT("+------------------------------------+"));
    DEBUGMESSAGE(0, CYNTEXT("| All packets sent! Awaiting ACKs... |"));
    DEBUGMESSAGE(0, CYNTEXT("+------------------------------------+"));
    do
    { usleep(100000); }
    while (ACKsPointer->Missing > 0);
}
//---------------------------------------------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    system("clear");
    srandom(time(NULL));
    if (argc == 2)
    {
        debugLevel = strtol(argv[1], NULL, 10);
    }
    int command = 0;
    char c;
    char readstring[MAX_MESSAGE_LENGTH] = "\0";

    // Setup the ACK struct used for tracking ACKS----
    ACKmngr ACKs;
    for (int y = 0; y < ACK_TABLE_SIZE; y++)
    {
        ACKs.Table[y] = -1;
    }
    ACKs.Missing = 0;
    //---------------------------------------------

    DEBUGMESSAGE(3, "Intializing socket...");
    socket_fd = InitializeSocket();
    DEBUGMESSAGE(1, "Socket setup successfully.");


    while (KillThreads == 0)
    {
        usleep(1000);
        int update = 0;
        //system("clear"); // Clean up the console
        //printf("%s\n", readstring);
        PrintMenu();

        char* commandBuffer;
        if ((commandBuffer = malloc(128)) == NULL)
        {
            CRASHWITHERROR("commandBuffer malloc failed");
        }

        if (sem_init(&roundTimeSemaphore, 0, windowSize) == -1)
        {
            CRASHWITHERROR("Semaphore roundTimeSemaphore initialization failed in main()");
        }
        if (sem_init(&ackSemaphore, 0, 1) == -1)
        {
            CRASHWITHERROR("Semaphore ackSemaphore initialization failed in main()");
        }

        scanf("%s", commandBuffer);
        command = strtol(commandBuffer, NULL, 10); // Get a command from the user
        while ((c = getchar()) != '\n' && c != EOF); // Cleaning out the readbuffer

        switch (command)
        {
            case 1:
                system("clear"); // Clean up the console
                if (connectionStatus == -1)
                {
                    // Create the thread checking for messages from the receiver------
                    DEBUGMESSAGE(0, YELTEXT("Setting up ReadPackets thread..."));
                    pthread_t thread; //Thread ID
                    if (pthread_create(&thread, NULL, (void*) ReadPackets, &ACKs) != 0)
                    {
                        CRASHWITHERROR("pthread_create(ReadPackets) failed in main()");
                    }
                    usleep(5000);
                    printf(GRN"Done!\n"RESET);
                    //----------------------------------------------------------------
                    // Create the thread managing the average roundtime calculation------
                    DEBUGMESSAGE(0, YELTEXT("Setting up roundTimeManager thread..."));
                    if (pthread_create(&thread, NULL, (void*) roundTimeManager, NULL) != 0)
                    {
                        CRASHWITHERROR("pthread_create(ReadPackets) failed in main()");
                    }
                    usleep(5000);
                    printf(GRN"Done!\n"RESET);
                    connectionStatus = 0; // connectionStatus set to "pending"
                    NegotiateConnection(windowSize, frameSize, &ACKs);
                    while (connectionStatus == 0)
                    {
                        usleep(100000);
                    }
                }
                else if (connectionStatus == 1)
                {
                    DEBUGMESSAGE(0, "Error: already connected!");
                }
                else if (connectionStatus == 0)
                {
                    DEBUGMESSAGE(0, "Error: connection status is 'pending'. Something wrong?");
                }
                else
                {
                    CRASHWITHMESSAGE("Connection status undefined when calling NegotiateConnection()! Weird stuff!");
                }
                break;
            case 2:
                if (connectionStatus == 1)
                {
                    //----------------------------------------------------------------
                    usleep(5000);
                    printf(YEL"Reading message from file..."RESET);
                    LoadMessageFromFile(readstring);
                    usleep(5000);
                    printf(GRN"Done!\n"RESET);
                    printf(YEL"Sending message!..."RESET);
                    SlidingWindow(readstring, &ACKs); // Send the Message
                    usleep(1000);
                }
                else
                {
                    DEBUGMESSAGE(0, "Error: not connected to receiver!");
                }
                break;
            case 3:
                LoadMessageFromFile(readstring);
                printf("%s\n", readstring);
                // Just sending the user back to the start of the while loop
                break;
            case 4:
                printf(RED"Input new value (in percent 1-100): ");
                scanf("%s", commandBuffer);
                update = strtol(commandBuffer, NULL, 10); // Get a command from the user
                while ((c = getchar()) != '\n' && c != EOF); // Cleaning out the readbuffer
                loss = update;
                break;
            case 5:
                printf(RED"Input new value (in percent 1-100): ");
                scanf("%s", commandBuffer);
                update = strtol(commandBuffer, NULL, 10); // Get a command from the user
                while ((c = getchar()) != '\n' && c != EOF); // Cleaning out the readbuffer
                corrupt = update;
                break;
            case 2049:
                1;
                system("clear");
                unsigned int receiverAddressLength = sizeof(receiverAddress);
                packet* endGame;
                endGame = malloc(sizeof(packet));
                if (endGame == NULL)
                {
                    CRASHWITHERROR("malloc() for dataBufferArray in SlidingWindow() failed");
                }
                WritePacket(endGame, PACKETFLAG_FIN, "byebye", frameSize, 1);
                SendPacket(socket_fd, endGame, &receiverAddress, receiverAddressLength);
                KillThreads = 1; // Make sure that we let the other threads know that we are closing down the client
                break;
            default:
                printf("Wrong input\n");
        }
    }

    //
    printf(YEL"SHUTTING DOWN....\n"RESET);
    sem_post(&roundTimeSemaphore); // Make sure that the rounTimeManager isn't stuck.
    usleep(10000);
    close(socket_fd);
    for (int p = 0; p < 10; p++)
    {
        usleep(100000);
    }
    printf("Thank you come again :D\n");
    sleep(1);
    system("clear"); // Clean up the console
    exit(EXIT_SUCCESS);
}

