//
// Created by quadragoon on 2019-05-06.
//

#ifndef DVA218_LAB3B_COMMON_H
#define DVA218_LAB3B_COMMON_H

#define CRASHWITHMESSAGE(message) perror(message);exit(EXIT_FAILURE)
#define LISTENING_PORT 23456
#define DATA_BUFFER_SIZE 2048

int InitializeSocket();
ssize_t ReceiveMessage(int socket_fd, char* dataBuffer, struct sockaddr_in* senderAddress, unsigned int* addressLength);
ssize_t SendMessage(int socket_fd, const char* dataBuffer, int length, const struct sockaddr_in* senderAddress, unsigned int addressLength);

#endif //DVA218_LAB3B_COMMON_H
