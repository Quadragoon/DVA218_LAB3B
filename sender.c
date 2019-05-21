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
unsigned short frameSize = 500;
float roundTime = 0;
float averageRoundTime = 0;
struct roundTimeHandler timeStamper[50];


struct sockaddr_in receiverAddress;
struct sockaddr_in senderAddress;

sem_t windowSemaphore, roundTimeSemaphore;
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

int NegotiateConnection(const char* receiverIP, byte desiredWindowSize, unsigned short desiredFrameSize) {
    DEBUGMESSAGE(3, "Negotiating connection");
    struct timeval timeStamp1, timeStamp2;

    // memset used to make sure that the address fields are filled with '0's
    memset(&receiverAddress, 0, sizeof (struct sockaddr_in));
    memset(&senderAddress, 0, sizeof (struct sockaddr_in));

    // Socket address format set to AF_INET for Internet use, 
    receiverAddress.sin_family = AF_INET;
    // Set port number to LISTENING_PORT (defined in common.c). The function htons converts from host byte order to network byte order.
    receiverAddress.sin_port = htons(LISTENING_PORT);

    // Get retval(return value) from inet_pton - (convert IPv4 and IPv6 addresses from text to binary form).  
    int retval = inet_pton(receiverAddress.sin_family, receiverIP, &(receiverAddress.sin_addr));

    if (retval == -1) {
	CRASHWITHERROR("NegotiateConnection() failed to parse IP address");
    } else if (retval == 0) {
	DEBUGMESSAGE(0, YELTEXT("Invalid IP address entered"));
    }

    unsigned int receiverAddressLength = sizeof (receiverAddress);
    unsigned int senderAddressLength = sizeof (senderAddress);

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
    if (ReceivePacket(socket_fd, &packetBuffer, &senderAddress, &senderAddressLength) != -1) {
	byte suggestedWindowSize = packetBuffer.data[0];
	unsigned short suggestedFrameSize = ntohs((packetBuffer.data[1] * 256) + packetBuffer.data[2]);

	if (packetBuffer.flags & PACKETFLAG_SYN && packetBuffer.flags & PACKETFLAG_ACK) {
	    DEBUGMESSAGE(3, "SYN+ACK: Flags "
		    GRNTEXT("OK"));
	    if (packetBuffer.sequenceNumber == (sequence)) {
		DEBUGMESSAGE(3, "SYN+ACK: Sequence number "
			GRNTEXT("OK"));
		if (suggestedWindowSize == desiredWindowSize && suggestedFrameSize == desiredFrameSize) {
		    DEBUGMESSAGE(2, "SYN+ACK: Data "
			    GRNTEXT("OK"));
		    windowSize = suggestedWindowSize;
		    frameSize = suggestedFrameSize;
		    //------------------------------------------------------TIMESTAMP2
		    gettimeofday(&timeStamp2, NULL);
		    roundTime = (timeStamp2.tv_usec - timeStamp1.tv_usec);
		    //------------------------------------------------------
		    return 1;
		} else {
		    DEBUGMESSAGE(2, "SYN+ACK: Data "
			    REDTEXT("NOT OK."));
		    DEBUGMESSAGE(1, "SYN+ACK: Suggested parameters don't match desired ones. Data corrupted?");
		    return -1;
		}
	    } else {
		DEBUGMESSAGE(2, "SYN+ACK: Sequence number "
			REDTEXT("NOT OK."));
		return -1;
	    }
	} else if (packetBuffer.flags & PACKETFLAG_SYN && packetBuffer.flags & PACKETFLAG_NAK) {
	    DEBUGMESSAGE(3, "SYN+NAK: Flags "
		    GRNTEXT("OK"));
	    if (packetBuffer.sequenceNumber == (sequence)) {
		DEBUGMESSAGE(3, "SYN+NAK: Sequence number "
			GRNTEXT("OK"));
		if (suggestedWindowSize == desiredWindowSize && suggestedFrameSize == desiredFrameSize) {
		    DEBUGMESSAGE(1, "SYN+NAK: Data "
			    REDTEXT("VERY NOT OK."));
		    DEBUGMESSAGE(1, "SYN+NAK: Received NAK suggestion for desired parameters.\n"
			    YELTEXT("Something is wrong."));
		    return -1;
		} else if (suggestedWindowSize >= MIN_ACCEPTED_WINDOW_SIZE &&
			suggestedWindowSize <= MAX_ACCEPTED_WINDOW_SIZE &&
			suggestedFrameSize >= MIN_ACCEPTED_FRAME_SIZE &&
			suggestedFrameSize <= MAX_ACCEPTED_FRAME_SIZE) {
		    DEBUGMESSAGE(1, "SYN+NAK: Renegotiating connection...");
		    DEBUGMESSAGE(3, "SYN+NAK: Trying again with parameters window:%d and frame:%d",
			    suggestedWindowSize, suggestedFrameSize);
		    return NegotiateConnection(receiverIP, suggestedWindowSize, suggestedFrameSize);
		} else {
		    DEBUGMESSAGE(2, "SYN+ACK: Data "
			    REDTEXT("NOT OK."));
		    DEBUGMESSAGE(1, "SYN+ACK: Suggested parameters out of bounds. Connection impossible.");
		    return -1;
		}
	    } else {
		DEBUGMESSAGE(2, "SYN+ACK: Sequence number "
			REDTEXT("NOT OK."));
		return -1;
	    }
	} else {
	    DEBUGMESSAGE(2, "SYN+ACK: Flags "
		    REDTEXT("NOT OK."));
	    return -1;
	}
    }
    return 0;
}

//---------------------------------------------------------------------------------------------------------------
// The function that continously updates the roundTime average

float roundTimeManager() {
    memset(timeStamper, 0, 50 * 8);
    float roundTimeTable[BASE_AVERAGE];
    memset(roundTimeTable, 0, BASE_AVERAGE);
    float lastReportedRoundTime = 0;
    int i = 0;
    int divider = 0;
    int roundTimeSemCounter = 0;
    DEBUGMESSAGE_EXACT(DEBUGLEVEL_ROUNDTIME, MAG"roundTimeManager thread up and running, waiting for roundTimeSemaphore\n"RESET);

    while (KillThreads != 1) {
	sem_wait(&roundTimeSemaphore);
	if (KillThreads == 1) {
	    printf(RED"------------roundTimeManager KillThreads: ["RESET" %d "RED"]------------\n"RESET, KillThreads);
	    printf(RED"------------roundTimeManager thread shutting down------------\n"RESET);
	    usleep(1000);
	    pthread_exit(NULL);
	    break;
	}
	sem_getvalue(&roundTimeSemaphore, &roundTimeSemCounter);
	for (int a = 0; a < windowSize; a++) {
	    if (roundTime != lastReportedRoundTime) {
		roundTimeTable[i] = roundTime;
		i++;
		if (i == BASE_AVERAGE) {
		    i = 0;
		}
		for (int u = 0; u < BASE_AVERAGE; u++) {
		    if (roundTimeTable[u] != 0) {
			divider++;
			averageRoundTime += roundTimeTable[u];
		    }
		}
		if (divider > 0) {
		    averageRoundTime = (averageRoundTime / divider);
		    DEBUGMESSAGE_EXACT(DEBUGLEVEL_ROUNDTIME, CYN"averageRoundTime set to: ["RESET" %.0f "CYN"]     using: ["RESET" %d "CYN"] samples.  SemCounter: ["RESET" %d "CYN"]\n"RESET, averageRoundTime, divider, roundTimeSemCounter);
		    divider = 0;
		}
		lastReportedRoundTime = roundTime;
	    } else {
		DEBUGMESSAGE_EXACT(DEBUGLEVEL_ROUNDTIME, CYN"."RESET);
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

void* ReadPackets(ACKmngr* ACKsPointer) {
    DEBUGMESSAGE(3, "ReadPackets thread running\n");
    int MissingACKS = 0;

    packet packetBuffer;
    unsigned int senderAddressLength = sizeof (senderAddress);

    while (KillThreads != 1) {
	ReceivePacket(socket_fd, &packetBuffer, &senderAddress, &senderAddressLength); // Thread gets stuck here on shutdown?
	sem_post(&roundTimeSemaphore); // Add 1 to roundTimeSemaphore
	if (KillThreads == 1) {
	    printf(RED"------------ReadPackets KillThreads: ["RESET" %d "RED"]------------\n"RESET, KillThreads);
	    printf(RED"------------ReadPackets thread shutting down------------\n"RESET);
	    usleep(1000);
	    pthread_exit(NULL);
	    break;
	}
	if (packetBuffer.flags == PACKETFLAG_ACK) {
	    
	    ACKsPointer->Table[packetBuffer.sequenceNumber] = 1;
	    
	    //-------------------------------------------- Updating the Missing ACKS
	    MissingACKS = 0;
	    for (int y = 0; y < ACK_TABLE_SIZE; y++) {
		if (ACKsPointer->Table[y] == 0) {
		    MissingACKS++;
		}
		ACKsPointer->Missing = MissingACKS;
	    }
	    DEBUGMESSAGE_EXACT(DEBUGLEVEL_READPACKETS, CYN"ACK: ["RESET" %d "CYN"] Received     ACKs.Missing:["RESET" %d "CYN"]\n"RESET, packetBuffer.sequenceNumber, ACKsPointer->Missing);
	    //-------------------------------------------- Updating the roundTime
	    for (int i = 0; i < 50; i++) {
		if (timeStamper[i].sequence == packetBuffer.sequenceNumber) {
		    gettimeofday(&timeStamper[i].timeStampEnd, NULL); //--------------------------------------TIMESTAMPEND
		    roundTime = (timeStamper[i].timeStampEnd.tv_usec - timeStamper[i].timeStampStart.tv_usec);
		    break;
		}
	    }
	    //--------------------------------------------

	    while (ACKsPointer->Table[lowestSequenceAwaited] == 1) {
		sem_post(&windowSemaphore);
		if (lowestSequenceAwaited == 65535)
		    lowestSequenceAwaited = 0;
		else
		    lowestSequenceAwaited++;
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
	    break;	
	}

    }


    printf(RED"------------ReadPackets thread shutting down------------\n"RESET);
    usleep(1000);
    pthread_exit(NULL);
}
//---------------------------------------------------------------------------------------------------------------

void PrintMenu() {
    int roundTimeSemCounter = 0;
    sem_getvalue(&roundTimeSemaphore, &roundTimeSemCounter);

    printf(YEL"--------------------------\n"RESET);
    printf(YEL"Welcome!  "RESET YEL"\n[%d] Roundtime Average"RESET" %.0f"YEL"us\n"RESET, averageRoundTime, roundTimeSemCounter);
    printf(YEL"--------------------------\n"RESET);
    printf(CYN"[ "RESET"1"CYN" ]: Send Message\n"RESET);
    printf(MAG"[ "RESET"2"MAG" ]: Preview Message\n"RESET);
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
    if (fp != NULL) {
	while (tracker < MAX_MESSAGE_LENGTH) {
	    symbol = fgetc(fp);
	    if (symbol < 0) { // Check for the end of the file
		break;
	    }
	    readstring[tracker] = (char) symbol;
	    tracker++;
	}
	fclose(fp);
    } else {
	DEBUGMESSAGE(0, YELTEXT("\n -Couldn't open file 'message'- "));
    }
}

//---------------------------------------------------------------------------------------------------------------

int ThreadedTimeout(timeoutHandlerData* timeoutData) {
    ACKmngr* ACKsPointer = timeoutData->ACKsPointer;
    int numPreviousTimeouts = timeoutData->numPreviousTimeouts;
    int sequenceNumber = timeoutData->sequenceNumber;
    packet* packetsToSend = timeoutData->packetsToSend;
    DEBUGMESSAGE(3, "ThreadedTimeout for seq %d started", sequenceNumber);

    sleep(TIMEOUT_DELAY);
    while (ACKsPointer->Table[sequenceNumber] == 0 || numPreviousTimeouts >= MAX_TIMEOUT_RETRIES) {
	numPreviousTimeouts++;
	SendPacket(socket_fd, &(packetsToSend[sequence]), &receiverAddress, sizeof (receiverAddress));
	sleep(TIMEOUT_DELAY);
    }

    printf(RED"------------ThreadedTimeout thread shutting down------------\n"RESET);
    free(timeoutData);
    return numPreviousTimeouts;
}

//---------------------------------------------------------------------------------------------------------------

void SlidingWindow(char* readstring, ACKmngr* ACKsPointer) {
    system("clear"); // Clean up the console
    DEBUGMESSAGE(2, YELTEXT("---[ Sending Message ]--- "));
    float messageDivided = 0;
    int packets = 0;
    int seq = 0; // Keeps track of what frame the sliding window is currently managing
    int stampID = 0;
    int MissingACKS = 0;

    int messageTracker = 0; // Tracks where we are located in the message that is currently being chopped up.

    // Figure out how many packets we need to send
    int messageLength = strlen(readstring);
    messageDivided = (float) messageLength / (float) frameSize;
    packets = ceil((double) messageDivided);
    DEBUGMESSAGE(3, GRNTEXT("Message is [")
	    " %d "
	    GRNTEXT("] symbols long"), messageLength);
    DEBUGMESSAGE(3, GRNTEXT("Will split over [%.2f] rounded to [")
	    " %d "
	    GRNTEXT("] packets"), messageDivided, packets);
    DEBUGMESSAGE(3, YELTEXT("WindowSize is [")
	    " %d "
	    YELTEXT("] frames"), windowSize);

    unsigned int receiverAddressLength = sizeof (receiverAddress);

    packet* packetsToSend;
    packetsToSend = malloc(sizeof (packet) * (windowSize + 1));
    if (packetsToSend == NULL) {
	CRASHWITHERROR("malloc() for packetsToSend in SlidingWindow() failed");
    }

    int bufferSlot = 0;

    for (int i = 0; i < packets; i++) {
	sem_wait(&windowSemaphore);

	if (bufferSlot == windowSize)
	    bufferSlot = 0; // if the condition is met, we would try to write outside our buffer. No good! Loop around!

	for (int j = messageTracker; j < (frameSize + messageTracker); j++) { // Fill up the outgoing packet with data
	    packetsToSend[bufferSlot].data[j - messageTracker] = readstring[j];
	}

	if (lowestSequenceAwaited == -1)
	    lowestSequenceAwaited = seq;

	if (i == packets - 1)
	    WritePacket(&(packetsToSend[bufferSlot]), PACKETFLAG_FIN, (void*) (packetsToSend[bufferSlot].data), frameSize, seq);
	else
	    WritePacket(&(packetsToSend[bufferSlot]), 0, (void*) (packetsToSend[bufferSlot].data), frameSize, seq);

	DEBUGMESSAGE(3, BLU"\n----------------------Sending Packet:["RESET" %d "BLU"]   seq:["RESET" %d "BLU"]   messageTracker:["RESET" %d "BLU"]"RESET, packetsToSend[bufferSlot].sequenceNumber, seq, messageTracker);

	// Managing the semaphore responsible for letting lose the roundTimeManager--------------------
	sem_post(&roundTimeSemaphore); // Add 1 to roundTimeSemaphore
	//-------------------------------------------------------------

	//Providing timestamps for the roundTimeManager to use
	timeStamper[stampID].sequence = packetsToSend[bufferSlot].sequenceNumber;
	gettimeofday(&timeStamper[stampID].timeStampStart, NULL); //--------------------------------------TIMESTAMPSTART
	//--------------------------------------------------------------

	stampID++;
	if (stampID == 50) {
	    stampID = 0;
	}
	SendPacket(socket_fd, &(packetsToSend[bufferSlot]), &receiverAddress, receiverAddressLength);
	
	ACKsPointer->Table[seq] = 0; 
	(ACKsPointer->Missing)++;

	DEBUGMESSAGE(3, YEL
		"Message: ["
		RESET
		" %d "
		YEL
		"] Sent     "
		MAG
		"ACKs.Missing:["
		RESET
		" %d "
		MAG
		"]"
		RESET,
		packetsToSend[bufferSlot].sequenceNumber, ACKsPointer->Missing);


	seq++;
	bufferSlot++;
	messageTracker += frameSize;
    }
}
//---------------------------------------------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    system("clear");
    srandom(time(NULL));
    if (argc == 2) {
	debugLevel = strtol(argv[1], NULL, 10);
    }
    int command = 0;
    char c;
    char readstring[MAX_MESSAGE_LENGTH] = "\0";

    // Setup the ACK struct used for tracking ACKS----
    ACKmngr ACKs;
    ACKmngr* ACKsPointer = &ACKs;
    for (int y = 0; y < ACK_TABLE_SIZE; y++) {
	ACKs.Table[y] = 1;
    }
    ACKs.Missing = 0;
    //---------------------------------------------

    DEBUGMESSAGE(3, "Intializing socket...");
    socket_fd = InitializeSocket();
    DEBUGMESSAGE(1, "Socket setup successfully.");
    int retval = NegotiateConnection("127.0.0.1", windowSize, frameSize);
    if (retval == 1) {// Connection successful!
	if (sem_init(&windowSemaphore, 0, windowSize) == -1) {
	    CRASHWITHERROR("Semaphore initialization failed");
	}
    } else if (retval == 0) {// Error, but probably because incorrect parameters were entered

    } else if (retval == -1) {// Error. Probably bad.

    }
    // Round time manager is ready to go
    if (sem_init(&roundTimeSemaphore, 0, windowSize) == -1) {
	CRASHWITHERROR("Semaphore roundTimeSemaphore initialization failed in main()");
    }
    usleep(5000);

    // Create the thread checking for messages from the receiver------
    pthread_t thread; //Thread ID
    if (pthread_create(&thread, NULL, (void*) ReadPackets, ACKsPointer) != 0) {
	CRASHWITHERROR("pthread_create(ReadPackets) failed in main()");
    }
    //----------------------------------------------------------------
    // Create the thread managing the average roundtime calculation------
    if (pthread_create(&thread, NULL, (void*) roundTimeManager, NULL) != 0) {
	CRASHWITHERROR("pthread_create(ReadPackets) failed in main()");
    }
    //----------------------------------------------------------------


    while (KillThreads == 0) {
	usleep(1000);
	//system("clear"); // Clean up the console
	//printf("%s\n", readstring);
	PrintMenu();

	char* commandBuffer;
	if ((commandBuffer = malloc(128)) == NULL) {
	    CRASHWITHERROR("commandBuffer malloc failed");
	}

	scanf("%s", commandBuffer);
	command = strtol(commandBuffer, NULL, 10); // Get a command from the user
	while ((c = getchar()) != '\n' && c != EOF); // Cleaning out the readbuffer

	switch (command) {
	    case 1:
		system("clear"); // Clean up the console
		LoadMessageFromFile(readstring);
		SlidingWindow(readstring, ACKsPointer); // Send the Message
		break;
	    case 2:
		LoadMessageFromFile(readstring);
		printf("%s\n", readstring);
		// Just sending the user back to the start of the while loop
		break;
	    case 2049:
		// TODO: Let the receiver properly know that we are closing down the shop
		// TODO: Let the receiver properly know that we are closing down the shop
		// TODO: Let the receiver properly know that we are closing down the shop
		system("clear");
		KillThreads = 1; // Make sure that we let the other threads know that we are closing down the client

	    default:
		printf("Wrong input\n");
	}
    }

    //
    printf(YEL"SHUTTING DOWN....\n"RESET);
    sem_post(&roundTimeSemaphore); // Make sure that the rounTimeManager isn't stuck.
    usleep(10000);
    close(socket_fd);
    for (int p = 0; p < 10; p++) {
	usleep(100000);
    }
    printf("Thank you come again :D\n");
    sleep(1);
    system("clear"); // Clean up the console
    exit(EXIT_SUCCESS);
    return 0;
}

