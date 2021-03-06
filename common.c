/* File: common.c
 *
 * Author: Joel Risberg,Jan-Ove Leksell
 * 2019-may
 *
 *
 * Description:
 * Common functions and library values used in the sender and receiver programs.
 */

#include "common.h"

#define CRASHWITHERROR(message) perror(message);exit(EXIT_FAILURE)
#define LISTENING_PORT 23456

int debugLevel = 0;
int loss = 0;
int corrupt = 0;

int InitializeSocket()
{
    int socket_fd = 0;
    if ((socket_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        CRASHWITHERROR("socket() failed");
    }
    else
        return socket_fd;
}

/*ssize_t SendMessage(int socket_fd, const char* dataBuffer, int length, const struct sockaddr_in* receiverAddress,
                    unsigned int addressLength)
{
    int retval = sendto(socket_fd, dataBuffer, length, MSG_CONFIRM, (struct sockaddr*) receiverAddress, addressLength);
    if (retval < 0)
    {
        CRASHWITHERROR("SendMessage() failed");
    }
    else
        return retval;
}*/

/*ssize_t
ReceiveMessage(int socket_fd, char* packetBuffer, struct sockaddr_in* senderAddress, unsigned int* addressLength)
{
    int retval = recvfrom(socket_fd, packetBuffer, DATA_BUFFER_SIZE, MSG_WAITALL, (struct sockaddr*) senderAddress,
                          addressLength);
    if (retval < 0)
    {
        CRASHWITHERROR("SendMessage() failed");
    }
    else
        return retval;
}*/

ssize_t
SendPacket(int socket_fd, packet* packetToSend, const struct sockaddr_in* receiverAddress, unsigned int addressLength)
{
    packetToSend->checksum = 0;
    packetToSend->checksum = (CalculateChecksum(packetToSend) ^ 65535u);

    int packetLength = PACKET_HEADER_LENGTH + packetToSend->dataLength;

    // Run the packet through the Error Generator before sending it (or losing it)
    if (ErrorGenerator(packetToSend) != 0)
    {
        int retval = sendto(socket_fd, packetToSend, packetLength, MSG_CONFIRM, (struct sockaddr*) receiverAddress,
                            addressLength);
        if (retval < 0)
        {
            CRASHWITHERROR("SendPacket() failed");
        }
        else
            return retval;
    }
    else
    {
        return packetLength;
    }
}

ssize_t
ReceivePacket(int socket_fd, packet* packetBuffer, struct sockaddr_in* senderAddress, unsigned int* addressLength)
{
    int retval = recvfrom(socket_fd, packetBuffer, sizeof(packet), MSG_WAITALL, (struct sockaddr*) senderAddress,
                          addressLength);
    if (retval < 0)
    {
        DEBUGMESSAGE(0, "recvfrom() in ReceivePacket() failed");
        return -1;
    }
    else
    {
        packetBuffer->checksum = ntohs(
                packetBuffer->checksum);
        unsigned short checksum = CalculateChecksum(packetBuffer);
        if (checksum == 65535u)
            return retval;
        else
        {
            DEBUGMESSAGE(2, "ReceivePacket() failed: checksum incorrect\nExpected 65535, got %d (off by %d)\n",
                         checksum, 65535 - checksum);
            return -1;
        }
    }
}

// Sets a flag in a packet to the specified value
// Returns 1 if the flag was changed, 0 if the flag remains the same, and -1 on error.

int SetPacketFlag(packet* packet, uint flagToModify, int value)
{
    // First, do a bit of sanity checking on values
    if (flagToModify > 255)
        return -1;
    if (value < 0 || value > 1)
        return -1;

    int originalValue = packet->flags & flagToModify;
    if (originalValue == flagToModify * value)
        return 0; // The flag is already set how we want it. Nothing changed, return 0.
    else
    {
        if (value == 0)
            packet->flags -= (packet->flags & flagToModify);
        else if (value == 1)
            packet->flags = packet->flags | flagToModify;
        else
            return -1; // Something went horribly wrong, value is out of bounds somehow???

        return 1; // The flag was changed, so we're returning 1
    }
}

unsigned short CalculateChecksum(const packet* packet)
{
    DEBUGMESSAGE_EXACT(DEBUGLEVEL_CHECKSUM, MAG
            "\n-------------------------------------------------------------["
            RESET
            "CalculateChecksum"
            MAG
            "]\n"
            RESET);
    unsigned int total = 0;
    byte* packetBytes = (byte*) packet;

    unsigned int numBytesInPacket = PACKET_HEADER_LENGTH + packet->dataLength;

    if (debugLevel == DEBUGLEVEL_CHECKSUM)
        PrintPacketData(packet);

    DEBUGMESSAGE_EXACT(DEBUGLEVEL_CHECKSUM, GRN
            "numBytesInPacket:["
            RESET
            " %d "
            GRN
            "]            [   PACKET_HEADER_LENGTH:["
            RESET
            " %d "
            GRN
            "] + packet->dataLength:["
            RESET
            " %d "
            GRN
            "]    ]\n"
            RESET, numBytesInPacket, PACKET_HEADER_LENGTH, packet->dataLength);

    for (int i = 0; i < numBytesInPacket; i += 2)
    {
        if (i == numBytesInPacket - 1)
        {
            total += packetBytes[i] * 256;
            DEBUGMESSAGE_EXACT(DEBUGLEVEL_CHECKSUM, MAG
                    "[i == numBytesInPacket-1]:["
                    RESET
                    "%d"
                    MAG
                    "]     total:["
                    RESET
                    " %d "
                    MAG
                    "]\n"
                    RESET, i, total);
        }
        else
        {
            unsigned short shortToAdd = 0;
            shortToAdd = ((packetBytes[i] * 256) + packetBytes[i + 1]);
            total += shortToAdd;
            DEBUGMESSAGE_EXACT(DEBUGLEVEL_CHECKSUM, CYN"i:["RESET"%d"CYN"]     total:["RESET" %d "CYN"]\n"RESET, i, total);
        }
    }

    while (total > 65535)
        total -= 65535;

    DEBUGMESSAGE_EXACT(DEBUGLEVEL_CHECKSUM, MAG
            "---------------------------------------------------Returning total:["
            RESET
            " %d "
            MAG
            "] \n\n"
            RESET, total);
    return total;
}

int WritePacket(packet* packet, uint flags, void* data, unsigned short dataLength, unsigned short sequenceNumber)
{
    SetPacketFlag(packet, 0b11111111, 0); // clear all flags
    SetPacketFlag(packet, flags, 1); // ... and set the ones requested

    for (int i = 0; i < dataLength; i++)
    {
        byte* dataBytes = (byte*) data;
        packet->data[i] = dataBytes[i];
    }
    packet->dataLength = dataLength;

    packet->sequenceNumber = sequenceNumber;
    packet->nothing = packet->nothing;
    return 1; // 1 is returned on success
}

int ErrorGenerator(packet* packet)
{
    byte* packetBytes = (byte*) packet;
    unsigned int numBytesInPacket = PACKET_HEADER_LENGTH + packet->dataLength;

    DEBUGMESSAGE_EXACT(DEBUGLEVEL_ERRORGENERATOR, RED
            "\n-----------------------------------------------------["
            RESET
            "Welcome to the ErrorGenerator V0.1"
            RED
            "]\n"
            RESET);
    //-------------------------------------------------
    DEBUGMESSAGE_EXACT(DEBUGLEVEL_ERRORGENERATOR, YEL
            "[ Unaltered Packet ]"
            RESET"\n");
    if (debugLevel == DEBUGLEVEL_ERRORGENERATOR)
        PrintPacketData(packet);
    //-------------------------------------------------

    // Randomize the chance for a packet to be lost
    if ((random() % 100) < loss)
    {
        DEBUGMESSAGE_EXACT(DEBUGLEVEL_ERRORGENERATOR, RED
                "[! Packet LoSt !]\n"
                RESET);
        DEBUGMESSAGE_EXACT(DEBUGLEVEL_ERRORGENERATOR, RED
                "----------------------------------------------------------------------------------------- \n\n"
                RESET);
        return 0;
    }
    else
    {
        // Randomize the chance for a packet to be corrupted
        if ((random() % 100) < corrupt)
        {
            DEBUGMESSAGE_EXACT(DEBUGLEVEL_ERRORGENERATOR, RED
                    "[! Packet CorRUptEd !]\n"
                    RESET);
            int BytesToCorrupt = 1 + (random() % (numBytesInPacket - 1));
            for (int i = 0; i < BytesToCorrupt; i++)
            {
                packetBytes[random() % (numBytesInPacket - 1)] = (random() % 255);
            }
            DEBUGMESSAGE(DEBUGLEVEL_ERRORGENERATOR, RED
                    "[ Altered Packet ]"
                    RESET);
            if (debugLevel == DEBUGLEVEL_ERRORGENERATOR)
                PrintPacketData(packet);
        }
        DEBUGMESSAGE_EXACT(DEBUGLEVEL_ERRORGENERATOR, RED
                "----------------------------------------------------------------------------------------- \n\n"
                RESET);
        return 1;
    }
}

int PrintPacketData(const packet* packet)
{
    // Outputs debug messages that print out the entire packet
    byte* packetBytes = (byte*) packet;
    unsigned int numBytesInPacket = PACKET_HEADER_LENGTH + packet->dataLength;

    printf(YELTEXT("Packet: "));
    for (int i = 0; i < numBytesInPacket; i++)
    {
        printf(YEL"["RESET"%d"YEL"]"RESET, packetBytes[i]);
    }

    printf("\n");

    printf(GRN"Data: "RESET);
    for (int i = PACKET_HEADER_LENGTH; i < numBytesInPacket; i++)
    {
        printf(GRN"["RESET"%c"GRN"]"RESET, packetBytes[i]);
    }

    printf("\n");

    return 1;
}