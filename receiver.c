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
        CRASHWITHMESSAGE("setsockopt() failed");
    }

    struct sockaddr_in socketAddress;
    socketAddress.sin_family = AF_INET;
    socketAddress.sin_port = htons(LISTENING_PORT);
    socketAddress.sin_addr.s_addr = INADDR_ANY;
    if (bind(socket_fd, (struct sockaddr*)&socketAddress, sizeof(socketAddress)) < 0)
    {
        CRASHWITHMESSAGE("bind() failed");
    }

    printf("Socket setup and bound successfully.\n");

    char* packetBuffer;
    if ((packetBuffer = malloc(PACKET_BUFFER_SIZE)) == NULL)
    {
        CRASHWITHMESSAGE("memoryBuffer malloc failed");
    }

    struct sockaddr_in senderAddress;
    memset(&senderAddress, 0, sizeof(senderAddress));
    unsigned int addressLength = sizeof(senderAddress);
    int bytesReceived = 0;

    bytesReceived = ReceiveMessage(socket_fd, packetBuffer, &senderAddress, &addressLength);
    printf("%d bytes received: %s\n", bytesReceived, packetBuffer);
    strcpy(packetBuffer, "bOnsly");
    printf("Sending message: %s\n", packetBuffer);
    SendMessage(socket_fd, packetBuffer, strlen(packetBuffer), &senderAddress, addressLength);
    sleep(1);

    return 0;
}