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

int socket_fd;
unsigned short sequence = 0;
unsigned short awaitedAcknowledgements[255];

int main()
{
    socket_fd = InitializeSocket();

    DEBUGMESSAGE(1, "Socket setup successfully.");

    struct sockaddr_in receiverAddress, senderAddress;
    memset(&receiverAddress, 0, sizeof(struct sockaddr_in));
    memset(&senderAddress, 0, sizeof(struct sockaddr_in));

    receiverAddress.sin_family = AF_INET;
    receiverAddress.sin_port = htons(LISTENING_PORT);
    receiverAddress.sin_addr.s_addr = INADDR_ANY;

    unsigned int receiverAddressLength = sizeof(receiverAddress);
    unsigned int senderAddressLength = sizeof(senderAddress);

    packet packetToSend, packetBuffer;
    sequence = 23456;
    packetToSend.sequenceNumber = sequence;
    if (SetPacketFlag(&packetToSend, PACKETFLAG_SYN, 1) < 0)
    {
        CRASHWITHMESSAGE("Setting SYN flag failed");
    }

    strcpy(packetToSend.data, "JONAS");
    packetToSend.dataLength = sizeof("JONAS");

    SendPacket(socket_fd, &packetToSend, &receiverAddress, receiverAddressLength);
    ReceivePacket(socket_fd, &packetBuffer, &senderAddress, &senderAddressLength);

    if (packetBuffer.flags & PACKETFLAG_SYN && packetBuffer.flags & PACKETFLAG_ACK)
    {
        DEBUGMESSAGE(3, "Flags OK");
        if (packetBuffer.sequenceNumber == (sequence))
        {
            DEBUGMESSAGE(3, "Sequence OK");
            if (strcmp(packetBuffer.data, "BONSLY") == 0)
            {
                DEBUGMESSAGE(2, "Data OK.\nSeems to work, which is weird");
            }
        }
        else
        {
            DEBUGMESSAGE(2, "Sequence NOT OK");
        }

    }
    else
    {
        DEBUGMESSAGE(2, "Flags NOT OK");
    }

    return 0;
}