/*
 KISS over TCP: an APRS program on the network (APRSdroid, Xastir, PinPoint, Dire Wolf...) connects to this
 port and gets every frame heard on RF, as KISS. Off by default.

 - One client at a time; a new connection replaces the old one.
 - RX only unless "Allow TX" is on: what the client sends is read and thrown away, so nobody on the network
   can make the station transmit. With TX allowed, its KISS frames (and TXDELAY/TXTAIL commands) go to the
   modem like those from Bluetooth KISS. There is no password on this port: allow TX only on a trusted network.
*/
#include <WiFi.h>
#include <KISS.h>
#include "main.h"
#include "kisstcp.h"

extern Configuration config;

#define KISS_TCP_QUEUE 8    // frames waiting to be sent to the client
#define KISS_TCP_MAXLEN 330 // longest AX.25 frame kept

typedef struct
{
    uint16_t len;
    uint8_t data[KISS_TCP_MAXLEN];
} kissTcpFrame;

static QueueHandle_t kissQueue = NULL;
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
        if (kissQueue == NULL)
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
        if (b >= 0 && config.kiss_tcp_tx)
            kiss_serial((uint8_t)b); // TX allowed: a complete KISS frame is sent on RF
    }
}
