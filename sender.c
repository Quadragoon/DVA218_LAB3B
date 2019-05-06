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

    char* dataBuffer;
    if ((dataBuffer = malloc(DATA_BUFFER_SIZE)) == NULL)
    {
        CRASHWITHMESSAGE("dataBuffer malloc failed");
    }
    strcpy(dataBuffer, "jonas");

    struct sockaddr_in receiverAddress;
    receiverAddress.sin_family = AF_INET;
    receiverAddress.sin_port = htons(LISTENING_PORT);
    receiverAddress.sin_addr.s_addr = INADDR_ANY;

    unsigned int addressLength = sizeof(receiverAddress);

    printf("Sending message: %s\n", dataBuffer);
    SendMessage(socket_fd, dataBuffer, strlen(dataBuffer), &receiverAddress, addressLength);
    printf("Waiting for return message.\n");
    ReceiveMessage(socket_fd, dataBuffer, &receiverAddress, &addressLength);
    printf("Message received: %s\n", dataBuffer);
    sleep(1);

    return 0;
}