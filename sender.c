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

    printf("Socket setup successfully.\n");

    char* packetBuffer;
    if ((packetBuffer = malloc(PACKET_BUFFER_SIZE)) == NULL)
    {
        CRASHWITHMESSAGE("packetBuffer malloc failed");
    }
    strcpy(packetBuffer, "jonas");

    struct sockaddr_in receiverAddress;
    receiverAddress.sin_family = AF_INET;
    receiverAddress.sin_port = htons(LISTENING_PORT);
    receiverAddress.sin_addr.s_addr = INADDR_ANY;

    unsigned int addressLength = sizeof(receiverAddress);

    printf("Sending message: %s\n", packetBuffer);
    SendMessage(socket_fd, packetBuffer, strlen(packetBuffer), &receiverAddress, addressLength);
    printf("Waiting for return message.\n");
    ReceiveMessage(socket_fd, packetBuffer, &receiverAddress, &addressLength);
    printf("Message received: %s\n", packetBuffer);
    sleep(1);

    return 0;
}