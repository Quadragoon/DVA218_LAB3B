//
// Created by quadragoon on 2019-05-06.
//

#include "common.h"


#define CRASHWITHERROR(message) perror(message);exit(EXIT_FAILURE)
#define LISTENING_PORT 23456
#define PACKET_BUFFER_SIZE 2048

int InitializeSocket() {
    int socket_fd = 0;
    if ((socket_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
	CRASHWITHERROR("socket() failed");
    } else
	return socket_fd;
}

ssize_t SendMessage(int socket_fd, const char* dataBuffer, int length, const struct sockaddr_in* receiverAddress,
	unsigned int addressLength) {
    int retval = sendto(socket_fd, dataBuffer, length, MSG_CONFIRM, (struct sockaddr*) receiverAddress, addressLength);
    if (retval < 0) {
	CRASHWITHERROR("SendMessage() failed");
    } else
	return retval;
}

ssize_t
ReceiveMessage(int socket_fd, char* packetBuffer, struct sockaddr_in* senderAddress, unsigned int* addressLength) {
    int retval = recvfrom(socket_fd, packetBuffer, PACKET_BUFFER_SIZE, MSG_WAITALL, (struct sockaddr*) senderAddress,
	    addressLength);
    if (retval < 0) {
	CRASHWITHERROR("SendMessage() failed");
    } else
	return retval;
}

ssize_t
SendPacket(int socket_fd, packet* packetToSend, const struct sockaddr_in* receiverAddress, unsigned int addressLength) {
    DEBUGMESSAGE(1, "Sending packet");
    packetToSend->checksum = 0;
    packetToSend->checksum = (CalculateChecksum(packetToSend) ^ 65535u);

    DEBUGMESSAGE(3, "Checksum is set to %d", packetToSend->checksum);

    int packetLength = PACKET_HEADER_LENGTH + packetToSend->dataLength;
    int retval = sendto(socket_fd, packetToSend, packetLength, MSG_CONFIRM, (struct sockaddr*) receiverAddress,
	    addressLength);
    if (retval < 0) {
	CRASHWITHERROR("SendMessage() failed");
    } else
	return retval;
}

ssize_t
ReceivePacket(int socket_fd, packet* packetBuffer, struct sockaddr_in* senderAddress, unsigned int* addressLength) {
    DEBUGMESSAGE(1, "Receiving packet");
    int retval = recvfrom(socket_fd, packetBuffer, sizeof (packet), MSG_WAITALL, (struct sockaddr*) senderAddress,
	    addressLength);
    if (retval < 0) {
	CRASHWITHERROR("ReceivePacket() failed");
    } else {
	packetBuffer->checksum = ntohs(packetBuffer->checksum); // TODO: behövs av någon anledning, inte hundra på varför
	unsigned short checksum = CalculateChecksum(packetBuffer);
	if (checksum == 65535u)
	    return retval;
	else {
	    DEBUGMESSAGE(2, "ReceivePacket() failed: checksum incorrect\nExpected 65535, got %d (off by %d)\n", checksum, 65535 - checksum);
	    exit(EXIT_FAILURE);
	}
    }
}

// Sets a flag in a packet to the specified value
// Returns 1 if the flag was changed, 0 if the flag remains the same, and -1 on error.

int SetPacketFlag(packet* packet, uint flagToModify, int value) {
    // First, do a bit of sanity checking on values
    if (flagToModify > 255)
	return -1;
    if (value < 0 || value > 1)
	return -1;

    int originalValue = packet->flags & flagToModify;
    if (originalValue == flagToModify * value)
	return 0; // The flag is already set how we want it. Nothing changed, return 0.
    else {
	if (value == 0)
	    packet->flags -= (packet->flags & flagToModify);
	else if (value == 1)
	    packet->flags = packet->flags | flagToModify;
	else
	    return -1; // Something went horribly wrong, value is out of bounds somehow???

	return 1; // The flag was changed, so we're returning 1
    }
}

unsigned short CalculateChecksum(const packet* packet) {
    DEBUGMESSAGE_NONEWLINE(4, "\n-------------------------------------------------------------[CalculateChecksum]\n");
    unsigned int total = 0;
    byte* packetBytes = (byte*) packet;

    unsigned int numBytesInPacket = PACKET_HEADER_LENGTH + packet->dataLength;

    //-------------------------------------------------
    for (int i = 0; i < numBytesInPacket; i++) {
	DEBUGMESSAGE_NONEWLINE(4, "[%d]", packetBytes[i]);
    }
    printf("\n");

    for (int i = PACKET_HEADER_LENGTH; i < numBytesInPacket - 1; i++) {
	DEBUGMESSAGE_NONEWLINE(4, "[%c]", packetBytes[i]);
    }
    printf("\n");
    //-------------------------------------------------

    DEBUGMESSAGE_NONEWLINE(4, "numBytesInPacket:[ %d ]            [   PACKET_HEADER_LENGTH:[ %d ] + packet->dataLength:[ %d ]    ]\n", numBytesInPacket, PACKET_HEADER_LENGTH, packet->dataLength);

    for (int i = 0; i < numBytesInPacket; i += 2) {
	if (i == numBytesInPacket - 1) {
	    total += packetBytes[i] * 256;
	    DEBUGMESSAGE_NONEWLINE(4, "[i == numBytesInPacket-1]:[%d]     total:[ %d ]\n", i, total);
	}
	else {
	    unsigned short shortToAdd = 0;
	    shortToAdd = ((packetBytes[i] * 256) + packetBytes[i + 1]);
	    total += shortToAdd;
	    DEBUGMESSAGE_NONEWLINE(4,"i:[%d]     total:[ %d ]\n", i, total);
	}
    }

    while (total > 65535)
	total -= 65535;
    
    DEBUGMESSAGE_NONEWLINE(4,"---------------------------------------------------Returning total:[ %d ] \n\n", total);
    return total;
}

int WritePacket(packet* packet, uint flags, void* data, byte dataLength, unsigned short sequenceNumber) {
    SetPacketFlag(packet, 0b11111111, 0); // clear all flags
    SetPacketFlag(packet, flags, 1); // ... and set the ones requested

    for (int i = 0; i < dataLength; i++) {
	byte* dataBytes = (byte*) data;
	packet->data[i] = dataBytes[i];
    }
    packet->dataLength = dataLength;

    packet->sequenceNumber = sequenceNumber;
    return 1; // 1 is returned on success
}