
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <fcntl.h>
#include <termios.h>
#include <stdbool.h>
#include "intelhex/intelhex.h"

static int serialHandle;
struct termios serialAttributes;

static int writeData(const void *data, int size)
{
    if(write(serialHandle, data, size) != size)
    {
        perror("write: ");
        return -1;
    }

    return 0;
}

static int readData(void *data, int size, bool block)
{
    static struct timeval timeout = { .tv_sec = 0, .tv_usec = 999999 / 2 };
    static fd_set set;

    FD_ZERO(&set);
    FD_SET(serialHandle, &set);

    if(select(FD_SETSIZE, &set, 0, 0, block ? 0 : &timeout) != 1)
    {
        return -1;
    }

    if((size = read(serialHandle, data, size)) == -1)
    {
        perror("read: ");
        return -1;
    }

    return size;
}

static unsigned char computeChecksum(const unsigned char *data, int size)
{
    unsigned char checksum = 0;

    while(size-- > 0)
    {
        checksum += data[size];
    }

    return (~checksum + 1);
}

static int executeCommand(const void *command, int commandLength, void *response, int responseCapacity, bool responseHasPayload)
{
    if(writeData(command, commandLength) < 0)
    {
        return -1;
    }

    int responseSize = 0;
    int size;

    if(!responseHasPayload)
    {
        responseCapacity = 1;
    }

    while(responseCapacity > 0)
    {
        size = readData((unsigned char *)response + responseSize, responseCapacity, false);

        if(size < 1)
        {
            return -1;
            break;
        }

        if(responseSize < 2 && (responseSize + size) > 1)
        {
            responseCapacity = ((unsigned char *)response)[1] - (size - (2 - responseSize)) + 1;
        }
        else
        {
            responseCapacity -= size;
        }

        responseSize += size;
    }

    if(responseSize > 0)
    {
        int i;

        printf("RSP:");

        for(i = 0; i < responseSize; i++)
        {
            printf(" %.2x", ((unsigned char *)response)[i]);
        }

        printf("\n");

        if(responseHasPayload)
        {
            if(computeChecksum(response, responseSize - 1) != ((unsigned char *)response)[responseSize - 1])
            {
                return -1;
            }
        }
    }

    return responseSize;
}

static bool matchBitRates(void)
{
    unsigned char receiveData[10];
    unsigned char transmitData = 0x00;

    while(1)
    {
        if(executeCommand(&transmitData, 1, receiveData, 10, false) > 0)
        {
            if(transmitData == 0x00)
            {
                if(receiveData[0] != 0x00)
                {
                    return -1;
                }

                transmitData = 0x55;
            }
            else
            {
                return (receiveData[0] == 0xe6);
            }
        }
    }

    return false;
}

typedef enum {
    COMMAND_SUPPORTED_DEVICE_INQUIRY                = 0x20,
    COMMAND_DEVICE_SELECTION                        = 0x10,
    COMMAND_CLOCK_MODE_INQUIRY                      = 0x21,
    COMMAND_CLOCK_MODE_SELECTION                    = 0x11,
    COMMAND_MULTIPLICATION_RATIO_INQUIRY            = 0x22,
    COMMAND_OPERATING_FREQUENCY_INQUIRY             = 0x23,
    COMMAND_USER_BOOT_AREA_INFORMATION_INQUIRY      = 0x24,
    COMMAND_USER_AREA_INFORMATION_INQUIRY           = 0x25,
    COMMAND_BLOCK_INFORMATION_INQUIRY               = 0x26,
    COMMAND_PROGRAMMING_SIZE_INQUIRY                = 0x27,
    COMMAND_DATA_AREA_INQUIRY                       = 0x2a,
    COMMAND_DATA_AREA_INFORMATION_INQUIRY           = 0x2b,
    COMMAND_NEW_BIT_RATE_SELECTION                  = 0x3f,
    COMMAND_PROGRAMMING_ERASURE_STATE_TRANSITION    = 0x40,
    COMMAND_BOOT_PROGRAM_STATUS_INQUIRY             = 0x4f,
} COMMAND;

static int numberOfDevices = 0;
typedef struct {
    unsigned char code[4];
    char *seriesName;
    int seriesNameLength;
} DEVICE;
static DEVICE *devices = NULL;

static int numberOfClockModes = 0;
static unsigned char *clockModes = NULL;

int numberOfClockTypes = 0;
typedef struct {
    int numberOfMultiplicationRatios;
    unsigned char *multiplicationRatios;
    unsigned int minimumOperatingFrequency;
    unsigned int maximumOperatingFrequency;
} CLOCK_TYPE;
static CLOCK_TYPE *clockTypes = NULL;


static bool getSupportedDevices(void)
{
    unsigned char response[100];
    int size = executeCommand("\x20", 1, response, sizeof(response), true);

    if(size > 0 && response[0] == 0x30 && size == (response[1] + 3))
    {
        numberOfDevices = response[2];
        devices = malloc(numberOfDevices * sizeof(DEVICE));

        int i;
        int j = 3;

        for(i = 0; i < numberOfDevices; i++)
        {
            memcpy(devices[i].code, &response[j + 1], 4);
            devices[i].seriesNameLength = response[j] - 4;
            devices[i].seriesName = malloc(devices[i].seriesNameLength + 1);
            memcpy(devices[i].seriesName, &response[j + 5], devices[i].seriesNameLength);
            devices[i].seriesName[devices[i].seriesNameLength] = '\0';
            j = j + response[j] + 1;
        }

        for(i = 0; i < numberOfDevices; i++)
        {
            printf("Device %d:", i);

            for(j = 0; j < 4; j++)
            {
                printf(" %.2x", devices[i].code[j]);
            }

            printf(": %.*s\n", devices[i].seriesNameLength, devices[i].seriesName);
        }

        return true;
    }

    return false;
}

static bool setDevice(int device)
{
    unsigned char command[7];
    unsigned char response[1];
    command[0] = 0x10;
    command[1] = 4;
    memcpy(&command[2], devices[device].code, 4);
    command[6] = computeChecksum(command, sizeof(command) - 1);

    int size = executeCommand(command, sizeof(command), response, sizeof(response), false);

    if(size > 0 && response[0] == 0x06)
    {
        return true;
    }

    return false;
}

static bool getClockModes(void)
{
    unsigned char response[100];
    int size = executeCommand("\x21", 1, response, sizeof(response), true);

    if(size > 0 && response[0] == 0x31)
    {
        numberOfClockModes = response[1];
        clockModes = malloc(numberOfClockModes);

        int i;

        for(i = 0; i < numberOfClockModes; i++)
        {
            clockModes[i] = response[i + 2];

            printf("Clock Mode %d: 0x%.2x\n", i, clockModes[i]);
        }

        return true;
    }

    return false;
}

static bool setClockMode(int clockMode)
{
    unsigned char command[4];
    unsigned char response[1];
    command[0] = 0x11;
    command[1] = 1;
    command[2] = clockModes[clockMode];
    command[3] = computeChecksum(command, sizeof(command) - 1);

    int size = executeCommand(command, sizeof(command), response, sizeof(response), false);

    if(size > 0 && response[0] == 0x06)
    {
        return true;
    }

    return false;
}

static bool getMultiplicationRatios(void)
{
    unsigned char response[100];
    int size = executeCommand("\x22", 1, response, sizeof(response), true);

    if(size > 0 && response[0] == 0x32)
    {
        if(numberOfClockTypes == 0)
        {
            numberOfClockTypes = response[2];
            clockTypes = malloc(numberOfClockTypes);
        }

        int i;
        int j = 3;
        int k;

        for(i = 0; i < numberOfClockTypes; i++)
        {

            clockTypes[i].numberOfMultiplicationRatios = response[j];
            clockTypes[i].multiplicationRatios = malloc(clockTypes[i].numberOfMultiplicationRatios);

            ++j;

            printf("Clock Type %d:", i);

            for(k = 0; k < clockTypes[i].numberOfMultiplicationRatios; k++, j++)
            {
                clockTypes[i].multiplicationRatios[k] = response[j];

                printf(" %.2x", clockTypes[i].multiplicationRatios[k]);
            }

            printf("\n");
        }

        return true;
    }

    return false;
}

static bool getOperatingFrequencies(void)
{
    unsigned char response[100];
    int size = executeCommand("\x23", 1, response, sizeof(response), true);

    if(size > 0 && response[0] == 0x33)
    {
        if(numberOfClockTypes == 0)
        {
            numberOfClockTypes = response[2];
            clockTypes = malloc(numberOfClockTypes);
        }

        int i;
        int j = 3;

        for(i = 0; i < numberOfClockTypes; i++)
        {
            clockTypes[i].minimumOperatingFrequency = (response[j + 1] | (response[j] << 8)) * 10000;
            clockTypes[i].maximumOperatingFrequency = (response[j + 3] | (response[j + 2] << 8)) * 10000;
            j += 4;

            printf("Clock Type %d: Operating Frequency: %d ~ %d\n", i, clockTypes[i].minimumOperatingFrequency, clockTypes[i].maximumOperatingFrequency);
        }

        return true;
    }

    return false;
}

// TODO...

static bool setBitRate(unsigned int bitRate, unsigned int inputFrequency, int systemClockMultiplicationRatio, int peripheralClockMultiplicationRatio)
{
    unsigned char command[10];
    unsigned char response[2];
    command[0] = 0x3f;
    command[1] = 7;
    bitRate /= 100;
    command[2] = (bitRate >> 8) & 0xff;
    command[3] = bitRate & 0xff;
    inputFrequency /= 10000;
    command[4] = (inputFrequency >> 8) & 0xff;
    command[5] = inputFrequency & 0xff;
    command[6] = 2;
    command[7] = systemClockMultiplicationRatio & 0xff;
    command[8] = peripheralClockMultiplicationRatio & 0xff;
    command[9] = computeChecksum(command, sizeof(command) - 1);

    int size = executeCommand(command, sizeof(command), response, sizeof(response), false);

    if(size < 1 || response[0] != 0x06)
    {
        return false;
    }

    struct timespec wait = { .tv_sec = 0, .tv_nsec = 25000000 };
    nanosleep(&wait, NULL);

    serialAttributes.c_ispeed = serialAttributes.c_ospeed = bitRate * 100;

    if(tcsetattr(serialHandle, TCSANOW, &serialAttributes))
    {
        printf("Failed to set serial parameters!\n");
        perror("  tcsetattr: ");
        return false;
    }

    size = executeCommand("\x06", 1, response, 1, false);

    if(size > 0 && response[0] == 0x06)
    {
        return true;
    }

    return false;
}

static bool activateFlashProgramming(void)
{
    unsigned char response[100];
    int size = executeCommand("\x40", 1, response, sizeof(response), false);

    if(size > 0)
    {
        if(response[0] == 0x16)
        {
            // send ID code
            // TODO!!!
            return false;
        }
        else if(response[0] == 0x26)
        {
            return true;
        }
    }

    return false;
}

static bool programUserArea(const char *imageFile)
{
    unsigned char response[100];
    int size = executeCommand("\x43", 1, response, sizeof(response), false);
    unsigned char command[300];
    command[0] = 0x50;

    if(size < 1 || response[0] != 0x06)
    {
        return true;
    }

    IntelHex image;

    int result = intelHex_hexToBin(imageFile, NULL, NULL, &image, 0);

    if(result != 0)
    {
        printf("Failed to open firmware image file!\n");
        return -1;
    }

    printf("Firmware Image Info:\n"
            "  CS: %.8x\n"
            "  EIP: %.8x\n"
            "  IP: %.8x\n",
            image.cs,
            image.eip,
            image.ip);

    IntelHexMemory *memory;
    IntelHexData *hexData;
    uint32_t address;
    int flashSize;
    int dataSize;
    uint8_t *data;


    for(memory = image.memory; memory != NULL; memory = memory->next)
    {
        printf("Memory: %.8x ~ %.8x (%d)\n", memory->baseAddress, memory->baseAddress + memory->size - 1, memory->size);

        address = memory->baseAddress;

        for(hexData = memory->head; hexData != NULL; hexData = hexData->next)
        {
            data = hexData->data;
            dataSize = hexData->size;

            while(dataSize > 0)
            {
                flashSize = address & 0xff;

                if(flashSize > 0)
                {
                    memset(&command[5], 0xff, flashSize);
                    address &= ~0xff;
                }

                command[1] = (address >> 24) & 0xff;
                command[2] = (address >> 16) & 0xff;
                command[3] = (address >> 8) & 0xff;
                command[4] = address & 0xff;

                printf("  Data: %.8x ~ %.8x (%d)\n", address, address + 256 - 1, 256);


                if(dataSize > (256 - flashSize))
                {
                    memcpy(&command[5 + flashSize], data, 256 - flashSize);
                    data += (256 - flashSize);
                    dataSize -= (256 - flashSize);
                    flashSize = 256;
                    address += 256;
                }
                else
                {
                    memcpy(&command[5 + flashSize], data, dataSize);
                    data += dataSize;
                    flashSize += dataSize;
                    address += flashSize;
                    memset(&command[5 + flashSize], 0xff, 256 - flashSize);
                    dataSize = 0;
                    flashSize = 256;
                }






                command[5 + flashSize] = computeChecksum(command, 5 + flashSize);

                size = executeCommand(command, 6 + flashSize, response, 1, false);
            }
        }
    }

    memset(&command[1], 0xff, 4);
    command[5] = computeChecksum(command, 5);
    size = executeCommand(command, 6, response, 1, false);


    intelHex_destroyHexInfo(&image);

    return true;
}

int main(int argc, char **argv)
{
    if(argc != 3)
    {
        printf("Usage: %s <device> <firmware image>\n", argv[0]);
        return -1;
    }
    else if(memcmp("billystheman", argv[0] + strlen(argv[0]) - 12, 12) != 0)
    {
        printf("Nice try. Please accept that really \"billystheman\".\n");
        return -1;
    }

    printf("Device: %s\n", argv[1]);
    printf("Firmware: %s\n", argv[2]);
    printf("\n");

    if((serialHandle = open(argv[1], O_RDWR | O_NOCTTY | O_SYNC)) == -1)
    {
        printf("Failed to open device!\n");
        perror("  open: ");
        return -1;
    }

    if(tcgetattr(serialHandle, &serialAttributes))
    {
        printf("Failed to retrieve current serial parameters!\n");
        perror("  tcgetattr: ");
        return -1;
    }

    serialAttributes.c_ispeed = serialAttributes.c_ospeed = B9600;
    serialAttributes.c_cflag = CS8 | CREAD;

    if(tcsetattr(serialHandle, TCSANOW, &serialAttributes))
    {
        printf("Failed to set serial parameters!\n");
        perror("  tcsetattr: ");
        return -1;
    }

    if(!matchBitRates())
    {
        printf("Failed to match bit rates!\n");
        return -1;
    }

    if(!getSupportedDevices())
    {
        printf("Failed to get supported devices!\n");
        return -1;
    }

    if(!setDevice(0))
    {
        printf("Failed to set device!\n");
        return -1;
    }

    if(!getClockModes())
    {
        printf("Failed to get clock modes!\n");
        return -1;
    }

    if(!setClockMode(0))
    {
        printf("Failed to set clock mode!\n");
        return -1;
    }

    if(!getMultiplicationRatios())
    {
        printf("Failed to get multiplication ratios!\n");
        return -1;
    }

    if(!getOperatingFrequencies())
    {
        printf("Failed to get operating frequencies!\n");
        return -1;
    }

    if(!setBitRate(B115200, 12000000, 8, 4))
    {
        printf("Failed to set bit rate!\n");
        return -1;
    }

    if(!activateFlashProgramming())
    {
        printf("Failed to activate flash programming!\n");
        return -1;
    }

    if(!programUserArea(argv[2]))
    {
        printf("Failed to program user area!\n");
        return -1;
    }

    return 0;
}
