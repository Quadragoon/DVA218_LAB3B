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

#include "common.h"

int socket_fd;
unsigned short sequence = 0;
int KillThreads = 0;
byte windowSize = 16;
unsigned short frameSize = 1000;

struct sockaddr_in receiverAddress;
struct sockaddr_in senderAddress;

#define MIN_ACCEPTED_WINDOW_SIZE 1
#define MAX_ACCEPTED_WINDOW_SIZE 16
#define MIN_ACCEPTED_FRAME_SIZE 1
#define MAX_ACCEPTED_FRAME_SIZE 65535

#define MAX_MESSAGE_LENGTH 10000


//---------------------------------------------------------------------------------------------------------------

int NegotiateConnection(const char* receiverIP, byte desiredWindowSize, unsigned short desiredFrameSize)
{
    DEBUGMESSAGE(5, "Negotiating connection");

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
        DEBUGMESSAGE(0, YEL"Invalid IP address entered."RESET);
    }

    unsigned int receiverAddressLength = sizeof(receiverAddress);
    unsigned int senderAddressLength = sizeof(senderAddress);

    // 'packet' struct defined in common.h
    packet packetToSend, packetBuffer;

    byte packetData[3];
    packetData[0] = desiredWindowSize;
    byte* desiredFrameSizeBytes = (byte*)(&desiredFrameSize);
    packetData[1] = desiredFrameSizeBytes[0];
    packetData[2] = desiredFrameSizeBytes[1];
    WritePacket(&packetToSend, PACKETFLAG_SYN, (void*)packetData, 3, sequence);

    SendPacket(socket_fd, &packetToSend, &receiverAddress, receiverAddressLength);
    if (ReceivePacket(socket_fd, &packetBuffer, &senderAddress, &senderAddressLength) != -1)
    {
        byte suggestedWindowSize = packetBuffer.data[0];
        unsigned short suggestedFrameSize = ntohs((packetBuffer.data[1] * 256) + packetBuffer.data[2]);

        if (packetBuffer.flags & PACKETFLAG_SYN && packetBuffer.flags & PACKETFLAG_ACK)
        {
            DEBUGMESSAGE(3, "SYN+ACK: Flags "GRN"OK"RESET);
            if (packetBuffer.sequenceNumber == (sequence))
            {
                DEBUGMESSAGE(3, "SYN+ACK: Sequence number "GRN"OK"RESET);
                if (suggestedWindowSize == desiredWindowSize && suggestedFrameSize == desiredFrameSize)
                {
                    DEBUGMESSAGE(2, "SYN+ACK: Data "GRN"OK."RESET);
                    windowSize = suggestedWindowSize;
                    frameSize = suggestedFrameSize;
                    return 1;
                }
                else
                {
                    DEBUGMESSAGE(2, "SYN+ACK: Data "RED"NOT OK."RESET);
                    DEBUGMESSAGE(1, "SYN+ACK: Suggested parameters don't match desired ones. Data corrupted?");
                    return -1;
                }
            }
            else
            {
                DEBUGMESSAGE(2, "SYN+ACK: Sequence number "RED"NOT OK"RESET);
                return -1;
            }
        }
        else if (packetBuffer.flags & PACKETFLAG_SYN && packetBuffer.flags & PACKETFLAG_NAK)
        {
            DEBUGMESSAGE(3, "SYN+NAK: Flags "GRN"OK"RESET);
            if (packetBuffer.sequenceNumber == (sequence))
            {
                DEBUGMESSAGE(3, "SYN+NAK: Sequence number "GRN"OK"RESET);
                if (suggestedWindowSize == desiredWindowSize && suggestedFrameSize == desiredFrameSize)
                {
                    DEBUGMESSAGE(1, "SYN+NAK: Data "RED"VERY NOT OK."RESET);
                    DEBUGMESSAGE(1, "SYN+NAK: Received NAK suggestion for desired parameters.\n"
                    YEL"Something is wrong."RESET);
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
                    return NegotiateConnection(receiverIP, suggestedWindowSize, suggestedFrameSize);
                }
                else
                {
                    DEBUGMESSAGE(2, "SYN+ACK: Data "RED"NOT OK."RESET);
                    DEBUGMESSAGE(1, "SYN+ACK: Suggested parameters out of bounds. Connection impossible.");
                    return -1;
                }
            }
            else
            {
                DEBUGMESSAGE(2, "SYN+ACK: Sequence number "RED"NOT OK"RESET);
                return -1;
            }
        }
        else
        {
            DEBUGMESSAGE(2, "SYN+ACK: Flags "RED"NOT OK"RESET);
            return -1;
        }
    }
    return 0;
}

//---------------------------------------------------------------------------------------------------------------
// The function that reads packets from the receiver, is run by a separate thread

void* ReadPackets(ACKmngr *ACKsPointer) {
    DEBUGMESSAGE(2, "FINFINDER thread running\n");

    ACKmngr ACKs;
    ACKs = *ACKsPointer;

    while (KillThreads == 0) {
	sleep(2);
	// TODO: Look for FINs here
    }

    KillThreads = 1;
    pthread_exit(NULL);
}
//---------------------------------------------------------------------------------------------------------------

void PrintMenu()
{
    printf(YEL"--------------------------\n"RESET);
    printf(YEL"Welcome!\n"RESET);
    printf(YEL"--------------------------\n"RESET);
    printf(CYN"[ "RESET"1"CYN" ]: Send Message\n"RESET);
    printf(MAG"[ "RESET"2"MAG" ]: Reload Message\n"RESET);
    printf(RED"[ "RESET"2049"RED" ]: End program\n"RESET);
    printf(YEL"--------------------------\n"RESET);
}
//---------------------------------------------------------------------------------------------------------------

void LoadMessageFromFile(char readstring[MAX_MESSAGE_LENGTH]) {
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
        DEBUGMESSAGE(1, YEL"\n -Couldn't open file 'message'- "RESET);

    }
}
//---------------------------------------------------------------------------------------------------------------

void SlidingWindow(char* readstring, ACKmngr* ACKsPointer) {
    system("clear"); // Clean up the console
    DEBUGMESSAGE(2, YEL"---[ Sending Message ]--- "RESET);
    float messagedivided = 0;
    int packets = 0;
    int seq = 0; // Keeps track of what frame the sliding window is currently managing
    ACKmngr ACKs;
    ACKs = *ACKsPointer;

    int MessageTracker = 0; // Tracks where we are located in the message that is currently being chopped up.

    // Figure out how many packets we need to send
    int MessageLength = strlen(readstring);
    messagedivided = (float) MessageLength / (float) frameSize;
    packets = ceil((double)messagedivided);
    DEBUGMESSAGE(4, GRN"Message is [ "RESET"%d"GRN" ] symbols long"RESET, MessageLength);
    DEBUGMESSAGE(4, GRN"Will split over [%.2f] rounded to [ "RESET"%d"GRN" ] packets"GRN, messagedivided, packets);
    DEBUGMESSAGE(4, YEL"WindowSize is [ "RESET"%d"YEL" ] frames"RESET, windowSize);

    // TODO: Implement 'Send Message' to receiver, properly ----------- // Vilken flagga används för data?

    unsigned int receiverAddressLength = sizeof(receiverAddress);
    unsigned int senderAddressLength = sizeof(senderAddress);
    packet* packetsToSend;
    packetsToSend = malloc(sizeof(packet) * (windowSize + 1));
    if (packetsToSend == NULL)
    {
        CRASHWITHERROR("malloc() in SlidingWindow() failed");
    }
    packet packetBuffer;
    // 

    for (int i = 0; i < packets; i++) {
	if (ACKs.Missing < 1)
    {

        DEBUGMESSAGE_NONEWLINE(5, YEL"Sending:"RESET);
        int ti = 0;
        for (int u = MessageTracker; u < (frameSize + MessageTracker); u++)
        { // Fill up the packet with data
            packetsToSend[seq].data[ti] = readstring[u];
            printf("%c", packetsToSend[seq].data[ti]);
            ti++;
        }
        packetsToSend[seq].sequenceNumber = seq;
        DEBUGMESSAGE(5, GRN"\n MessageTracker:["RESET" %d "GRN"]"RESET, MessageTracker);
        DEBUGMESSAGE(4, GRN"\n SequenceNumber:["RESET" %d "GRN"]   seq:["RESET" %d "GRN"]\n"RESET, packetsToSend[seq].sequenceNumber, seq);


        if (i == packets - 1)
        {
            WritePacket(&(packetsToSend[seq]), PACKETFLAG_FIN, (void*) (packetsToSend[seq].data), frameSize, seq);
        }
        else
        {
            WritePacket(&(packetsToSend[seq]), 0, (void*) (packetsToSend[seq].data), frameSize,
                        seq); // Vilken flagga används för data?
        }

        SendPacket(socket_fd, &(packetsToSend[seq]), &receiverAddress, receiverAddressLength);

        ACKs.Table[seq] = 0;
        ACKs.Missing++;
        seq++;
        MessageTracker += frameSize;
    }

	ReceivePacket(socket_fd, &packetBuffer, &senderAddress, &senderAddressLength);
	if (packetBuffer.flags == PACKETFLAG_ACK) {
	    ACKs.Table[ packetBuffer.sequenceNumber ] = 1;
	    ACKs.Missing--;
	}
	//usleep(5000);
    }
    //------------------------------
    printf("\n");
    //sleep(1);
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

    NegotiateConnection("127.0.0.1", windowSize, frameSize);

    // Create the thread checking for FINs from the receiver------
    pthread_t thread; //Thread ID
    pthread_create(&thread, NULL, (void*)ReadPackets, ACKsPointer);
    //------------------------------------------------------------

    while (KillThreads == 0)
    {
        usleep(100);

        system("clear"); // Clean up the console
        LoadMessageFromFile(readstring);
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
                SlidingWindow(readstring, ACKsPointer);
                break;
            case 2:
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

