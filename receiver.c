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

    printf("Socket setup and bound successfully.\n");

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
    printf("Packet received\n");
    if (packetBuffer.flags & PACKETFLAG_SYN)
    {
        printf("Packet is SYN\n");
        if (strcmp(packetBuffer.data, "JONAS") == 0)
        {
            printf("Data checks out\n");
            SetPacketFlag(&packetToSend, PACKETFLAG_SYN | PACKETFLAG_ACK, 1);
            strcpy(packetToSend.data, "BONSLY");
            packetToSend.dataLength = sizeof("BONSLY");
            packetToSend.sequenceNumber = packetBuffer.sequenceNumber;
            SendPacket(socket_fd, &packetToSend, &senderAddress, senderAddressLength);
        }
    }

    return 0;
}