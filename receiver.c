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

#define MIN_ACCEPTED_WINDOW_SIZE 1
#define MAX_ACCEPTED_WINDOW_SIZE 16
#define MIN_ACCEPTED_FRAME_SIZE 100
#define MAX_ACCEPTED_FRAME_SIZE 65535

int socket_fd;

int CopyPacket(const packet* src, packet* dest)
{
    dest->flags = src->flags;
    dest->sequenceNumber = src->sequenceNumber;
    dest->dataLength = src->dataLength;
    dest->checksum = src->checksum;
    for (int i = 0; i < src->dataLength; i++)
        dest->data[i] = src->data[i];
    return 1; // should return something else on fail but, uh, checking for fail in a simple function like this seems weird to do
}

int ReceiveConnection(const packet* connectionRequestPacket, struct sockaddr_in senderAddress,
                      unsigned int senderAddressLength)
{
    packet packetBuffer, packetToSend;
    memset(&packetToSend, 0, sizeof(packet));
    memset(&packetBuffer, 0, sizeof(packet));

    CopyPacket(connectionRequestPacket, &packetBuffer);

    if (packetBuffer.flags & PACKETFLAG_SYN)
    {
        byte packetData[3];
        DEBUGMESSAGE(3, "SYN: Flags "GRN"OK"RESET);
        byte requestedWindowSize = packetBuffer.data[0];
        byte suggestedWindowSize;

        unsigned short requestedFrameSize = ntohs((packetBuffer.data[1] * 256) + packetBuffer.data[2]);
        unsigned short suggestedFrameSize;
        if (requestedWindowSize >= MIN_ACCEPTED_WINDOW_SIZE && requestedWindowSize <= MAX_ACCEPTED_WINDOW_SIZE)
        {
            DEBUGMESSAGE(3, "SYN: Requested window size "GRN"OK"RESET);
            suggestedWindowSize = requestedWindowSize;
        }
        else if (requestedWindowSize < MIN_ACCEPTED_WINDOW_SIZE)
        {
            DEBUGMESSAGE(3, "SYN: Requested window size "BLU"NEGOTIABLE"RESET);
            suggestedWindowSize = MIN_ACCEPTED_WINDOW_SIZE;
        }
        else if (requestedWindowSize > MAX_ACCEPTED_WINDOW_SIZE)
        {
            DEBUGMESSAGE(3, "SYN: Requested window size "BLU"NEGOTIABLE"RESET);
            suggestedWindowSize = MAX_ACCEPTED_WINDOW_SIZE;
        }
        else
        {
            DEBUGMESSAGE(2, "SYN: Requested window size "RED"NOT OK"RESET);
            return -1;
        }

        if (requestedFrameSize >= MIN_ACCEPTED_FRAME_SIZE && requestedFrameSize <= MAX_ACCEPTED_FRAME_SIZE)
        {
            DEBUGMESSAGE(3, "SYN: Requested frame size "GRN"OK"RESET);
            suggestedFrameSize = requestedFrameSize;
        }
        else if (requestedFrameSize < MIN_ACCEPTED_FRAME_SIZE)
        {
            DEBUGMESSAGE(3, "SYN: Requested frame size "BLU"NEGOTIABLE"RESET);
            suggestedFrameSize = MIN_ACCEPTED_FRAME_SIZE;
        }
        else if (requestedFrameSize > MAX_ACCEPTED_FRAME_SIZE)
        {
            DEBUGMESSAGE(3, "SYN: Requested frame size "BLU"NEGOTIABLE"RESET);
            suggestedFrameSize = MAX_ACCEPTED_FRAME_SIZE;
        }
        else
        {
            DEBUGMESSAGE(2, "SYN: Requested frame size "RED"NOT OK"RESET);
            return -1;
        }

        packetData[0] = suggestedWindowSize;
        byte* suggestedFrameSizeBytes = (byte*)&suggestedFrameSize;
        packetData[1] = suggestedFrameSizeBytes[0];
        packetData[2] = suggestedFrameSizeBytes[1];

        if (requestedWindowSize == suggestedWindowSize && requestedFrameSize == suggestedFrameSize)
        {
            WritePacket(&packetToSend, PACKETFLAG_SYN | PACKETFLAG_ACK,
                        packetData, sizeof(packetData), packetBuffer.sequenceNumber);
            SendPacket(socket_fd, &packetToSend, &senderAddress, senderAddressLength);
        }
        else
        {
            WritePacket(&packetToSend, PACKETFLAG_SYN | PACKETFLAG_NAK,
                        packetData, sizeof(packetData), packetBuffer.sequenceNumber);
            SendPacket(socket_fd, &packetToSend, &senderAddress, senderAddressLength);
        }

    }
    else
    {
        DEBUGMESSAGE(2, "SYN: Flags "RED"NOT OK"RESET);
    }

    return 0;
}

void ReadIncomingMessages()
{
    struct sockaddr_in senderAddress;
    memset(&senderAddress, 0, sizeof(struct sockaddr_in));
    unsigned int senderAddressLength = sizeof(senderAddress);

    packet packetBuffer;
    memset(&packetBuffer, 0, sizeof(packet));

    while (1)
    {
        ReceivePacket(socket_fd, &packetBuffer, &senderAddress, &senderAddressLength);
        //--------------------------------------------------------------------------JANNE LEKER HÄR--------------------------------------------
        if (packetBuffer.flags == 0)
        { // Vad använder vi för flagga till datapaket?
            char Filler[10] = "I hear ya";
            printf("%s", packetBuffer.data);

            packet packetToSend;
            memset(&packetToSend, 0, sizeof(packet));
	    usleep(5000);
            WritePacket(&packetToSend, PACKETFLAG_ACK, (void*) Filler, 0,
                        packetBuffer.sequenceNumber); //------What data to send with the ACK? any?
            SendPacket(socket_fd, &packetToSend, &senderAddress, senderAddressLength);
        }
        else if (packetBuffer.flags == PACKETFLAG_FIN)
        { // Oh lordy, kill it with fire
            char Filler[10] = "thank u";
            printf("%s\n", packetBuffer.data);

            packet packetToSend;
            memset(&packetToSend, 0, sizeof(packet));
	    usleep(5000);
            WritePacket(&packetToSend, PACKETFLAG_ACK, (void*) Filler, 0,
                        packetBuffer.sequenceNumber); //------What data to send with the ACK? any?
            SendPacket(socket_fd, &packetToSend, &senderAddress, senderAddressLength);
        }
            //--------------------------------------------------------------------------JANNE LEKTE HÄR--------------------------------------------
        else if (packetBuffer.flags & PACKETFLAG_SYN)
        {
            ReceiveConnection(&packetBuffer, senderAddress, senderAddressLength);
        }
        if (packetBuffer.sequenceNumber == 10E25)
            break; // compiler whines about endless loops without this bit

    }
}

int main(int argc, char* argv[])
{
    if (argc == 2)
    {
        debugLevel = strtol(argv[1], NULL, 10);
    }

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
    if (bind(socket_fd, (struct sockaddr*) &socketAddress, sizeof(socketAddress)) < 0)
    {
        CRASHWITHERROR("bind() failed");
    }

    DEBUGMESSAGE(1, "Socket setup and bound successfully.");

    ReadIncomingMessages();

    return 0;
}