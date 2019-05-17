//
// Created by quadragoon on 2019-05-03.
//

#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <zconf.h>
#include <arpa/inet.h>

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

int socket_fd;
unsigned short sequence = 0;

struct sockaddr_in receiverAddress;
struct sockaddr_in senderAddress;

int NegotiateConnection(const char* receiverIP, const byte* desiredWindowSize)
{
    memset(&receiverAddress, 0, sizeof(struct sockaddr_in));
    memset(&senderAddress, 0, sizeof(struct sockaddr_in));

    receiverAddress.sin_family = AF_INET;
    receiverAddress.sin_port = htons(LISTENING_PORT);

    int retval = inet_pton(receiverAddress.sin_family, receiverIP, &(receiverAddress.sin_addr));
    if (retval == -1)
    {
        CRASHWITHERROR("NegotiateConnection() failed to get IP address");
    }
    else if (retval == 0)
    {
        DEBUGMESSAGE(0, YEL"Invalid IP address entered."RESET);
    }

    unsigned int receiverAddressLength = sizeof(receiverAddress);
    unsigned int senderAddressLength = sizeof(senderAddress);

    packet packetToSend, packetBuffer;
    packetToSend.sequenceNumber = sequence;
    if (SetPacketFlag(&packetToSend, PACKETFLAG_SYN, 1) < 0)
    {
        CRASHWITHMESSAGE("Setting SYN flag failed");
    }

    strcpy(packetToSend.data, desiredWindowSize);
    packetToSend.dataLength = sizeof(*desiredWindowSize);

    SendPacket(socket_fd, &packetToSend, &receiverAddress, receiverAddressLength);
    ReceivePacket(socket_fd, &packetBuffer, &senderAddress, &senderAddressLength);

    if (packetBuffer.flags & PACKETFLAG_SYN && packetBuffer.flags & PACKETFLAG_ACK)
    {
        DEBUGMESSAGE(3, "SYN+ACK: Flags "GRN"OK"RESET);
        if (packetBuffer.sequenceNumber == (sequence))
        {
            DEBUGMESSAGE(3, "SYN+ACK: Sequence number "GRN"OK"RESET);
            if (packetBuffer.data[0] == desiredWindowSize)
            {
                DEBUGMESSAGE(2, "SYN+ACK: Data "GRN"OK."RESET);
            }
            else
            {
                DEBUGMESSAGE(2, "SYN+ACK: Data "RED"NOT OK."RESET);
            }
        }
        else
        {
            DEBUGMESSAGE(2, "SYN+ACK: Sequence number "RED"NOT OK"RESET);
        }
    }
    else
    {
        DEBUGMESSAGE(2, "SYN+ACK: Flags "RED"NOT OK"RESET);
    }

    return 0;
}

int main()
{
    socket_fd = InitializeSocket();

    DEBUGMESSAGE(1, "Socket setup successfully.");

    byte desiredWindowSize = 1;
    NegotiateConnection("127.0.0.1", &desiredWindowSize);

    return 0;
}