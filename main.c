
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
#include <time.h>
#include "intelhex/intelhex.h"


/******************************************************************************
 * LOG defines
 */
#ifdef VERBOSE
#define PREFIX                        "rx63nprog: "
#define ERROR(...)                    fprintf(stderr, PREFIX " error: " __VA_ARGS__)
#define WARNING(...)                  fprintf(stderr, PREFIX " warning: " __VA_ARGS__)
#define LOG(...)                      fprintf(stdout, PREFIX " " __VA_ARGS__)
#define LOG_PERROR(arg)               perror(arg)
#else
#define ERROR(...)
#define WARNING(...) 
#define LOG(...)
#define LOG_PERROR(arg)
#endif

#ifdef DEBUG
#define LOG_DBG(...)                  fprintf(stdout, __VA_ARGS__)
#else
#define LOG_DBG(...)
#endif


/******************************************************************************
 * typedefs
 */
typedef enum {
    COMMAND_INITIAL_TRANSMIT                        = 0x00,
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
    COMMAND_NEW_BIT_RATE_CONFIRMATION               = 0x06,
    COMMAND_PROGRAMMING_ERASURE_STATE_TRANSITION    = 0x40,
    COMMAND_USER_BOOT_AREA_PROGRAMMING_SELECTION    = 0x42,
    COMMAND_USER_DATA_AREA_PROGRAMMING_SELECTION    = 0x43,
    COMMAND_BOOT_PROGRAM_STATUS_INQUIRY             = 0x4f,
    COMMAND_256_BYTE_PROGRAMMING                    = 0x50,
    COMMAND_BIT_RATE_INIT                           = 0x55,
} COMMAND;

typedef enum {
    RESPONSE_INITIAL_TRANSMIT_OK                    = 0x00,
    RESPONSE_GENERIC_OK                             = 0x06,
    RESPONSE_SUPPORTED_DEVICE_INQUIRY_OK            = 0x30,
    RESPONSE_CLOCK_MODE_INQUIRY_OK                  = 0x31,
    RESPONSE_MULTIPLICATION_RATIO_INQUIRY_OK        = 0x32,
    RESPONSE_OPERATING_FREQUENCY_INQUIRY_OK         = 0x33,
    RESPONSE_BOOT_PROGRAM_STATUS_OK                 = 0x5f,
    RESPONSE_DEVICE_SELECTION_ERROR                 = 0x90,
    RESPONSE_NEW_BIT_RATE_SELECTION_ERROR           = 0xbf,
    RESPONSE_256_BYTE_PROGRAMMING_ERROR             = 0xd0,
    RESPONSE_BIT_RATE_INIT_OK                       = 0xe6,
    RESPONSE_BIT_RATE_INIT_ERROR                    = 0xff,
} RESPONSE;

typedef enum {
    PAYLOAD_NONE,              /*No Payload is expected, and no error buffer is given*/
    PAYLOAD_EXPECTED,          /*Payload is expected*/
    PAYLOAD_NONE_WITH_ERR_BUF, /*No Payload is expected, and error buffer is given*/
} PAYLOAD;

/*Device Struct Representation*/
#define SERIESNAME_LEN 48
typedef struct {
    unsigned char code[4];
    char seriesName[SERIESNAME_LEN];
    int seriesNameLength;
} DEVICE;

/*Clock Type Struct Representation*/
typedef struct {
    int numberOfMultiplicationRatios;
    unsigned char *multiplicationRatios;
    unsigned int minimumOperatingFrequency;
    unsigned int maximumOperatingFrequency;
} CLOCK_TYPE;

/*Execution Parameters*/
typedef struct {
    void *command;
    int commandLength;
    void *response;
    int responseCapacity;
    PAYLOAD payload;
    unsigned char expectedReply;
    int isBlocking;
    struct timeval *timeout;
} EXECPARAM;

/******************************************************************************
 * globals
 */
static int g_serialHandle = -1;
struct termios g_serialAttributes;

static int g_deviceListCnt = 0;
static DEVICE *deviceList = NULL;

static int g_clockModeListCnt = 0;
static unsigned char *g_clockModeList = NULL;

int g_clockTypeListCnt = 0;
static CLOCK_TYPE *g_clockTypeList = NULL;


/******************************************************************************
 * helper functions
 */
static int writeData(const void *data, int size)
{
    if(write(g_serialHandle, data, size) != size)
    {
        LOG_PERROR("write: ");
        return -1;
    }

    return 0;
}

static int readData(void *data, int size, int block, struct timeval *t)
{
    static struct timeval timeout_default = { .tv_sec = 0, .tv_usec = 999999 / 2 };
    static fd_set set;

    struct timeval timeout;

    if (t != NULL)
    {
        timeout.tv_sec = t->tv_sec;
        timeout.tv_usec = t->tv_usec;
    } else {
        timeout.tv_sec = timeout_default.tv_sec;
        timeout.tv_usec = timeout_default.tv_usec;
    }

    FD_ZERO(&set);
    FD_SET(g_serialHandle, &set);

    int retVal = select(FD_SETSIZE, &set, 0, 0, block ? NULL : &timeout);
    if(retVal == 0)
    {
        WARNING("select : no available fd\n");
        return -1;
    }

    if(retVal < 0)
    {
        LOG_PERROR("select: ");
        return -1;
    }

    if((size = read(g_serialHandle, data, size)) == -1)
    {
        LOG_PERROR("read: ");
        return -1;
    }

    return size;
}

static unsigned char computeChecksum(const unsigned char *data, int size)
{
    unsigned char checksum = 0;

    if((data == NULL) || (size < 0))
    {
        return 0;
    }

    while(size-- > 0)
    {
        checksum += data[size];
    }

    return (~checksum + 1);
}

static int executeCommand(EXECPARAM p)
{
    if(p.command == NULL || p.commandLength <= 0 || p.response == NULL || p.responseCapacity <= 0)
    {
        ERROR("invalid params\n");
        return -1;
    }

    if(p.payload == PAYLOAD_EXPECTED && p.responseCapacity <= 1)
    {
        ERROR("response buffer insufficient\n");
        return -1;
    }

    LOG_DBG("   COM: %.2x  payload: %d  blocking: %s\n", ((char *)p.command)[0], p.payload, (p.isBlocking == 0 ? "no" : "yes"));
    if(writeData(p.command, p.commandLength) < 0)
    {
        return -1;
    }

    int responseCapacity = p.responseCapacity;
    int responseSize = 0;
    switch(p.payload)
    {
        case PAYLOAD_NONE:
            responseCapacity = 1;
            break;
        case PAYLOAD_NONE_WITH_ERR_BUF:
            if(responseCapacity < 2)
            {
                ERROR("invalid buffer size. provide error buffer\n");
                return -1;
            }
            responseCapacity = 1;
            break;
        case PAYLOAD_EXPECTED:
            /*follow-through*/
        default:
            break;
    }

    while(responseCapacity > 0)
    {
        int size = readData((unsigned char *)p.response + responseSize, responseCapacity, p.isBlocking, p.timeout);
        if(size < 1)
        {
            return -1;
        }
        /*Accommodate the error response*/
        if(p.payload == PAYLOAD_NONE_WITH_ERR_BUF && ((unsigned char *)p.response)[0] != p.expectedReply)
        {
            int tempSize = readData((unsigned char *)p.response + responseSize + 1, responseCapacity, p.isBlocking, p.timeout);
            if(tempSize < 1)
            {
                return -1;
            }
            size += tempSize;
        }

        if(responseSize < 2 && (responseSize + size) > 1)
        {
            responseCapacity = ((unsigned char *)p.response)[1] - (size - (2 - responseSize)) + 1;
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

        LOG_DBG("   RSP:");

        for(i = 0; i < responseSize; i++)
        {
            LOG_DBG(" %.2x", ((unsigned char *)p.response)[i]);
        }

        LOG_DBG("\n");

        if(p.payload == PAYLOAD_EXPECTED)
        {
            if(computeChecksum(p.response, responseSize - 1) != ((unsigned char *)p.response)[responseSize - 1])
            {
                return -1;
            }
        }
    }

    return responseSize;
}

/******************************************************************************
 * matchBitRates()
 * 
 * Initial commands to setup the device.
 * 
 */
static int matchBitRates(void)
{
    unsigned char response[1];
    unsigned char command[1];
    int retries = 30; /*max number of retries is 30*/

    command[0] = COMMAND_INITIAL_TRANSMIT; /*0x00*/

    /*Retry 30 times*/
    while(retries-- > 0)
    {
        LOG_DBG("tries left : %d\n", retries);
        EXECPARAM p = {.command = command, .commandLength = 1, .response = response, .responseCapacity = 1, 
                       .payload = PAYLOAD_NONE, .expectedReply = 0, .isBlocking = 0, .timeout = NULL};
        if(executeCommand(p) > 0)
        {
            if(response[0] != RESPONSE_INITIAL_TRANSMIT_OK)
            {
                return -1;
            }
            break;
        }
    }
    if(retries <= 0)
    {
        return -1;
    }

    LOG("Automatic Adjustment OK\n");

    command[0] = COMMAND_BIT_RATE_INIT; /*0x55*/
    EXECPARAM p = {.command = command, .commandLength = 1, .response = response, .responseCapacity = 1, 
                   .payload = PAYLOAD_NONE, .expectedReply = 0, .isBlocking = 0, .timeout = NULL};
    if(executeCommand(p) > 0)
    {
        if(response[0] != RESPONSE_BIT_RATE_INIT_OK)
        {
            return -1;
        }
        return 0;
    }

    return -1;
}

/******************************************************************************
 * cleanupDeviceList()
 * 
 * Free the device list
 * 
 */
static int cleanupDeviceList(void)
{
    if(deviceList != NULL)
    {
        free(deviceList);
        deviceList = NULL;
    }
    g_deviceListCnt = 0;
    return 0;
}

/******************************************************************************
 * getSupportedDevices()
 * 
 * Obtain all supported devices into the device list
 * 
 */
static int getSupportedDevices(void)
{
    unsigned char response[100];
    unsigned char command[1];
    command[0] = COMMAND_SUPPORTED_DEVICE_INQUIRY; /*0x20*/
    EXECPARAM p = {.command = command, .commandLength = 1, .response = response, .responseCapacity = sizeof(response), 
                   .payload = PAYLOAD_EXPECTED, .expectedReply = 0, .isBlocking = 0, .timeout = NULL};
    int size = executeCommand(p);

    if(size > 0 && response[0] == RESPONSE_SUPPORTED_DEVICE_INQUIRY_OK && size == (response[1] + 3))
    {
        cleanupDeviceList();
        if(response[2] <= 0)
        {
            ERROR("getSupportedDevices(): no devices found\n");
            return -1;
        }
        g_deviceListCnt = response[2];

        deviceList = malloc(g_deviceListCnt * sizeof(DEVICE));
        if(deviceList == NULL)
        {
            g_deviceListCnt = 0;
            return -1;
        }

        int i;
        int j = 3;

        for(i = 0; i < g_deviceListCnt; i++)
        {
            memcpy(deviceList[i].code, &response[j + 1], 4);
            int tempLen = response[j] - 4;
            deviceList[i].seriesNameLength = (tempLen > (SERIESNAME_LEN - 1) ? (SERIESNAME_LEN - 1) : tempLen);
            memcpy(deviceList[i].seriesName, &response[j + 5], deviceList[i].seriesNameLength);
            deviceList[i].seriesName[deviceList[i].seriesNameLength] = '\0';
            j = j + response[j] + 1;
        }

        for(i = 0; i < g_deviceListCnt; i++)
        {
            LOG_DBG("Device %d:", i);

            LOG_DBG(" code: ");
            for(j = 0; j < 4; j++)
            {
                LOG_DBG(" %.2x", deviceList[i].code[j]);
            }

            LOG_DBG(" : %.*s\n", deviceList[i].seriesNameLength, deviceList[i].seriesName);
        }

        return 0;
    }

    cleanupDeviceList();

    return -1;
}

/******************************************************************************
 * setDevice()
 * 
 * Program the user area with the loaded image file.
 * 
 */
static int setDevice(int deviceIndex)
{
    unsigned char command[7];
    unsigned char response[1];
    command[0] = COMMAND_DEVICE_SELECTION; /*0x10*/
    command[1] = 4;

    if(deviceList == NULL)
    {
        ERROR("No retrieved devices. Retrieve devices first.\n");
        return -1;
    }

    /*index check*/
    if(deviceIndex < 0 || deviceIndex >= g_deviceListCnt)
    {
        ERROR("device index is invalid\n");
        return -1;
    }

    memcpy(&command[2], deviceList[deviceIndex].code, 4);
    command[6] = computeChecksum(command, sizeof(command) - 1);

    EXECPARAM p = {.command = command, .commandLength = sizeof(command), .response = response, .responseCapacity = 1, 
                   .payload = PAYLOAD_NONE, .expectedReply = 0, .isBlocking = 0, .timeout = NULL};
    int size = executeCommand(p);

    if(size > 0 && response[0] == RESPONSE_GENERIC_OK)
    {
        return 0;
    }

    return -1;
}

/******************************************************************************
 * cleanupClockModeList()
 * 
 * Clean all allocated clock modes
 * 
 */
static int cleanupClockModeList(void)
{
    if(g_clockModeList != NULL)
    {
        free(g_clockModeList);
        g_clockModeList = NULL;
    }
    g_clockModeListCnt = 0;
    return 0;
}

/******************************************************************************
 * getClockModes()
 * 
 * Obtain all clock modes into the clock mode list
 * 
 */
static int getClockModes(void)
{
    unsigned char response[100];
    unsigned char command[1];
    command[0] = COMMAND_CLOCK_MODE_INQUIRY; /*0x21*/

    EXECPARAM p = {.command = command, .commandLength = sizeof(command), .response = response, .responseCapacity = sizeof(response), 
                   .payload = PAYLOAD_EXPECTED, .expectedReply = 0, .isBlocking = 0, .timeout = NULL};
    int size = executeCommand(p);

    if(size > 0 && response[0] == RESPONSE_CLOCK_MODE_INQUIRY_OK)
    {
        if(response[1] <= 0)
        {
            ERROR("invalid number of clock modes\n");
            return -1;
        }
        g_clockModeListCnt = response[1];
        g_clockModeList = malloc(g_clockModeListCnt * sizeof(unsigned char));
        if(g_clockModeList == NULL)
        {
            ERROR("malloc() fail\n");
            return -1;
        }

        int i;

        for(i = 0; i < g_clockModeListCnt; i++)
        {
            g_clockModeList[i] = response[i + 2];
            LOG_DBG("Clock Mode %d: 0x%.2x\n", i, g_clockModeList[i]);
        }

        return 0;
    }

    cleanupClockModeList();
    return -1;
}

/******************************************************************************
 * setClockMode()
 * 
 * Set the clock mode from the clock mode list.
 * 
 */
static int setClockMode(int clockModeIndex)
{
    unsigned char command[4];
    unsigned char response[1];
    
    if(g_clockModeList == NULL)
    {
        ERROR("clock mode list is not available.\n");
        return -1;
    }

    /*index check*/
    if(clockModeIndex < 0 || clockModeIndex >= g_clockModeListCnt)
    {
        ERROR("clockMode index invalid\n");
        return -1;
    }

    command[0] = COMMAND_CLOCK_MODE_SELECTION; /*0x11*/
    command[1] = 1;
    command[2] = g_clockModeList[clockModeIndex];
    command[3] = computeChecksum(command, sizeof(command) - 1);

    EXECPARAM p = {.command = command, .commandLength = sizeof(command), .response = response, .responseCapacity = sizeof(response), 
                   .payload = PAYLOAD_NONE, .expectedReply = 0, .isBlocking = 0, .timeout = NULL};
    int size = executeCommand(p);

    if(size > 0 && response[0] == RESPONSE_GENERIC_OK)
    {
        return 0;
    }

    return -1;
}

static int cleanupClockTypeList(void)
{
    int i = 0;
    if(g_clockTypeList != NULL)
    {
        for(i = 0; i < g_clockTypeListCnt; i++)
        {
            if(g_clockTypeList[i].multiplicationRatios != NULL)
            {
                free(g_clockTypeList[i].multiplicationRatios);
                g_clockTypeList[i].multiplicationRatios = NULL;
            }
        }
        free(g_clockTypeList);
        g_clockTypeList = NULL;
    }
    g_clockTypeListCnt = 0;
    return 0;
}

/******************************************************************************
 * getMultiplicationRatios()
 * 
 * Get the multiplication ratios for display to the user
 * 
 */
static int getMultiplicationRatios(void)
{
    unsigned char response[100];
    unsigned char command[1];
    command[0] = COMMAND_MULTIPLICATION_RATIO_INQUIRY; /*0x22*/

    EXECPARAM p = {.command = command, .commandLength = sizeof(command), .response = response, .responseCapacity = sizeof(response), 
                   .payload = PAYLOAD_EXPECTED, .expectedReply = 0, .isBlocking = 0, .timeout = NULL};
    int size = executeCommand(p);

    if(size > 0 && response[0] == RESPONSE_MULTIPLICATION_RATIO_INQUIRY_OK)
    {
        if(g_clockTypeListCnt == 0 || g_clockTypeListCnt != response[2])
        {
            if(response[2] <= 0)
            {
                ERROR("g_clockTypeListCnt error : %d\n", response[2]);
                cleanupClockTypeList();
                return -1;
            }
            g_clockTypeListCnt = response[2];
            g_clockTypeList = malloc(g_clockTypeListCnt * sizeof(CLOCK_TYPE));
            if(g_clockTypeList == NULL)
            {
                ERROR("malloc() fail\n");
                cleanupClockTypeList();
                return -1;
            }
        }

        int i = 0;
        int j = 3;
        int k = 0;

        for(i = 0; i < g_clockTypeListCnt; i++)
        {

            g_clockTypeList[i].numberOfMultiplicationRatios = response[j];
            if(g_clockTypeList[i].numberOfMultiplicationRatios <= 0)
            {
                LOG("numberOfMultiplicationRatios : %d\n", g_clockTypeList[i].numberOfMultiplicationRatios);
                cleanupClockTypeList();
                return -1;
            }
            g_clockTypeList[i].multiplicationRatios = malloc(g_clockTypeList[i].numberOfMultiplicationRatios);
            if(g_clockTypeList[i].multiplicationRatios == NULL)
            {
                ERROR("malloc() fail\n");
                cleanupClockTypeList();
                return -1;
            }

            ++j;

            LOG_DBG("Clock Type %d:", i);

            for(k = 0; k < g_clockTypeList[i].numberOfMultiplicationRatios; k++, j++)
            {
                g_clockTypeList[i].multiplicationRatios[k] = response[j];

                LOG_DBG(" %.2x", g_clockTypeList[i].multiplicationRatios[k]);
            }

            LOG_DBG("\n");
        }
    }

    return 0;
}

/******************************************************************************
 * getOperatinFrequencies()
 * 
 * Obtain the operating frequencies for all clocks avaiable, for display to the user
 * We're expecting two clocks, the (1) system and the (2)peripheral clock
 * 
 */
static int getOperatingFrequencies(void)
{
    unsigned char response[100];
    unsigned char command[1];
    command[0] = COMMAND_OPERATING_FREQUENCY_INQUIRY; /*0x23*/

    EXECPARAM p = {.command = command, .commandLength = sizeof(command), .response = response, .responseCapacity = sizeof(response), 
                   .payload = PAYLOAD_EXPECTED, .expectedReply = 0, .isBlocking = 0, .timeout = NULL};
    int size = executeCommand(p);

    if(size > 0 && response[0] == RESPONSE_OPERATING_FREQUENCY_INQUIRY_OK)
    {
        if(g_clockTypeListCnt == 0)
        {
            g_clockTypeListCnt = response[2];
            if(g_clockTypeListCnt <= 0)
            {
                ERROR("g_clockTypeListCnt error : %d\n", g_clockTypeListCnt);
                g_clockTypeListCnt = 0;
                return -1;
            }
            g_clockTypeList = malloc(g_clockTypeListCnt);
            if(g_clockTypeList == NULL)
            {
                ERROR("malloc() fail\n");
                g_clockTypeListCnt = 0;
                return -1;
            }
        }

        int i = 0;
        int j = 3;

        for(i = 0; i < g_clockTypeListCnt; i++)
        {
            g_clockTypeList[i].minimumOperatingFrequency = (response[j + 1] | (response[j] << 8)) * 10000;
            g_clockTypeList[i].maximumOperatingFrequency = (response[j + 3] | (response[j + 2] << 8)) * 10000;
            j += 4;

            LOG_DBG("Clock Type %d: Operating Frequency: %d ~ %d\n", i, g_clockTypeList[i].minimumOperatingFrequency, g_clockTypeList[i].maximumOperatingFrequency);
        }

        return 0;
    }

    return -1;
}


/******************************************************************************
 * convertBitRate()
 * 
 * Helper function to convert the bit rate
 * Return is (speed_t)-1 on failure
 * 
 */
static speed_t convertBitRate(unsigned int bitRate)
{
    /*There may be some portability issues with regards to the bit rate.
      Bit rates from 0 to 150bps will not be recognized -- the specifications
      require the bit rate to be divided by 100*/

    speed_t outSpeed = (speed_t)(-1);
    switch(bitRate)
    {
        case 200:
            outSpeed = B200;
            break;
        case 300:
            outSpeed = B300;
            break;
        case 600:
            outSpeed = B600;
            break;
        case 1200:
            outSpeed = B1200;
            break;
        case 1800:
            outSpeed = B1800;
            break;
        case 2400:
            outSpeed = B2400;
            break;
        case 4800:
            outSpeed = B4800;
            break;
        case 9600:
            outSpeed = B9600;
            break;
        case 19200:
            outSpeed = B19200;
            break;
        case 38400:
            outSpeed = B38400;
            break;
        case 57600:
            outSpeed = B57600;
            break;
        case 115200:
            outSpeed = B115200;
            break;
        case 230400:
            outSpeed = B230400;
            break;
#ifdef B460800
        /*B460800 may not be defined in some systems*/
        case 460800:
            outSpeed = B460800;
            break;
#endif
        default:
            break;
    }
    return outSpeed;
}

/******************************************************************************
 * setBitRate()
 * 
 * Set the bit rate, frequency, and multiplication ratios for the system and
 * peripheral clocks.
 *   bitRate parameter is in bps format
 *   frequency parameter is in Hz format
 *   systemClockMultiplicationRatio and peripheralClockMultiplicationRatio are
 *       one-byte signed inputs:
 *        - a positive value indicates a multiplication ratio
 *        - a negative value indicates a division ratio
 * 
 */
static int setBitRate(unsigned int bitRate, unsigned int frequency, char systemClockMultiplicationRatio, char peripheralClockMultiplicationRatio)
{
    unsigned char command[10];
    unsigned char response[2];
    
    speed_t bitRate_termios = convertBitRate(bitRate);
    if(bitRate_termios == (speed_t)-1)
    {
        ERROR("invalid bit rate\n");
        return -1;
    }

    unsigned int inputBitRate = bitRate / 100; /*1/100 of the new bit rate value should be specified*/
    unsigned int inputFrequency = frequency / 10000; /*This value should be calculated by multiplying the input frequency value to two decimal places by 100.*/

    command[0] = COMMAND_NEW_BIT_RATE_SELECTION; /*0x3f*/
    command[1] = 7; /*2-bytes inputBitRate, 2-bytes input frequency, 1-byte clocktype count, 2-bytes for the multiplication ratios*/
    command[2] = (inputBitRate >> 8) & 0xff;
    command[3] = inputBitRate & 0xff;
    command[4] = (inputFrequency >> 8) & 0xff;
    command[5] = inputFrequency & 0xff;
    command[6] = 2; /*this is always fixed at 2: one for the system clock, and another for the peripheral clock.*/
    command[7] = systemClockMultiplicationRatio & 0xff;
    command[8] = peripheralClockMultiplicationRatio & 0xff;
    command[9] = computeChecksum(command, sizeof(command) - 1);

    EXECPARAM p = {.command = command, .commandLength = sizeof(command), .response = response, .responseCapacity = sizeof(response), 
                   .payload = PAYLOAD_NONE_WITH_ERR_BUF, .expectedReply = RESPONSE_GENERIC_OK, .isBlocking = 0, .timeout = NULL};
    int size = executeCommand(p);
    if(size < 1)
    {
        return -1;
    }

    if(response[0] != RESPONSE_GENERIC_OK)
    {
        switch(response[1])
        {
            case 0x11:
                ERROR("Checksum Error\n");
                break;
            case 0x24:
                ERROR("Bit rate selection Error\n");
                break;
            case 0x25:
                ERROR("Input frequency Error\n");
                break;
            case 0x26:
                ERROR("Multiplication ratio Error\n");
                break;
            case 0x27:
                ERROR("Operating Frequency Error\n");
                break;
            default:
                ERROR("Unknown Frequency Error\n");
                break;
        }
        return -1;
    }

    /*Sleep 25ms*/
    struct timespec wait = { .tv_sec = 0, .tv_nsec = 25000000 };
    nanosleep(&wait, NULL);

    g_serialAttributes.c_ispeed = g_serialAttributes.c_ospeed = bitRate_termios;
    if(tcsetattr(g_serialHandle, TCSANOW, &g_serialAttributes))
    {
        ERROR("Failed to set serial parameters!\n");
        LOG_PERROR("  tcsetattr: ");
        return -1;
    }

    return 0;
}

/******************************************************************************
 * confirmBitRate()
 * 
 * Confirm the bit rate from setBitRate()
 * 
 */
static int confirmBitRate(void)
{
    unsigned char command[1];
    unsigned char response[1];
    
    command[0] = COMMAND_NEW_BIT_RATE_CONFIRMATION; /*0x06*/
    EXECPARAM p = {.command = command, .commandLength = sizeof(command), .response = response, .responseCapacity = sizeof(response), 
                   .payload = PAYLOAD_NONE, .expectedReply = 0, .isBlocking = 0, .timeout = NULL};
    int size = executeCommand(p);

    if(size > 0 && response[0] == RESPONSE_GENERIC_OK)
    {
        return 0;
    }

    return -1;
}

/******************************************************************************
 * activateFlashProgramming()
 * 
 * Transfer the RX63N/RX631 device into a programmable state.
 * NOTE: Do not issue a programming/erasure state transition command before
 *       the device selection, clock mode selection, and new
 *       bit rate selection commands.
 * NOTE: No handling forthe case where ID code protection is enabled
 * 
 */
static int activateFlashProgramming(void)
{
    unsigned char command[1];
    unsigned char response[1];
    /*specifications say that two bytes are needed for an error response, but only one is needed*/
    command[0] = COMMAND_PROGRAMMING_ERASURE_STATE_TRANSITION; /*0x40*/

    struct timeval timeout =  { .tv_sec = 1, .tv_usec = 0 };
    EXECPARAM p = {.command = command, .commandLength = 1, .response = response, .responseCapacity = 1, 
                   .payload = PAYLOAD_NONE, .expectedReply = 0, .isBlocking = 0, .timeout = &timeout};//
    int size = executeCommand(p);

    if(size > 0)
    {
        LOG_DBG("activateFlashProgramming() response = 0x%x\n", response[0]);
        /*ID code protection is enabled*/
        if(response[0] == 0x16)
        {
            /*TODO: send ID code*/
            ERROR("Unsupported: ID code protection is enabled");
            return -1;
        }
        /*ID code protection is disabled*/
        if(response[0] == 0x26)
        {
            return 0;
        }
    }

    return -1;
}

/******************************************************************************
 * programUserArea()
 * 
 * Program the user area with the loaded image file.
 * 
 */
static int programUserArea(const char *imageFile)
{
    unsigned char response[2];
    unsigned char command[262]; /*1 byte cmd + 4 byte addr + 256 byte data + 1 byte checksum*/
    
    /*User/Data Area Programming Selection*/
    command[0] = COMMAND_USER_DATA_AREA_PROGRAMMING_SELECTION; /*0x43*/

    /*Check the imageFile parsing before starting the user area programming*/
    IntelHex image;
    int result = intelHex_hexToBin(imageFile, NULL, NULL, &image, 0);
    if(result != 0)
    {
        ERROR("Failed to open firmware image file!\n");
        return -1;
    }

    EXECPARAM p = {.command = command, .commandLength = 1, .response = response, .responseCapacity = 1, 
                   .payload = PAYLOAD_NONE, .expectedReply = 0, .isBlocking = 0, .timeout = NULL};
    int size = executeCommand(p);
    if(size < 1 || response[0] != RESPONSE_GENERIC_OK)
    {
        ERROR("User/Data Area Programming Selection error!\n");
        return -1;
    }

    LOG("Image OK\n");
    LOG_DBG("Firmware Image Info:\n"
            "  CS: %.8x\n"
            "  EIP: %.8x\n"
            "  IP: %.8x\n",
            image.cs,
            image.eip,
            image.ip);

    IntelHexMemory *memory = NULL;
    IntelHexData *hexData = NULL;
    uint32_t address = 0;
    uint32_t flashSize = 0;
    int dataSize = 0;
    uint8_t *data = NULL;

    int hasError = 0;

    LOG("Programming to device...\n");
    /*256-Byte Programming*/
    command[0] = COMMAND_256_BYTE_PROGRAMMING; /*0x50*/
    for(memory = image.memory; memory != NULL && !hasError; memory = memory->next)
    {
        LOG_DBG("Memory: %.8x ~ %.8x (%d)\n", memory->baseAddress, memory->baseAddress + memory->size - 1, memory->size);

        address = memory->baseAddress;

        for(hexData = memory->head; hexData != NULL && !hasError; hexData = hexData->next)
        {
            data = hexData->data;
            dataSize = hexData->size;

            /*flash per 256 bytes*/
            while(dataSize > 0)
            {
                flashSize = (uint32_t)(address & 0xff);

                if(flashSize > 0)
                {
                    memset(&command[5], 0xff, flashSize);
                    address &= ~0xff;
                }

                command[1] = (address >> 24) & 0xff;
                command[2] = (address >> 16) & 0xff;
                command[3] = (address >> 8) & 0xff;
                command[4] = address & 0xff;

                LOG_DBG("  Data: %.8x ~ %.8x (%d)\n", address, address + 256 - 1, 256);

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

                EXECPARAM pprog = {.command = command, .commandLength = 262, .response = response, .responseCapacity = 2,
                               .payload = PAYLOAD_NONE_WITH_ERR_BUF, .expectedReply = RESPONSE_GENERIC_OK, .isBlocking = 0, .timeout = NULL};
                size = executeCommand(pprog);
                if(size < 0)
                {
                    hasError = 1;
                    break;
                }
                if(response[0] != RESPONSE_GENERIC_OK)
                {
                    switch(response[1])
                    {
                        case 0x11:
                            ERROR("Checksum error\n");
                            break;
                        case 0x2a:
                            ERROR("Address error\n");
                            break;
                        case 0x53:
                            ERROR("Programming cannot be done due to a programming error\n");
                            break;
                        default:
                            ERROR("Unknown error\n");
                            break;
                    }
                    hasError = 1;
                    break;
                }
            }
        }
    }

    /*Terminate Programming*/
    memset(&command[1], 0xff, 4); /*0xff is set to all 4 bytes of the address area*/
    command[5] = computeChecksum(command, 5);
    EXECPARAM pterm = {.command = command, .commandLength = 6, .response = response, .responseCapacity = 1, 
                   .payload = PAYLOAD_NONE, .expectedReply = 0, .isBlocking = 0, .timeout = NULL};
    size = executeCommand(pterm);
    if(size < 0)
    {
        ERROR("error in terminating programming\n");
    }

    intelHex_destroyHexInfo(&image);
    return hasError ? -1 : 0;
}

/******************************************************************************
 * main
 */
int main(int argc, char **argv)
{
    if(argc != 3)
    {
        ERROR("Usage: %s <device> <firmware image>\n", argv[0]);
        return -1;
    }

    LOG_DBG("Device: %s\n", argv[1]);
    LOG_DBG("Firmware: %s\n", argv[2]);
    LOG_DBG("\n");

    if((g_serialHandle = open(argv[1], O_RDWR | O_NOCTTY | O_SYNC)) == -1)
    {
        LOG_PERROR("  open: ");
        return -1;
    }

    if(tcgetattr(g_serialHandle, &g_serialAttributes) != 0)
    {
        LOG_PERROR("  tcgetattr: ");
        return -1;
    }

    g_serialAttributes.c_ispeed = g_serialAttributes.c_ospeed = B9600;
    g_serialAttributes.c_cflag = CS8 | CREAD;

    if(tcsetattr(g_serialHandle, TCSANOW, &g_serialAttributes) != 0)
    {
        LOG_PERROR("  tcsetattr: ");
        return -1;
    }

    if(matchBitRates() < 0)
    {
        ERROR("Failed to match bit rates!\n");
        return -1;
    }

    if(getSupportedDevices() < 0)
    {
        ERROR("Failed to get supported devices!\n");
        return -1;
    }

    if(setDevice(0) < 0)
    {
        ERROR("Failed to set device!\n");
        cleanupDeviceList();
        return -1;
    }

    if(getClockModes() < 0)
    {
        ERROR("Failed to get clock modes!\n");
        cleanupClockModeList();
        cleanupDeviceList();
        return -1;
    }

    if(setClockMode(0) < 0)
    {
        ERROR("Failed to set clock mode!\n");
        cleanupClockModeList();
        cleanupDeviceList();
        return -1;
    }

    if(getMultiplicationRatios() < 0)
    {
        ERROR("Failed to get multiplication ratios!\n");
        cleanupClockModeList();
        cleanupDeviceList();
        return -1;
    }

    if(getOperatingFrequencies() < 0)
    {
        ERROR("Failed to get operating frequencies!\n");
        cleanupClockTypeList();
        cleanupClockModeList();
        cleanupDeviceList();
        return -1;
    }

    if(setBitRate(115200, 12000000, 8, 4) < 0)
    {
        ERROR("Failed to set bit rate!\n");
        cleanupClockTypeList();
        cleanupClockModeList();
        cleanupDeviceList();
        return -1;
    }
    
    if(confirmBitRate() < 0)
    {
        ERROR("Failed to confirm bit rate!\n");
        cleanupClockTypeList();
        cleanupClockModeList();
        cleanupDeviceList();
        return -1;
    }

    if(activateFlashProgramming() < 0)
    {
        ERROR("Failed to activate flash programming!\n");
        cleanupClockTypeList();
        cleanupClockModeList();
        cleanupDeviceList();
        return -1;
    }

    if(programUserArea(argv[2]) < 0)
    {
        ERROR("Failed to program user area!\n");
        cleanupClockTypeList();
        cleanupClockModeList();
        cleanupDeviceList();
        return -1;
    }

    LOG("Finished\n");
    cleanupClockTypeList();
    cleanupClockModeList();
    cleanupDeviceList();
    return 0;
}
