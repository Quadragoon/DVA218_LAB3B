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

//-------------------------- A bit of color bitte
#define RED   "\x1B[31m"
#define GRN   "\x1B[32m"
#define YEL   "\x1B[33m"
#define BLU   "\x1B[34m"
#define MAG   "\x1B[35m"
#define CYN   "\x1B[36m"
#define WHT   "\x1B[37m"
#define RESET "\x1B[0m"
//----------------------------------------------

int socket_fd;
unsigned short sequence = 0;
int KillThreads = 0;
byte desiredWindowSize = 16;

struct sockaddr_in receiverAddress;
struct sockaddr_in senderAddress;

#define MIN_ACCEPTED_WINDOW_SIZE 1
#define MAX_ACCEPTED_WINDOW_SIZE 16
#define MAX_MESSAGE_LENGTH 1500
#define FRAME_SIZE 10  // Why is the data length set to the negoiated window size right now, window size is supposed to be the number of frames no?


//---------------------------------------------------------------------------------------------------------------

int NegotiateConnection(const char* receiverIP, const byte* desiredWindowSize) {
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
	DEBUGMESSAGE(0, YEL"Invalid IP address entered."RESET);
    }

    unsigned int receiverAddressLength = sizeof (receiverAddress);
    unsigned int senderAddressLength = sizeof (senderAddress);

    // 'packet' struct defined in common.h
    packet packetToSend, packetBuffer;

    WritePacket(&packetToSend, PACKETFLAG_SYN, (void*) desiredWindowSize, sizeof (*desiredWindowSize), sequence);

    SendPacket(socket_fd, &packetToSend, &receiverAddress, receiverAddressLength);
    ReceivePacket(socket_fd, &packetBuffer, &senderAddress, &senderAddressLength);

    if (packetBuffer.flags & PACKETFLAG_SYN && packetBuffer.flags & PACKETFLAG_ACK) {
	DEBUGMESSAGE(3, "SYN+ACK: Flags "GRN"OK"RESET);
	if (packetBuffer.sequenceNumber == (sequence)) {
	    DEBUGMESSAGE(3, "SYN+ACK: Sequence number "GRN"OK"RESET);
	    byte suggestedWindowSize = packetBuffer.data[0];
	    if (suggestedWindowSize == *desiredWindowSize) {
		DEBUGMESSAGE(2, "SYN+ACK: Data "GRN"OK."RESET);
		return *desiredWindowSize;
	    } else if (suggestedWindowSize >= MIN_ACCEPTED_WINDOW_SIZE && suggestedWindowSize <= MAX_ACCEPTED_WINDOW_SIZE) {
		return NegotiateConnection(receiverIP, &suggestedWindowSize);
	    } else if (suggestedWindowSize < MIN_ACCEPTED_WINDOW_SIZE || suggestedWindowSize > MAX_ACCEPTED_WINDOW_SIZE) {
		DEBUGMESSAGE(2, "SYN+ACK: Data "RED"NOT OK."RESET);
		DEBUGMESSAGE(1, "SYN+ACK: Suggested window size out of bounds. Connection impossible.");
		return -1;
	    }
	} else {
	    DEBUGMESSAGE(2, "SYN+ACK: Sequence number "RED"NOT OK"RESET);
	    return -1;
	}
    } else if (packetBuffer.flags & PACKETFLAG_SYN && packetBuffer.flags & PACKETFLAG_NAK) {
	DEBUGMESSAGE(3, "SYN+ACK: Flags "BLU"NEGOTIABLE"RESET);
	if (packetBuffer.sequenceNumber == (sequence)) {
	    DEBUGMESSAGE(3, "SYN+ACK: Sequence number "GRN"OK"RESET);
	    byte suggestedWindowSize = packetBuffer.data[0];
	    if (suggestedWindowSize == *desiredWindowSize) {
		DEBUGMESSAGE(1, "SYN+ACK: Data "RED"VERY NOT OK."RESET);
		DEBUGMESSAGE(1, "SYN+ACK: Received NAK suggestion for desired window size.\n"YEL"Something is wrong."RESET)
		return -1;
	    } else if (suggestedWindowSize >= MIN_ACCEPTED_WINDOW_SIZE && suggestedWindowSize <= MAX_ACCEPTED_WINDOW_SIZE) {
		return NegotiateConnection(receiverIP, &suggestedWindowSize);
	    } else if (suggestedWindowSize < MIN_ACCEPTED_WINDOW_SIZE || suggestedWindowSize > MAX_ACCEPTED_WINDOW_SIZE) {
		DEBUGMESSAGE(2, "SYN+ACK: Data "RED"NOT OK."RESET);
		DEBUGMESSAGE(1, "SYN+ACK: Suggested window size out of bounds. Connection impossible.");
		return -1;
	    }
	} else {
	    DEBUGMESSAGE(2, "SYN+ACK: Sequence number "RED"NOT OK"RESET);
	    return -1;
	}
    } else {
	DEBUGMESSAGE(2, "SYN+ACK: Flags "RED"NOT OK"RESET);
	return -1;
    }
    return 0;
}



//---------------------------------------------------------------------------------------------------------------
// The function that reads packets from the receiver, is run by a separate thread

void * FINFINDER() {
    DEBUGMESSAGE(2, "FINFINDER thread running\n");

    while (KillThreads == 0) {
	sleep(1);
	// TODO: Look for FINs here
    }

    KillThreads = 1;
    pthread_exit(NULL);
}
//---------------------------------------------------------------------------------------------------------------

void PrintMenu() {
    printf(YEL"--------------------------\n"RESET);
    printf(YEL"Welcome!\n"RESET);
    printf(YEL"--------------------------\n"RESET);
    printf(CYN"[ "RESET"1"CYN" ]: Send Message\n"RESET);
    printf(MAG"[ "RESET"2"MAG" ]: Reload Message\n"RESET);
    printf(RED"[ "RESET"2049"RED" ]: End program\n"RESET);
    printf(YEL"--------------------------\n"RESET);
}
//---------------------------------------------------------------------------------------------------------------

void ReadMessage(char readstring[MAX_MESSAGE_LENGTH]) {
    system("clear"); // Clean up the console
    printf(YEL"---[ Message Preview ]--- \n"RESET);

    //---------------------------

    FILE *fp;
    size_t len = 0;
    int symbol;
    int tracker = 0;

    fp = fopen("message", "r");
    if (fp != NULL) {
	while (tracker < MAX_MESSAGE_LENGTH) {
	    symbol = fgetc(fp);
	    if (symbol < 0) { // Check for the end of the file
		break;
	    }
	    readstring[tracker] = (char*) symbol;
	    tracker++;
	}
	fclose(fp);
    } else {
	DEBUGMESSAGE(1,YEL"\n -Couldn't open file 'message'- "RESET);

    }
}
//---------------------------------------------------------------------------------------------------------------

void SlidingWindow(char readstring[MAX_MESSAGE_LENGTH], int WindowSize) {
    system("clear"); // Clean up the console
    DEBUGMESSAGE(2,YEL"---[ Sending Message ]--- "RESET);
    float messagedivided = 0;
    int packets = 0;
    int seq = 0; // Keeps track of what frame the sliding window is currently managing
    int ACKsMissing = 0; // Keeps track of how many ACKs are still unaccounted for
    int ACKtable[MAX_ACCEPTED_WINDOW_SIZE]; // Keeps track of exactly which ACKs are missing
    memset(ACKtable, '1', MAX_ACCEPTED_WINDOW_SIZE); 
    int MessageTracker = 0; // Tracks where we are located in the message that is currently being chopped up.


    // Figure out how many packets we need to send
    int MessageLength = strlen(readstring);
    messagedivided = (float) MessageLength / (float) FRAME_SIZE;
    packets = ceil(messagedivided);
    DEBUGMESSAGE(4,GRN"Message is [ "RESET"%d"GRN" ] symbols long"RESET, MessageLength);
    DEBUGMESSAGE(4,GRN"Will split over [%.2f] rounded to [ "RESET"%d"GRN" ] packets"GRN, messagedivided, packets);
    DEBUGMESSAGE(4,YEL"WindowSize is [ "RESET"%d"YEL" ] frames"RESET, WindowSize);


    // TODO: Implement 'Send Message' to receiver, properly ----------- // Vilken flagga används för data?

    unsigned int receiverAddressLength = sizeof (receiverAddress);
    unsigned int senderAddressLength = sizeof (senderAddress);
    packet packetToSend[WindowSize];
    packet packetBuffer;
    // 

    for (int i = 0; i < packets; i++) {
	if (ACKsMissing < WindowSize + 1) {
	    
	    DEBUGMESSAGE_NONEWLINE(4,YEL"Sending:"RESET);
	    for(int u = MessageTracker; u < (FRAME_SIZE+MessageTracker); u++){ // Fill up the packet with data
		packetToSend[seq].data[u] = readstring[u];
		printf("%c", packetToSend[seq].data[u]); // Works this far..
	    }
	    DEBUGMESSAGE(4,GRN"\n MessageTracker:["RESET" %d "GRN"]"RESET,MessageTracker);
	    
	    WritePacket(&(packetToSend[seq]), 7, (void*)(packetToSend[seq].data), WindowSize, seq); // Vilken flagga används för data?
	    
	    SendPacket(socket_fd, &(packetToSend[seq]), &receiverAddress, receiverAddressLength);
	    
	    ACKtable[seq] = 0;
	    ACKsMissing++;
	    seq++;
	    MessageTracker += FRAME_SIZE;
	}
	
	ReceivePacket(socket_fd, &packetBuffer, &senderAddress, &senderAddressLength);
	if(packetBuffer.flags == PACKETFLAG_ACK){
	    ACKtable[  packetBuffer.sequenceNumber  ] = 1;
	    ACKsMissing--;
	}
	
    }


    //------------------------------

    printf("\n");
    sleep(5);
}
//---------------------------------------------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    if (argc == 2) {
	debugLevel = atoi(argv[1]);
    }
    int command = 0;
    char c;
    char readstring[MAX_MESSAGE_LENGTH] = "\0";

    socket_fd = InitializeSocket();
    DEBUGMESSAGE(1, "Socket setup successfully.");

    int *socketpointer = &socket_fd;
    int WindowSize = NegotiateConnection("127.0.0.1", &desiredWindowSize);

    // Create the thread checking for FINs from the receiver------
    pthread_t thread; //Thread ID
    pthread_create(&thread, NULL, FINFINDER, NULL);
    //------------------------------------------------------------

    while (KillThreads == 0) {
	usleep(100);

	system("clear"); // Clean up the console
	ReadMessage(readstring);
	printf("%s\n", readstring);
	PrintMenu();

	scanf(" %d", &command); // Get a command from the user
	while ((c = getchar()) != '\n' && c != EOF); //Rensar läsbufferten

	switch (command) {
	    case 1:
		SlidingWindow(readstring, WindowSize);
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
		break;
	    default: printf("Wrong input\n");
	}
    }

    KillThreads = 1;
    return 0;
}

