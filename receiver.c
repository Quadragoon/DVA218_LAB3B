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

#define MIN_ACCEPTED_WINDOW_SIZE 1
#define MAX_ACCEPTED_WINDOW_SIZE 1

int socket_fd;

int main()
{
    socket_fd = InitializeSocket();

    const int sockoptval = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEPORT, &sockoptval, sizeof(sockoptval)) < 0)
    {
        CRASHWITHERROR("setsockopt() failed");
    }

    struct sockaddr_in socketAddress;
    socketAddress.sin_family = AF_INET;
    socketAddress.sin_port = htons(LISTENING_PORT);
    socketAddress.sin_addr.s_addr = INADDR_ANY;
    if (bind(socket_fd, (struct sockaddr*)&socketAddress, sizeof(socketAddress)) < 0)
    {
        CRASHWITHERROR("bind() failed");
    }

    DEBUGMESSAGE(1, "Socket setup and bound successfully.");

    unsigned short sequence = 0;

    struct sockaddr_in receiverAddress, senderAddress;
    memset(&receiverAddress, 0, sizeof(struct sockaddr_in));
    memset(&senderAddress, 0, sizeof(struct sockaddr_in));
    unsigned int receiverAddressLength = sizeof(receiverAddress);
    unsigned int senderAddressLength = sizeof(senderAddress);

    packet packetToSend, packetBuffer;
    memset(&packetToSend, 0, sizeof(packet));
    memset(&packetBuffer, 0, sizeof(packet));

    ReceivePacket(socket_fd, &packetBuffer, &senderAddress, &senderAddressLength);
    if (packetBuffer.flags & PACKETFLAG_SYN)
    {
        DEBUGMESSAGE(3, "SYN: Flags "GRN"OK"RESET);
        byte requestedWindowSize = packetBuffer.data[0];
        if (requestedWindowSize >= MIN_ACCEPTED_WINDOW_SIZE && requestedWindowSize <= MAX_ACCEPTED_WINDOW_SIZE)
        {
            DEBUGMESSAGE(3, "SYN: Data "GRN"OK"RESET);
            WritePacket(&packetToSend, PACKETFLAG_SYN | PACKETFLAG_ACK, &requestedWindowSize, sizeof(byte), packetBuffer.sequenceNumber);
            SendPacket(socket_fd, &packetToSend, &senderAddress, senderAddressLength);
        }
        /*else if (requestedWindowSize < MIN_ACCEPTED_WINDOW_SIZE)
        {
            // TODO: window size negotiation, other than just acceptance
        }
        else if (requestedWindowSize > MAX_ACCEPTED_WINDOW_SIZE)
        {
            // TODO: window size negotiation, other than just acceptance
        }*/
        else
        {
            DEBUGMESSAGE(2, "SYN: Data "RED"NOT OK"RESET);
        }
    }
    else
    {
        DEBUGMESSAGE(2, "SYN: Flags "RED"NOT OK"RESET);
    }

    return 0;
}