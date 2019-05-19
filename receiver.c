//
// Created by quadragoon on 2019-05-03.
//

#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <zconf.h>
#include "common.h"

//-------------------------- A bit of color plz
#define RED   "\x1B[31m"
#define GRN   "\x1B[32m"
#define YEL   "\x1B[33m"
#define BLU   "\x1B[34m"
#define MAG   "\x1B[35m"
#define CYN   "\x1B[36m"
#define WHT   "\x1B[37m"
#define RESET "\x1B[0m"
//----------------------------------------------

#define MIN_ACCEPTED_WINDOW_SIZE 10
#define MAX_ACCEPTED_WINDOW_SIZE 10

int socket_fd;
int WindowSize = 0;

int CopyPacket(const packet* src, packet* dest) {
    dest->flags = src->flags;
    dest->sequenceNumber = src->sequenceNumber;
    dest->dataLength = src->dataLength;
    dest->checksum = src->checksum;
    for (int i = 0; i < src->dataLength; i++)
	dest->data[i] = src->data[i];
    return 1; // should return something else on fail but, uh, checking for fail in a simple function like this seems weird to do
}

int ReceiveConnection(const packet* connectionRequestPacket, struct sockaddr_in senderAddress, unsigned int senderAddressLength) {
    packet packetBuffer, packetToSend;
    memset(&packetToSend, 0, sizeof (packet));
    memset(&packetBuffer, 0, sizeof (packet));

    CopyPacket(connectionRequestPacket, &packetBuffer);

    if (packetBuffer.flags & PACKETFLAG_SYN) {
	DEBUGMESSAGE(3, "SYN: Flags "GRN"OK"RESET);
	byte requestedWindowSize = packetBuffer.data[0];
	if (requestedWindowSize >= MIN_ACCEPTED_WINDOW_SIZE && requestedWindowSize <= MAX_ACCEPTED_WINDOW_SIZE) {
	    WindowSize = requestedWindowSize; // ------------------------------------JANNE LEKER HÄR--------------------------------------------
	    DEBUGMESSAGE(3, "SYN: Data "GRN"OK"RESET);
	    WritePacket(&packetToSend, PACKETFLAG_SYN | PACKETFLAG_ACK,
		    &requestedWindowSize, sizeof (requestedWindowSize), packetBuffer.sequenceNumber);
	    SendPacket(socket_fd, &packetToSend, &senderAddress, senderAddressLength);
	} else if (requestedWindowSize < MIN_ACCEPTED_WINDOW_SIZE) {
	    DEBUGMESSAGE(3, "SYN: Data "BLU"NEGOTIABLE"RESET);
	    byte suggestedWindowSize = MIN_ACCEPTED_WINDOW_SIZE;
	    WritePacket(&packetToSend, PACKETFLAG_SYN | PACKETFLAG_NAK,
		    &suggestedWindowSize, sizeof (suggestedWindowSize), packetBuffer.sequenceNumber);
	    SendPacket(socket_fd, &packetToSend, &senderAddress, senderAddressLength);
	} else if (requestedWindowSize > MAX_ACCEPTED_WINDOW_SIZE) {
	    DEBUGMESSAGE(3, "SYN: Data "BLU"NEGOTIABLE"RESET);
	    byte suggestedWindowSize = MAX_ACCEPTED_WINDOW_SIZE;
	    WritePacket(&packetToSend, PACKETFLAG_SYN | PACKETFLAG_NAK,
		    &suggestedWindowSize, sizeof (suggestedWindowSize), packetBuffer.sequenceNumber);
	    SendPacket(socket_fd, &packetToSend, &senderAddress, senderAddressLength);
	} else {
	    DEBUGMESSAGE(2, "SYN: Data "RED"NOT OK"RESET);
	}
    } else {
	DEBUGMESSAGE(2, "SYN: Flags "RED"NOT OK"RESET);
    }

    return 0;
}

void ReadIncomingMessages() {
    struct sockaddr_in receiverAddress, senderAddress;
    memset(&senderAddress, 0, sizeof (struct sockaddr_in));
    unsigned int senderAddressLength = sizeof (senderAddress);

    packet packetBuffer;
    memset(&packetBuffer, 0, sizeof (packet));

    while (1) {
	ReceivePacket(socket_fd, &packetBuffer, &senderAddress, &senderAddressLength);
	//--------------------------------------------------------------------------JANNE LEKER HÄR-------------------------------------------- 
	if (packetBuffer.flags == 7) { // Vad använder vi för flagga till datapaket?
	    char Filler[10] = "I hear ya";
	    printf("%s", packetBuffer.data);

	    packet packetToSend;
	    memset(&packetToSend, 0, sizeof (packet));
	    
	    WritePacket(&packetToSend, PACKETFLAG_ACK, (void*)Filler, WindowSize, packetBuffer.sequenceNumber); //------What data to send with the ACK? any?
	    SendPacket(socket_fd, &packetToSend, &senderAddress, senderAddressLength);
	}
	else if (packetBuffer.flags == 8 ){ // Oh lordy, kill it with fire
	    char Filler[10] = "thank u";
	    printf("%s\n", packetBuffer.data);

	    packet packetToSend;
	    memset(&packetToSend, 0, sizeof (packet));
	    
	    WritePacket(&packetToSend, PACKETFLAG_ACK, (void*)Filler, WindowSize, packetBuffer.sequenceNumber); //------What data to send with the ACK? any?
	    SendPacket(socket_fd, &packetToSend, &senderAddress, senderAddressLength);
	}
	//--------------------------------------------------------------------------JANNE LEKTE HÄR--------------------------------------------
	else if (packetBuffer.flags & PACKETFLAG_SYN) {
	    ReceiveConnection(&packetBuffer, senderAddress, senderAddressLength);
	}
	if (packetBuffer.sequenceNumber == 246)
	    break; // compiler whines about endless loops without this bit
	
    }
}

int main(int argc, char* argv[]) {
    if (argc == 2) {
	debugLevel = atoi(argv[1]);
    }

    socket_fd = InitializeSocket();

    const int sockoptval = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEPORT, &sockoptval, sizeof (sockoptval)) < 0) {
	CRASHWITHERROR("setsockopt() failed");
    }

    struct sockaddr_in socketAddress;
    socketAddress.sin_family = AF_INET;
    socketAddress.sin_port = htons(LISTENING_PORT);
    socketAddress.sin_addr.s_addr = INADDR_ANY;
    if (bind(socket_fd, (struct sockaddr*) &socketAddress, sizeof (socketAddress)) < 0) {
	CRASHWITHERROR("bind() failed");
    }

    DEBUGMESSAGE(1, "Socket setup and bound successfully.");

    ReadIncomingMessages();

    return 0;
}