//
// Created by quadragoon on 2019-05-06.
//

#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>

#define CRASHWITHMESSAGE(message) perror(message);exit(EXIT_FAILURE)
#define LISTENING_PORT 23456
#define PACKET_BUFFER_SIZE 2048

typedef struct packet
{
    char flags;
    unsigned short sequenceNumber;
    char dataLength;
    char data[256];
    char checksum;
} packet;

int InitializeSocket()
{
    int socket_fd = 0;
    if ((socket_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        CRASHWITHMESSAGE("socket() failed");
    }
    else
        return socket_fd;
}

ssize_t ReceiveMessage(int socket_fd, char* packetBuffer, struct sockaddr_in* senderAddress, unsigned int* addressLength)
{
    int retval = recvfrom(socket_fd, packetBuffer, PACKET_BUFFER_SIZE, MSG_WAITALL, (struct sockaddr*)senderAddress, addressLength);
    if (retval < 0)
    {
        CRASHWITHMESSAGE("SendMessage() failed");
    }
    else
        return retval;
}

ssize_t SendMessage(int socket_fd, const char* dataBuffer, int length, const struct sockaddr_in* senderAddress, unsigned int addressLength)
{
    int retval = sendto(socket_fd, dataBuffer, length, MSG_CONFIRM, (struct sockaddr*)senderAddress, addressLength);
    if (retval < 0)
    {
        CRASHWITHMESSAGE("SendMessage() failed");
    }
    else
        return retval;
}
