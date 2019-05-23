/* File: receiver.c
 *
 * Author: Joel Risberg,Jan-Ove Leksell
 * 2019-may
 * 
 * Instructions: 
 * Run through a linux terminal from the folder containing the compiled file. 
 * Use the command './sender X' where 'X' is the debug level 
 * Debug Level:( from '0' -[Just about nothing] to '5' -[more] 
 * Specific debug modes are: 
 * Checksum calculation:------------------------------------- 15   
 * Roundtime, average time for sending / ACKing packets:----- 20    
 * Error generator:------------------------------------------ 25   
 * Reading packets from the receiver:------------------------ 30
 * 
 * Description: 
 * Setups a socket, listens for and manage connections to senders. Uses checksums to check for errors, reorganize data when needed etc.
 * Then writes the received data to a text file in the folder "received"
 */

#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <zconf.h>
#include <sys/stat.h>
#include <semaphore.h>

#include "common.h"

#define MIN_ACCEPTED_WINDOW_SIZE 1
#define MAX_ACCEPTED_WINDOW_SIZE 16
#define MIN_ACCEPTED_FRAME_SIZE 10
#define MAX_ACCEPTED_FRAME_SIZE 65535

#define CONNECTION_STATUS_PENDING 1
#define CONNECTION_STATUS_ACTIVE 2

typedef struct bufferedPacketList bufferedPacketList;
struct bufferedPacketList
{
    packet storedData;
    bufferedPacketList* next;
};

typedef struct connection connection;
struct connection
{
    in_addr_t address;
    in_port_t port;
    byte status;
    int id;
    unsigned short sequence;
    bufferedPacketList* packetList;

    connection* next;
};

int socket_fd;
connection* connectionList = NULL;

int AddConnection(struct sockaddr_in* address)
{
    connection* newConnection;
    if ((newConnection = malloc(sizeof(connection))) == NULL)
    {
        DEBUGMESSAGE(0, "AddConnection malloc() failed");
        return -1;
    }

    newConnection->address = address->sin_addr.s_addr;
    newConnection->port = address->sin_port;
    newConnection->status = CONNECTION_STATUS_PENDING;
    newConnection->id = random() % 10000000;
    newConnection->sequence = 0;
    newConnection->packetList = NULL;
    newConnection->next = NULL;

    connection* lastConnection = connectionList;
    if (lastConnection == NULL)
        connectionList = newConnection;
    else
    {
        while (lastConnection->next != NULL)
            lastConnection = lastConnection->next;
        lastConnection->next = newConnection;
    }

    return 1;
}

connection* FindConnection(const struct sockaddr_in* socketAddress)
{
    connection* lastConnection = connectionList;
    if (lastConnection == NULL)
        return NULL;

    while (lastConnection != NULL)
    {
        if (lastConnection->address == socketAddress->sin_addr.s_addr &&
            lastConnection->port == socketAddress->sin_port)
            return lastConnection;
        lastConnection = lastConnection->next;
    }
    return NULL;
}

int RemoveConnectionByID(int id)
{
    connection* lastConnection = connectionList;
    if (lastConnection == NULL)
        return 0;

    while (lastConnection->next != NULL)
    {
        if (lastConnection->next->id == id)
        {
            connection* nextInLine = lastConnection->next->next;
            memset(lastConnection->next, 0, sizeof(connection));
            free(lastConnection->next);
            lastConnection->next = nextInLine;
            return 1;
        }
    }
    return 0;
}

FILE* OpenConnectionFile(connection* clientConnection)
{
    FILE* file;
    char* fileName;
    fileName = malloc(50);
    mkdir("received", 0777);
    sprintf(fileName, "./received/%d", clientConnection->id);
    if ((file = fopen(fileName, "a")) == NULL)
    {
        CRASHWITHERROR("Couldn't open file to write");
    }
    free(fileName);
    return file;
}

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

int StoreBufferedData(connection* clientConnection, const packet* packetToStore)
{
    bufferedPacketList* newListItem;
    if ((newListItem = malloc(sizeof(bufferedPacketList))) == NULL)
    {
        DEBUGMESSAGE(0, "StoreBufferedData malloc() failed");
        return -1;
    }

    CopyPacket(packetToStore, &(newListItem->storedData));
    newListItem->next = NULL;

    bufferedPacketList* listPointer = clientConnection->packetList;
    if (listPointer == NULL)
    {
        clientConnection->packetList = newListItem;
        return 1;
    }
    else
    {
        int listPointerSequence = listPointer->storedData.sequenceNumber;
        if (packetToStore->sequenceNumber < listPointerSequence)
        {
            newListItem->next = clientConnection->packetList;
            clientConnection->packetList = newListItem;
            return 1;
        }

        while (listPointer->next != NULL)
        {
            listPointerSequence = listPointer->next->storedData.sequenceNumber;
            if (packetToStore->sequenceNumber < listPointerSequence)
            {
                newListItem->next = listPointer->next;
                listPointer->next = newListItem;
                return 1;
            }
            listPointer = listPointer->next;
        }
        listPointer->next = newListItem;
        return 1;
    }
}

bufferedPacketList* RetrieveBufferedData(connection* clientConnection)
{
    if (clientConnection->packetList != NULL)
    {
        int firstBufferedSequence = clientConnection->packetList->storedData.sequenceNumber;
        if (clientConnection->sequence == firstBufferedSequence)
        {
            bufferedPacketList* packetList = clientConnection->packetList;
            clientConnection->packetList = clientConnection->packetList->next;
            return packetList;
        }
    }
    return NULL;
}

int CheckBufferedDataForSequence(connection* clientConnection, unsigned short sequence)
{
    bufferedPacketList* bufferCursor = clientConnection->packetList;
    while (bufferCursor != NULL)
    {
        if (bufferCursor->storedData.sequenceNumber == sequence)
            return 1;
        else
            bufferCursor = bufferCursor->next;
    }
    return 0;
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
        DEBUGMESSAGE(3, "SYN: Flags "
                GRN
                "OK"
                RESET);
        byte requestedWindowSize = packetBuffer.data[0];
        byte suggestedWindowSize;

        unsigned short requestedFrameSize = ntohs((packetBuffer.data[1] * 256) + packetBuffer.data[2]);
        unsigned short suggestedFrameSize;
        if (requestedWindowSize >= MIN_ACCEPTED_WINDOW_SIZE && requestedWindowSize <= MAX_ACCEPTED_WINDOW_SIZE)
        {
            DEBUGMESSAGE(3, "SYN: Requested window size "
                    GRN
                    "OK"
                    RESET);
            suggestedWindowSize = requestedWindowSize;
        }
        else if (requestedWindowSize < MIN_ACCEPTED_WINDOW_SIZE)
        {
            DEBUGMESSAGE(3, "SYN: Requested window size "
                    BLU
                    "NEGOTIABLE"
                    RESET);
            suggestedWindowSize = MIN_ACCEPTED_WINDOW_SIZE;
        }
        else if (requestedWindowSize > MAX_ACCEPTED_WINDOW_SIZE)
        {
            DEBUGMESSAGE(3, "SYN: Requested window size "
                    BLU
                    "NEGOTIABLE"
                    RESET);
            suggestedWindowSize = MAX_ACCEPTED_WINDOW_SIZE;
        }
        else
        {
            DEBUGMESSAGE(2, "SYN: Requested window size "
                    RED
                    "NOT OK"
                    RESET);
            return -1;
        }

        if (requestedFrameSize >= MIN_ACCEPTED_FRAME_SIZE && requestedFrameSize <= MAX_ACCEPTED_FRAME_SIZE)
        {
            DEBUGMESSAGE(3, "SYN: Requested frame size "
                    GRN
                    "OK"
                    RESET);
            suggestedFrameSize = requestedFrameSize;
        }
        else if (requestedFrameSize < MIN_ACCEPTED_FRAME_SIZE)
        {
            DEBUGMESSAGE(3, "SYN: Requested frame size "
                    BLU
                    "NEGOTIABLE"
                    RESET);
            suggestedFrameSize = MIN_ACCEPTED_FRAME_SIZE;
        }
        else if (requestedFrameSize > MAX_ACCEPTED_FRAME_SIZE)
        {
            DEBUGMESSAGE(3, "SYN: Requested frame size "
                    BLU
                    "NEGOTIABLE"
                    RESET);
            suggestedFrameSize = MAX_ACCEPTED_FRAME_SIZE;
        }
        else
        {
            DEBUGMESSAGE(2, "SYN: Requested frame size "
                    RED
                    "NOT OK"
                    RESET);
            return -1;
        }

        packetData[0] = suggestedWindowSize;
        byte* suggestedFrameSizeBytes = (byte*) &suggestedFrameSize;
        packetData[1] = suggestedFrameSizeBytes[0];
        packetData[2] = suggestedFrameSizeBytes[1];

        if (requestedWindowSize == suggestedWindowSize && requestedFrameSize == suggestedFrameSize)
        {
            WritePacket(&packetToSend, PACKETFLAG_SYN | PACKETFLAG_ACK,
                        packetData, sizeof(packetData), packetBuffer.sequenceNumber);
            SendPacket(socket_fd, &packetToSend, &senderAddress, senderAddressLength);
            AddConnection(&senderAddress);
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
        DEBUGMESSAGE(2, "SYN: Flags "
                RED
                "NOT OK"
                RESET);
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
        int retval = 0;
        retval = ReceivePacket(socket_fd, &packetBuffer, &senderAddress, &senderAddressLength);
        if (retval > 0)
        {
            if (packetBuffer.flags == 0)
            {
                connection* clientConnection = FindConnection(&senderAddress);
                FILE* file;

                if (packetBuffer.sequenceNumber == clientConnection->sequence)
                {
                    file = OpenConnectionFile(clientConnection);
                    fprintf(file, "%s", packetBuffer.data);
                    clientConnection->sequence++;

                    bufferedPacketList* retrievedPacketList;
                    if (clientConnection->packetList != NULL)
                    {
                        DEBUGMESSAGE_EXACT(DEBUGLEVEL_REORDER, "Retrieving buffered data, seq at %d\n", clientConnection->sequence);
                        if (debugLevel == DEBUGLEVEL_REORDER)
                        {
                            printf(YELTEXT("Current packet storage: "));
                            retrievedPacketList = clientConnection->packetList;
                            while (retrievedPacketList != NULL)
                            {
                                printf(YELTEXT("%d "), retrievedPacketList->storedData.sequenceNumber);
                                retrievedPacketList = retrievedPacketList->next;
                            }
                            printf("\n");
                        }
                        retrievedPacketList = RetrieveBufferedData(clientConnection);
                        while (retrievedPacketList != NULL)
                        {
                            DEBUGMESSAGE_EXACT(DEBUGLEVEL_REORDER, "Retrieved packet at sequence %d\n", retrievedPacketList->storedData.sequenceNumber);
                            fprintf(file, "%s", retrievedPacketList->storedData.data);
                            clientConnection->sequence++;
                            free(retrievedPacketList);
                            retrievedPacketList = RetrieveBufferedData(clientConnection);
                        }
                    }
                    fclose(file);
                }
                else if (packetBuffer.sequenceNumber > clientConnection->sequence)
                {
                    if (CheckBufferedDataForSequence(clientConnection, packetBuffer.sequenceNumber))
                    {
                        DEBUGMESSAGE_EXACT(DEBUGLEVEL_REORDER, "Packet with seq %d already in buffer\n", packetBuffer.sequenceNumber);
                    }
                    else
                    {
                        DEBUGMESSAGE_EXACT(DEBUGLEVEL_REORDER, "Storing packet with sequence %d\n", packetBuffer.sequenceNumber);
                        StoreBufferedData(clientConnection, &packetBuffer);
                    }
                }
                else
                {
                    DEBUGMESSAGE(1, YELTEXT("WARNING: ")"Received packet with sequence number %d but looking for %d or greater",
                            packetBuffer.sequenceNumber, clientConnection->sequence);
                }
                packet packetToSend;
                memset(&packetToSend, 0, sizeof(packet));
                WritePacket(&packetToSend, PACKETFLAG_ACK, NULL, 0,
                            packetBuffer.sequenceNumber);
                SendPacket(socket_fd, &packetToSend, &senderAddress, senderAddressLength);
            }
            else if (packetBuffer.flags == PACKETFLAG_FIN)
            { // Oh lordy, kill it with fire
                connection* clientConnection = FindConnection(&senderAddress);
                FILE* file;
                file = OpenConnectionFile(clientConnection);
                fprintf(file, "%s\n", packetBuffer.data);
                fclose(file);
                DEBUGMESSAGE(1, "FINished writing to file %d", clientConnection->id);

                packet packetToSend;
                memset(&packetToSend, 0, sizeof(packet));
                WritePacket(&packetToSend, PACKETFLAG_ACK, NULL, 0,
                            packetBuffer.sequenceNumber);
                SendPacket(socket_fd, &packetToSend, &senderAddress, senderAddressLength);
            }
                //--------------------------------------------------------------------------JANNE LEKTE HÄR--------------------------------------------
            else if (packetBuffer.flags == PACKETFLAG_SYN)
            {
                ReceiveConnection(&packetBuffer, senderAddress, senderAddressLength);
            }
            else if (packetBuffer.flags == PACKETFLAG_ACK)
            {
                connection* clientConnection = FindConnection(&senderAddress);
                if (clientConnection->status == CONNECTION_STATUS_PENDING)
                {
                    clientConnection->status = CONNECTION_STATUS_ACTIVE;
                }
            }
            if (packetBuffer.sequenceNumber == 10E25)
                break; // compiler whines about endless loops without this bit
        }
    }
}

int main(int argc, char* argv[])
{
    srandom(time(NULL));
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