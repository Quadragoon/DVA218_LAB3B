/* File: sender.c
 *
 * Author: Joel Risberg,Jan-Ove Leksell
 * 2019-may
 * 
 * Instructions: 
 * Run through a linux terminal from the folder containing the compiled file. 
 * (while the receiver app is already running), using the command './sender X' where 'X' is the debug level 
 * Debug Level:( from '0' -[Just about nothing] to '4' -[I heard that you want to print every last byte!])
 * 
 * Description: 
 * Request connection to the receiver, send messages entered by the user while simultaneously
 * using a second thread to listen for messages from the receiver and printing them in the console 
 * depending on debug level.
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

#include "common.h"

int socket_fd;
unsigned short sequence = 0;
int KillThreads = 0;
byte windowSize = 4;
unsigned short frameSize = 1000;
float roundTime = 0;
float averageRoundTime = 0;

struct sockaddr_in receiverAddress;
struct sockaddr_in senderAddress;

sem_t windowSemaphore;
int lowestSequenceAwaited = -1;

#define MIN_ACCEPTED_WINDOW_SIZE 1
#define MAX_ACCEPTED_WINDOW_SIZE 16
#define MIN_ACCEPTED_FRAME_SIZE 1
#define MAX_ACCEPTED_FRAME_SIZE 65535

#define MAX_MESSAGE_LENGTH 20000

#define TIMEOUT_DELAY 2
#define MAX_TIMEOUT_RETRIES 999
#define BASE_AVERAGE 5

//---------------------------------------------------------------------------------------------------------------

int NegotiateConnection(const char* receiverIP, byte desiredWindowSize, unsigned short desiredFrameSize)
{
    DEBUGMESSAGE(5, "Negotiating connection");
    struct timeval timeStamp1, timeStamp2;

    // memset used to make sure that the address fields are filled with '0's
    memset(&receiverAddress, 0, sizeof(struct sockaddr_in));
    memset(&senderAddress, 0, sizeof(struct sockaddr_in));

    // Socket address format set to AF_INET for Internet use, 
    receiverAddress.sin_family = AF_INET;
    // Set port number to LISTENING_PORT (defined in common.c). The function htons converts from host byte order to network byte order.
    receiverAddress.sin_port = htons(LISTENING_PORT);

    // Get retval(return value) from inet_pton - (convert IPv4 and IPv6 addresses from text to binary form).  
    int retval = inet_pton(receiverAddress.sin_family, receiverIP, &(receiverAddress.sin_addr));

    if (retval == -1)
    {
        CRASHWITHERROR("NegotiateConnection() failed to parse IP address");
    }
    else if (retval == 0)
    {
        DEBUGMESSAGE(0, YELTEXT("Invalid IP address entered"));
    }

    unsigned int receiverAddressLength = sizeof(receiverAddress);
    unsigned int senderAddressLength = sizeof(senderAddress);

    // 'packet' struct defined in common.h
    packet packetToSend, packetBuffer;

    byte packetData[3];
    packetData[0] = desiredWindowSize;
    byte* desiredFrameSizeBytes = (byte*) (&desiredFrameSize);
    packetData[1] = desiredFrameSizeBytes[0];
    packetData[2] = desiredFrameSizeBytes[1];
    WritePacket(&packetToSend, PACKETFLAG_SYN, (void*) packetData, 3, sequence);

    gettimeofday(&timeStamp1, NULL); //--------------------------------------TIMESTAMP1

    SendPacket(socket_fd, &packetToSend, &receiverAddress, receiverAddressLength);
    if (ReceivePacket(socket_fd, &packetBuffer, &senderAddress, &senderAddressLength) != -1)
    {
        byte suggestedWindowSize = packetBuffer.data[0];
        unsigned short suggestedFrameSize = ntohs((packetBuffer.data[1] * 256) + packetBuffer.data[2]);

        if (packetBuffer.flags & PACKETFLAG_SYN && packetBuffer.flags & PACKETFLAG_ACK)
        {
            DEBUGMESSAGE(3, "SYN+ACK: Flags "
                    GRNTEXT("OK"));
            if (packetBuffer.sequenceNumber == (sequence))
            {
                DEBUGMESSAGE(3, "SYN+ACK: Sequence number "
                        GRNTEXT("OK"));
                if (suggestedWindowSize == desiredWindowSize && suggestedFrameSize == desiredFrameSize)
                {
                    DEBUGMESSAGE(2, "SYN+ACK: Data "
                            GRNTEXT("OK"));
                    windowSize = suggestedWindowSize;
                    frameSize = suggestedFrameSize;
                    //------------------------------------------------------TIMESTAMP2
                    gettimeofday(&timeStamp2, NULL);
                    roundTime = (timeStamp2.tv_usec - timeStamp1.tv_usec);
                    //------------------------------------------------------
                    return 1;
                }
                else
                {
                    DEBUGMESSAGE(2, "SYN+ACK: Data "
                            REDTEXT("NOT OK."));
                    DEBUGMESSAGE(1, "SYN+ACK: Suggested parameters don't match desired ones. Data corrupted?");
                    return -1;
                }
            }
            else
            {
                DEBUGMESSAGE(2, "SYN+ACK: Sequence number "
                        REDTEXT("NOT OK."));
                return -1;
            }
        }
        else if (packetBuffer.flags & PACKETFLAG_SYN && packetBuffer.flags & PACKETFLAG_NAK)
        {
            DEBUGMESSAGE(3, "SYN+NAK: Flags "
                    GRNTEXT("OK"));
            if (packetBuffer.sequenceNumber == (sequence))
            {
                DEBUGMESSAGE(3, "SYN+NAK: Sequence number "
                        GRNTEXT("OK"));
                if (suggestedWindowSize == desiredWindowSize && suggestedFrameSize == desiredFrameSize)
                {
                    DEBUGMESSAGE(1, "SYN+NAK: Data "
                            REDTEXT("VERY NOT OK."));
                    DEBUGMESSAGE(1, "SYN+NAK: Received NAK suggestion for desired parameters.\n"
                            YELTEXT("Something is wrong."));
                    return -1;
                }
                else if (suggestedWindowSize >= MIN_ACCEPTED_WINDOW_SIZE &&
                         suggestedWindowSize <= MAX_ACCEPTED_WINDOW_SIZE &&
                         suggestedFrameSize >= MIN_ACCEPTED_FRAME_SIZE &&
                         suggestedFrameSize <= MAX_ACCEPTED_FRAME_SIZE)
                {
                    DEBUGMESSAGE(1, "SYN+NAK: Renegotiating connection...");
                    DEBUGMESSAGE(4, "SYN+NAK: Trying again with parameters window:%d and frame:%d",
                                 suggestedWindowSize, suggestedFrameSize);
                    //------------------------------------------------------TIMESTAMP2
                    gettimeofday(&timeStamp2, NULL);
                    roundTime = (timeStamp2.tv_usec - timeStamp1.tv_usec);
                    //------------------------------------------------------
                    return NegotiateConnection(receiverIP, suggestedWindowSize, suggestedFrameSize);
                }
                else
                {
                    DEBUGMESSAGE(2, "SYN+ACK: Data "
                            REDTEXT("NOT OK."));
                    DEBUGMESSAGE(1, "SYN+ACK: Suggested parameters out of bounds. Connection impossible.");
                    return -1;
                }
            }
            else
            {
                DEBUGMESSAGE(2, "SYN+ACK: Sequence number "
                        REDTEXT("NOT OK."));
                return -1;
            }
        }
        else
        {
            DEBUGMESSAGE(2, "SYN+ACK: Flags "
                    REDTEXT("NOT OK."));
            return -1;
        }
    }
    return 0;
}

//---------------------------------------------------------------------------------------------------------------
// The function that continously updates the roundTime average
float roundTimeManager(int time)
{
    float roundTimeTable[BASE_AVERAGE];
    memset(roundTimeTable, '0', BASE_AVERAGE);
    float lastReportedRoundTime = 0;
    int i = 0;
    int divider = 0;
    
    while(KillThreads == 0)
    {
	for(int a = 0; a < windowSize; a++){
	    if(roundTime != lastReportedRoundTime)
	    {
		roundTimeTable[i] = roundTime;
		i++;
		if(i == BASE_AVERAGE)
		{
		    i = 0;
		}
		for(int u = 0; u < BASE_AVERAGE; u++){
		    if(roundTimeTable[u] != 0){
			divider++;
			averageRoundTime += roundTimeTable[u];
		    }
		}
		if(divider > 0){
		    averageRoundTime = (averageRoundTime/divider);
		    divider = 0;
		    DEBUGMESSAGE_EXACT(20, CYN"averageRoundTime set to: ["RESET" %f "CYN"]\n"RESET, averageRoundTime);
		}
	    }
	}
    }
    KillThreads = 1;
    pthread_exit(NULL);
}

//---------------------------------------------------------------------------------------------------------------
// The function that reads packets from the receiver, is run by a separate thread

void* ReadPackets(ACKmngr* ACKsPointer)
{
    DEBUGMESSAGE(2, "ReadPackets thread running\n");

    packet packetBuffer;
    unsigned int senderAddressLength = sizeof(senderAddress);

    while (KillThreads == 0)
    {
        ReceivePacket(socket_fd, &packetBuffer, &senderAddress, &senderAddressLength);
        if (packetBuffer.flags == PACKETFLAG_ACK)
        {
            ACKsPointer->Table[packetBuffer.sequenceNumber] = 1;
            (ACKsPointer->Missing)--;

            while (ACKsPointer->Table[lowestSequenceAwaited] == 1)
            {
            sem_post(&windowSemaphore);
                if (lowestSequenceAwaited == 65535)
                    lowestSequenceAwaited = 0;
                else
                    lowestSequenceAwaited++;
            }
            printf("ACK: [ %d ] Received     ACKs.Missing:[ %d ]\n", packetBuffer.sequenceNumber, ACKsPointer->Missing);
        }
        // TODO: Look for FINs here
    }

    KillThreads = 1;
    pthread_exit(NULL);
}
//---------------------------------------------------------------------------------------------------------------

void PrintMenu()
{
    printf(YEL"--------------------------\n"RESET);
    printf(YEL"Welcome!  "RESET YEL"\nRoundtime set to"RESET" %.0f"YEL"us\n"RESET, roundTime);
    printf(YEL"--------------------------\n"RESET);
    printf(CYN"[ "RESET"1"CYN" ]: Send Message\n"RESET);
    printf(MAG"[ "RESET"2"MAG" ]: Preview Message\n"RESET);
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

    fp = fopen("message", "r");
    if (fp != NULL)
    {
        while (tracker < MAX_MESSAGE_LENGTH)
        {
            symbol = fgetc(fp);
            if (symbol < 0)
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
        DEBUGMESSAGE(1, YELTEXT("\n -Couldn't open file 'message'- "));
    }
}


//---------------------------------------------------------------------------------------------------------------

int ThreadedTimeout(timeoutHandlerData* timeoutData)
{
    ACKmngr* ACKsPointer = timeoutData->ACKsPointer;
    int numPreviousTimeouts = timeoutData->numPreviousTimeouts;
    int sequenceNumber = timeoutData->sequenceNumber;
    packet* packetsToSend = timeoutData->packetsToSend;
    DEBUGMESSAGE(2, "ThreadedTimeout for seq %d started", sequenceNumber);

    sleep(TIMEOUT_DELAY);
    while (ACKsPointer->Table[sequenceNumber] == 0 || numPreviousTimeouts >= MAX_TIMEOUT_RETRIES)
    {
        numPreviousTimeouts++;
        SendPacket(socket_fd, &(packetsToSend[sequence]), &receiverAddress, sizeof(receiverAddress));
        sleep(TIMEOUT_DELAY);
    }

    DEBUGMESSAGE(0, "About to exit out of a thread!");
    free(timeoutData);
    DEBUGMESSAGE(0, "Memory freed");
    return numPreviousTimeouts;
}

void SlidingWindow(char* readstring, ACKmngr* ACKsPointer)
{
    system("clear"); // Clean up the console
    DEBUGMESSAGE(2, YELTEXT("---[ Sending Message ]--- "));
    float messageDivided = 0;
    int packets = 0;
    int seq = 0; // Keeps track of what frame the sliding window is currently managing

    int messageTracker = 0; // Tracks where we are located in the message that is currently being chopped up.

    // Figure out how many packets we need to send
    int messageLength = strlen(readstring);
    messageDivided = (float) messageLength / (float) frameSize;
    packets = ceil((double) messageDivided);
    DEBUGMESSAGE(4, GRNTEXT("Message is [")
            " %d "
            GRNTEXT("] symbols long"), messageLength);
    DEBUGMESSAGE(4, GRNTEXT("Will split over [%.2f] rounded to [")
            " %d "
            GRNTEXT("] packets"), messageDivided, packets);
    DEBUGMESSAGE(4, YELTEXT("WindowSize is [")
            " %d "
            YELTEXT("] frames"), windowSize);

    unsigned int receiverAddressLength = sizeof(receiverAddress);

    packet* packetsToSend;
    packetsToSend = malloc(sizeof(packet) * (windowSize + 1));
    if (packetsToSend == NULL)
    {
        CRASHWITHERROR("malloc() for packetsToSend in SlidingWindow() failed");
    }

    int bufferSlot = 0;

    for (int i = 0; i < packets; i++)
    {
        sem_wait(&windowSemaphore);

        if (bufferSlot == windowSize)
            bufferSlot = 0; // if the condition is met, we would try to write outside our buffer. No good! Loop around!

        DEBUGMESSAGE_NONEWLINE(5, YELTEXT("Sending:"));
        for (int j = messageTracker; j < (frameSize + messageTracker); j++)
        { // Fill up the outgoing packet with data
            packetsToSend[bufferSlot].data[j - messageTracker] = readstring[j];
        }

        if (lowestSequenceAwaited == -1)
            lowestSequenceAwaited = seq;

        if (i == packets - 1)
            WritePacket(&(packetsToSend[bufferSlot]), PACKETFLAG_FIN, (void*) (packetsToSend[bufferSlot].data), frameSize, seq);
        else
            WritePacket(&(packetsToSend[bufferSlot]), 0, (void*) (packetsToSend[bufferSlot].data), frameSize, seq);

        DEBUGMESSAGE(3, GRNTEXT("\n Sending Packet:[")
                " %d "
                GRNTEXT("]   seq:[")
                " %d "
                GRNTEXT("]\n"), packetsToSend[bufferSlot].sequenceNumber, seq);
        DEBUGMESSAGE(5, GRNTEXT("\n messageTracker:[")
                " %d "
                GRNTEXT("]"), messageTracker);

        SendPacket(socket_fd, &(packetsToSend[bufferSlot]), &receiverAddress, receiverAddressLength);
        printf(YEL"Message: ["RESET" %d "YEL"] Sent     "CYN"ACKs.Missing:["RESET" %d "CYN"]\n"RESET,
                packetsToSend[bufferSlot].sequenceNumber, ACKsPointer->Missing);
        ACKsPointer->Table[seq] = 0;
        (ACKsPointer->Missing)++;

        seq++;
        bufferSlot++;
        messageTracker += frameSize;
    }
}
//---------------------------------------------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    if (argc == 2)
    {
        debugLevel = strtol(argv[1], NULL, 10);
    }
    int command = 0;
    char c;
    char readstring[MAX_MESSAGE_LENGTH] = "\0";
    ACKmngr ACKs;
    ACKmngr* ACKsPointer = &ACKs;
    memset(ACKs.Table, '1', ACK_TABLE_SIZE);
    ACKs.Missing = 0;

    DEBUGMESSAGE(5, "Intializing socket...");
    socket_fd = InitializeSocket();
    DEBUGMESSAGE(1, "Socket setup successfully.");
    int retval = NegotiateConnection("127.0.0.1", windowSize, frameSize);
    if (retval == 1)
    {// Connection successful!
        if (sem_init(&windowSemaphore, 0, windowSize) == -1)
        {
            CRASHWITHERROR("Semaphore initialization failed");
        }
    }
    else if (retval == 0)
    {// Error, but probably because incorrect parameters were entered

    }
    else if (retval == -1)
    {// Error. Probably bad.

    }
    usleep(5000);

    // Create the thread checking for messages from the receiver------
    pthread_t thread; //Thread ID
    if (pthread_create(&thread, NULL, (void*) ReadPackets, ACKsPointer) != 0)
    {
        CRASHWITHERROR("pthread_create(ReadPackets) failed in main()");
    }
    //----------------------------------------------------------------



    //------------------------------------------------------------

    while (KillThreads == 0)
    {
        usleep(1000);
        system("clear"); // Clean up the console

        printf("%s\n", readstring);
        PrintMenu();

        char* commandBuffer;
        if ((commandBuffer = malloc(128)) == NULL)
        {
            CRASHWITHERROR("commandBuffer malloc failed");
        }

        scanf("%s", commandBuffer);
        command = strtol(commandBuffer, NULL, 10); // Get a command from the user
        while ((c = getchar()) != '\n' && c != EOF); //Rensar läsbufferten

        switch (command)
        {
            case 1:
                LoadMessageFromFile(readstring);
                SlidingWindow(readstring, ACKsPointer); // Send the Message
                break;
            case 2:
                LoadMessageFromFile(readstring);
                // Just sending the user back to the start of the while loop
                break;
            case 2049:
                // TODO: Let the receiver properly know that we are closing down the shop
                close(socket_fd);
                KillThreads = 1; // Make sure that we let the FINFINDER thread know that we are closing down the client
                system("clear"); // Clean up the console
                exit(EXIT_SUCCESS);
            default:
                printf("Wrong input\n");
        }
    }

    KillThreads = 1;
    return 0;
}

