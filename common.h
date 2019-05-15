//
// Created by quadragoon on 2019-05-06.
//

#ifndef DVA218_LAB3B_COMMON_H
#define DVA218_LAB3B_COMMON_H

#include <stdio.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>

#define CRASHWITHERROR(message) perror(message);exit(EXIT_FAILURE)
#define CRASHWITHMESSAGE(message) printf("%s\n", message);exit(EXIT_FAILURE)

#define DEBUGLEVEL 2
#define DEBUGMESSAGE(level, ...) if (DEBUGLEVEL >= level){printf(__VA_ARGS__); printf("\n");}

#define LISTENING_PORT 23456
#define PACKET_BUFFER_SIZE 2048
#define PACKET_HEADER_LENGTH 6
#define byte unsigned char

#define PACKETFLAG_SYN 1u
#define PACKETFLAG_ACK 2u
#define PACKETFLAG_NAK 4u
#define PACKETFLAG_FIN 8u

struct packet
{
    byte flags;
    byte dataLength;
    unsigned short sequenceNumber;
    unsigned short checksum;
    byte data[255];
};

typedef struct packet packet;
int InitializeSocket();
ssize_t SendMessage(int socket_fd, const char* dataBuffer, int length, const struct sockaddr_in* receiverAddress, unsigned int addressLength);
ssize_t ReceiveMessage(int socket_fd, char* packetBuffer, struct sockaddr_in* senderAddress, unsigned int* addressLength);

ssize_t SendPacket(int socket_fd, packet* packetToSend, const struct sockaddr_in* receiverAddress, unsigned int addressLength);
ssize_t ReceivePacket(int socket_fd, packet* packetBuffer, struct sockaddr_in* senderAddress, unsigned int* addressLength);

int SetPacketFlag(packet* packet, uint flag, int value);
unsigned short CalculateChecksum(const packet* packet);

#endif //DVA218_LAB3B_COMMON_H
