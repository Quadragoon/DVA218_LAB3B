//
// Created by quadragoon on 2019-05-06.
//

// DEBUG LEVEL TABLE! If you use debug levels, put 'em here.
// General levels:
// 0: Messages the user wants to see every time (invalid parameter input etc.)
// 1-3: Messages that display more and more information about what's going on and going wrong
// EXACT levels:
// 15: Checksum calculation
// 20: roundTime calculation
// 25: Error generator

#ifndef DVA218_LAB3B_COMMON_H
#define DVA218_LAB3B_COMMON_H

#include <stdio.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

//-------------------------- A bit of color plz
#define RED   "\x1B[31m"
#define GRN   "\x1B[32m"
#define YEL   "\x1B[33m"
#define BLU   "\x1B[34m"
#define MAG   "\x1B[35m"
#define CYN   "\x1B[36m"
//#define WHT   "\x1B[37m"
#define RESET "\x1B[0m"
//----------------------------------------------
//-------------------------- A bit of defines plz
#define REDTEXT(text) RED text RESET
#define GRNTEXT(text) GRN text RESET
#define YELTEXT(text) YEL text RESET
#define BLUTEXT(text) BLU text RESET
#define MAGTEXT(text) MAG text RESET
#define CYNTEXT(text) CYN text RESET
//----------------------------------------------

#define CRASHWITHERROR(message) perror(message);exit(EXIT_FAILURE)
#define CRASHWITHMESSAGE(message) printf("%s\n", message);exit(EXIT_FAILURE)

extern int debugLevel;
#define DEBUGMESSAGE(level, ...) if (debugLevel >= level){printf(__VA_ARGS__); printf("\n");} (1==1)
#define DEBUGMESSAGE_NONEWLINE(level, ...) if (debugLevel >= level){printf(__VA_ARGS__);} (1==1)
#define DEBUGMESSAGE_EXACT(level, ...) if (debugLevel == level){printf(__VA_ARGS__);} (1==1)

#define DEBUGLEVEL_CHECKSUM 15
#define DEBUGLEVEL_ROUNDTIME 20
#define DEBUGLEVEL_ERRORGENERATOR 25
#define DEBUGLEVEL_READPACKETS 30
#define DEBUGLEVEL_REORDER 35

#define LISTENING_PORT 23456
#define DATA_BUFFER_SIZE 65535
#define PACKET_HEADER_LENGTH 8
#define byte unsigned char

#define PACKETFLAG_SYN 1u
#define PACKETFLAG_ACK 2u
#define PACKETFLAG_NAK 4u
#define PACKETFLAG_FIN 8u

#define ACK_TABLE_SIZE 2000

//#define PACKET_LOSS 0
//#define PACKET_CORRUPT 0
extern int loss;
extern int corrupt;


typedef struct packet packet; 
struct packet
{
    byte flags;   // Flags that details what a packet contains, such as data, FIN, SYN, ACK etc.
    byte nothing; // Only here in order to make the packet a even eight bytes long, otherwise the dataLength displays erratic behaviours
    unsigned short dataLength;      // Length of the packets data
    unsigned short sequenceNumber;  // Used to keep track of packet order so we can avoid jumbled data
    unsigned short checksum;        // Stores the calculated "internet checksum" used for detecting corrupted packets
    byte data[DATA_BUFFER_SIZE];    // The actual data being sent
};

typedef struct ACKmngr ACKmngr;
struct ACKmngr
{
    int Missing;                 // Keeps track of how many ACKs are still unaccounted for
    int Table[ACK_TABLE_SIZE];   // Stores a '1' or '0' for each currently active ACK, '0' indicating that we are missing a ACK for this packet.
};

typedef struct timeoutHandlerData timeoutHandlerData;
struct timeoutHandlerData
{
    packet* dataBufferArray;
    ACKmngr* ACKsPointer;
    byte flags;
    int sequenceNumber;
    int bufferSlot;
};

typedef struct roundTimeHandler roundTimeHandler; // Used as an array in Sender.c in order to keep track of how long it takes to receive ACKs on sent packets
struct roundTimeHandler
{
    struct timeval timeStampStart;  // stores a time value that is set when a packet is sent or re-sent
    struct timeval timeStampEnd;    // stores a time value that is set when a packets ACK is received.
    unsigned int sequence;          // keeps track of what packet / ACK this time data is attached to
};

int InitializeSocket();
//ssize_t SendMessage(int socket_fd, const char* dataBuffer, int length, const struct sockaddr_in* receiverAddress, unsigned int addressLength);
//ssize_t ReceiveMessage(int socket_fd, char* packetBuffer, struct sockaddr_in* senderAddress, unsigned int* addressLength);

ssize_t SendPacket(int socket_fd, packet* packetToSend, const struct sockaddr_in* receiverAddress, unsigned int addressLength);
ssize_t ReceivePacket(int socket_fd, packet* packetBuffer, struct sockaddr_in* senderAddress, unsigned int* addressLength);

int SetPacketFlag(packet* packet, uint flagToModify, int value);
unsigned short CalculateChecksum(const packet* packet);
int WritePacket(packet* packet, uint flags, void* data, unsigned short dataLength, unsigned short sequenceNumber);

int ErrorGenerator(packet* packet);
int PrintPacketData(const packet* packet);

#endif //DVA218_LAB3B_COMMON_H
