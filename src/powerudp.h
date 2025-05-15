/* =========================== src/powerudp.h ============================ */
#ifndef POWERUDP_H
#define POWERUDP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Flags */
#define PUDP_FLAG_ACK 0x01
#define PUDP_FLAG_NAK 0x02
#define PUDP_FLAG_CFG 0x04

struct PUDPHeader {
    uint32_t seq;
    uint8_t  flags;
    uint8_t  reserved[3];
} __attribute__((packed));

struct ConfigMessage {
    uint8_t enable_retransmission;
    uint8_t enable_backoff;
    uint8_t enable_sequence;
    uint16_t base_timeout; /* ms */
    uint8_t max_retries;
} __attribute__((packed));

struct RegisterMessage {
    char psk[64];
} __attribute__((packed));

int  init_protocol(const char *server_ip, int server_port, const char *psk);
void close_protocol(void);

int  request_protocol_config(int enable_retx, int enable_backoff,
                             int enable_seq, uint16_t base_to, uint8_t max_rtx);

int  send_message(const char *dest_ip, const char *msg, int len);
int  receive_message(char *buf, int buflen);

int  get_last_message_stats(int *retransmissions, int *delivery_time_ms);
void inject_packet_loss(int probability_pct);

#ifdef __cplusplus
}
#endif

#endif /* POWERUDP_H */


