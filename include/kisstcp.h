#ifndef KISSTCP_H
#define KISSTCP_H

#include <Arduino.h>

#define KISS_TCP_PORT 8001 // usual KISS over TCP port (Dire Wolf, APRSdroid, Xastir, PinPoint)

// A frame heard on RF (raw AX.25, as given by Ax25ReadNextRxFrame); taskAPRS calls it. Never blocks: the
// frame is queued and the network task sends it, the only task that touches the TCP connection.
void kissTcpRxFrame(const uint8_t *buf, size_t len);
// Accept a client, send the queued frames, read what the client sends; taskNetwork calls it every pass.
void kissTcpLoop(void);
// Frames sent by the TX-allowed client: taskAPRS calls it before its own TX (one frame per call)
void kissTcpTxPoll(void);
// A client is connected (web status)
bool kissTcpConnected(void);
// Data frames received from clients, and how many of them were sent on RF
void kissTcpCounts(uint32_t &fromClient, uint32_t &sentRf);

#endif
