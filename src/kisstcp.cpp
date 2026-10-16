/*
 KISS over TCP: an APRS program on the network (APRSdroid, Xastir, PinPoint, Dire Wolf...) connects to this
 port and gets every frame heard on RF, as KISS. Off by default.

 - One client at a time; a new connection replaces the old one.
 - RX only unless "Allow TX" is on: frames the client sends are counted and thrown away, so nobody on the
   network can make the station transmit. With TX allowed, its data frames are handed to taskAPRS, which sends
   them on RF and counts them in "Packet TX (radio)". KISS parameter commands (TXDELAY, TXTAIL...) are ignored:
   the station's own settings apply. There is no password on this port: allow TX only on a trusted network.
*/
#include <WiFi.h>
#include <KISS.h>
#include <AX25.h>
#include "main.h"
#include "kisstcp.h"

extern Configuration config;
extern statusType status;

#define KISS_TCP_QUEUE 8    // frames waiting to be sent to the client
#define KISS_TCP_MAXLEN 330 // longest AX.25 frame kept

typedef struct
{
    uint16_t len;
    uint8_t data[KISS_TCP_MAXLEN];
} kissTcpFrame;

static QueueHandle_t kissQueue = NULL;   // heard on RF -> client (taskAPRS -> taskNetwork)
static QueueHandle_t kissTxQueue = NULL; // client -> RF when TX is allowed (taskNetwork -> taskAPRS)
static uint32_t kissFromClient = 0, kissSentRf = 0;
static kissTcpFrame kissIn; // frame being received from the client
static bool kissInFrame = false, kissEsc = false, kissData = false;
static WiFiServer *kissServer = NULL;
static WiFiClient kissClient;
static uint16_t kissPort = 0;
static volatile bool kissActive = false; // server running: taskAPRS may queue frames

void kissTcpRxFrame(const uint8_t *buf, size_t len)
{
    if (!kissActive || kissQueue == NULL || len == 0 || len > KISS_TCP_MAXLEN)
        return;
    kissTcpFrame f;
    f.len = len;
    memcpy(f.data, buf, len);
    xQueueSend(kissQueue, &f, 0); // queue full (slow client): the frame is dropped
}

bool kissTcpConnected(void)
{
    return kissActive && kissClient.connected();
}

void kissTcpCounts(uint32_t &fromClient, uint32_t &sentRf)
{
    fromClient = kissFromClient;
    sentRf = kissSentRf;
}

void kissTcpTxPoll(void)
{
    kissTcpFrame f;
    if (kissTxQueue == NULL || xQueueReceive(kissTxQueue, &f, 0) != pdTRUE)
        return;
    if (!config.kiss_tcp_tx)
        return; // TX turned off while the frame was waiting
    if (Ax25WriteTxFrame(f.data, f.len) != NULL)
    {
        status.txCount++; // a transmission on RF like any other
        kissSentRf++;
    }
}

// One byte from the client (KISS decoding: FEND ... FEND, FESC escapes, first byte = command)
static void kissTcpByte(uint8_t b)
{
    if (b == FEND)
    {
        if (kissInFrame && kissData && kissIn.len >= 15) // at least two addresses, control and PID
        {
            kissFromClient++;
            if (config.kiss_tcp_tx && kissTxQueue)
                xQueueSend(kissTxQueue, &kissIn, 0); // queue full: dropped
        }
        kissInFrame = true;
        kissData = false;
        kissEsc = false;
        kissIn.len = 0xFFFF; // next byte is the command
        return;
    }
    if (!kissInFrame)
        return;
    if (kissIn.len == 0xFFFF)
    {
        kissData = (b & 0x0F) == CMD_DATA; // port nibble ignored; anything else (TXDELAY...) is skipped
        kissIn.len = 0;
        return;
    }
    if (!kissData)
        return;
    if (b == FESC)
    {
        kissEsc = true;
        return;
    }
    if (kissEsc)
    {
        b = b == TFEND ? FEND : b == TFESC ? FESC : b;
        kissEsc = false;
    }
    if (kissIn.len < KISS_TCP_MAXLEN)
        kissIn.data[kissIn.len++] = b;
    else
        kissInFrame = false; // too long: dropped
}

static void kissTcpStop(void)
{
    kissActive = false;
    if (kissClient)
        kissClient.stop();
    if (kissServer)
    {
        kissServer->end();
        delete kissServer;
        kissServer = NULL;
    }
    if (kissQueue)
        xQueueReset(kissQueue);
    kissPort = 0;
}

void kissTcpLoop(void)
{
    uint16_t port = config.kiss_tcp_port ? config.kiss_tcp_port : KISS_TCP_PORT;
    bool want = config.kiss_tcp_en && (WiFi.isConnected() || WiFi.softAPgetStationNum() > 0);
    if (!want || port != kissPort)
    {
        if (kissServer)
            kissTcpStop();
        if (!want)
            return;
    }
    if (kissServer == NULL)
    {
        if (kissQueue == NULL)
            kissQueue = xQueueCreate(KISS_TCP_QUEUE, sizeof(kissTcpFrame));
        if (kissTxQueue == NULL)
            kissTxQueue = xQueueCreate(2, sizeof(kissTcpFrame));
        if (kissQueue == NULL || kissTxQueue == NULL)
            return;
        kissServer = new WiFiServer(port);
        kissServer->begin();
        kissServer->setNoDelay(true);
        kissPort = port;
        kissActive = true;
        log_d("KISS TCP on port %u", port);
    }

    WiFiClient c = kissServer->available();
    if (c)
    {
        if (kissClient)
            kissClient.stop(); // one client: the newest wins
        kissClient = c;
        kissClient.setNoDelay(true);
        xQueueReset(kissQueue); // start with what is heard from now on
        kissInFrame = false;
    }
    if (!kissClient)
        return;
    if (!kissClient.connected())
    {
        kissClient.stop();
        return;
    }

    kissTcpFrame f;
    static uint8_t pkg[2 * KISS_TCP_MAXLEN + 3]; // worst case: every byte escaped, plus FEND, command, FEND
    while (xQueueReceive(kissQueue, &f, 0) == pdTRUE)
    {
        int n = kiss_wrapper(pkg, f.data, f.len);
        if (kissClient.write(pkg, n) != (size_t)n)
        {
            kissClient.stop(); // the client stopped reading
            return;
        }
    }

    int n = 0;
    while (kissClient.available() && n++ < 512)
    {
        int b = kissClient.read();
        if (b >= 0)
            kissTcpByte((uint8_t)b);
    }
}
