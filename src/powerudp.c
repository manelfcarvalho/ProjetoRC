/* ======================== src/powerudp.c ======================== */
#define _POSIX_C_SOURCE 200809L
#include "powerudp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <time.h>

#define MAX_PENDING 32
#define MAX_PAYLOAD 512
#define MAX_SEQ_GAP 100

typedef struct {
    uint32_t            seq;
    int                 len;
    char                data[sizeof(PUDPHeader) + MAX_PAYLOAD];
    struct timeval      ts;
    uint32_t            to_ms;
    int                 retries;
    struct sockaddr_in  dst;
    int                 in_use;
} Pending;

static Pending          pend[MAX_PENDING];
static pthread_mutex_t  pend_mtx        = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t  seq_mtx         = PTHREAD_MUTEX_INITIALIZER;

/* UDP socket for both client and server roles */
int udp_sock = -1;

/* sequencing & timeouts */
static uint32_t seq_tx          = 1;
static uint32_t base_timeout_ms = PUDP_BASE_TO_MS;
static uint8_t  max_retries     = PUDP_MAX_RETRY;
static int      drop_probability= 0;

/* last event for CLI sync */
static int      last_evt_status = 0;  /* 1=ACK, -1=DROP */
static uint32_t last_evt_seq    = 0;

/* Mapa de sequências por IP */
#define MAX_PEERS 256
typedef struct {
    struct in_addr addr;
    uint32_t      expected_seq;
    int           in_use;
} PeerSeq;

static PeerSeq peer_seqs[MAX_PEERS];

/* Declarações antecipadas de funções */
static uint32_t now_ms(void);
static void msleep(unsigned int ms);
static void send_ack(const struct sockaddr_in *dst, uint32_t seq);
static void send_nak(const struct sockaddr_in *dst, uint32_t expected_seq);
static void send_sync_message(const struct sockaddr_in *dst, uint32_t last_seq, uint32_t next_seq);
static uint32_t get_peer_seq(struct in_addr addr);
static void update_peer_seq(struct in_addr addr, uint32_t new_seq);
static void add_pending(uint32_t seq, const char *frame, int len, const struct sockaddr_in *dst);
static void ack_pending(uint32_t seq);
static int common_udp_init(uint16_t port);
static void *retrans_loop(void *arg);
static void apply_config(const ConfigMessage *cfg);
static int resend_now(uint32_t seq);

/* Implementações das funções */
static uint32_t now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static void msleep(unsigned int ms) {
    struct timespec ts = {
        .tv_sec = ms / 1000,
        .tv_nsec = (ms % 1000) * 1000000
    };
    nanosleep(&ts, NULL);
}

static void send_ack(const struct sockaddr_in *dst, uint32_t seq) {
    PUDPHeader ack = { htonl(seq), PUDP_F_ACK, {0} };
    sendto(udp_sock, &ack, sizeof ack, 0,
           (struct sockaddr*)dst, sizeof(*dst));
    fprintf(stderr, "[PUDP] Sent ACK for seq=%u\n", seq);
}

static void send_nak(const struct sockaddr_in *dst, uint32_t expected_seq) {
    PUDPHeader nack = { htonl(expected_seq), PUDP_F_NAK, {0} };
    sendto(udp_sock, &nack, sizeof nack, 0,
           (struct sockaddr*)dst, sizeof(*dst));
    fprintf(stderr, "[PUDP] Sent NAK, expecting seq=%u\n", expected_seq);
}

static void send_sync_message(const struct sockaddr_in *dst, uint32_t last_seq, uint32_t next_seq) {
    char frame[sizeof(PUDPHeader) + sizeof(SyncMessage)];
    PUDPHeader *h = (PUDPHeader*)frame;
    h->seq = htonl(next_seq);
    h->flags = PUDP_F_SYNC;
    
    SyncMessage *sync = (SyncMessage*)(frame + sizeof(PUDPHeader));
    sync->last_seq = htonl(last_seq);
    sync->next_seq = htonl(next_seq);
    
    sendto(udp_sock, frame, sizeof(frame), 0,
           (struct sockaddr*)dst, sizeof(*dst));
    
    fprintf(stderr, "[PUDP] Sent SYNC message (last=%u, next=%u)\n", 
            last_seq, next_seq);
}

static uint32_t get_peer_seq(struct in_addr addr) {
    pthread_mutex_lock(&seq_mtx);
    for (int i = 0; i < MAX_PEERS; i++) {
        if (peer_seqs[i].in_use && 
            peer_seqs[i].addr.s_addr == addr.s_addr) {
            uint32_t seq = peer_seqs[i].expected_seq;
            pthread_mutex_unlock(&seq_mtx);
            return seq;
        }
    }
    
    // Novo peer, procura slot livre
    for (int i = 0; i < MAX_PEERS; i++) {
        if (!peer_seqs[i].in_use) {
            peer_seqs[i].addr = addr;
            peer_seqs[i].expected_seq = 1;
            peer_seqs[i].in_use = 1;
            pthread_mutex_unlock(&seq_mtx);
            return 1;
        }
    }
    pthread_mutex_unlock(&seq_mtx);
    return 1;
}

static void update_peer_seq(struct in_addr addr, uint32_t new_seq) {
    pthread_mutex_lock(&seq_mtx);
    for (int i = 0; i < MAX_PEERS; i++) {
        if (peer_seqs[i].in_use && 
            peer_seqs[i].addr.s_addr == addr.s_addr) {
            peer_seqs[i].expected_seq = new_seq;
            pthread_mutex_unlock(&seq_mtx);
            return;
        }
    }
    pthread_mutex_unlock(&seq_mtx);
}

static void add_pending(uint32_t seq, const char *frame, int len,
                       const struct sockaddr_in *dst) {
    pthread_mutex_lock(&pend_mtx);
    for (int i = 0; i < MAX_PENDING; ++i) {
        if (!pend[i].in_use) {
            pend[i].seq     = seq;
            pend[i].len     = len;
            memcpy(pend[i].data, frame, len);
            pend[i].dst     = *dst;
            gettimeofday(&pend[i].ts, NULL);
            pend[i].to_ms   = base_timeout_ms;
            pend[i].retries = 0;
            pend[i].in_use  = 1;
            pthread_mutex_unlock(&pend_mtx);
            return;
        }
    }
    pthread_mutex_unlock(&pend_mtx);
}

static void ack_pending(uint32_t seq) {
    for (int i = 0; i < MAX_PENDING; ++i) {
        if (pend[i].in_use && pend[i].seq == seq) {
            pend[i].in_use = 0;
            last_evt_status = 1;
            last_evt_seq = seq;
            fprintf(stderr, "[PUDP] ACK received for seq=%u\n", seq);
            return;
        }
    }
    fprintf(stderr, "[PUDP] ACK for unknown seq=%u\n", seq);
}

static void apply_config(const ConfigMessage *cfg) {
    base_timeout_ms = cfg->base_timeout_ms;
    max_retries     = cfg->max_retries;
    fprintf(stderr, "[PUDP] NEW CONFIG  timeout=%u ms  max_rtx=%u\n",
           base_timeout_ms, max_retries);
}

static int resend_now(uint32_t seq) {
    pthread_mutex_lock(&pend_mtx);
    for (int i = 0; i < MAX_PENDING; ++i) {
        if (pend[i].in_use && pend[i].seq == seq) {
            sendto(udp_sock, pend[i].data, pend[i].len, 0,
                   (struct sockaddr*)&pend[i].dst, sizeof pend[i].dst);
            gettimeofday(&pend[i].ts, NULL);
            fprintf(stderr,
                "[PUDP] NACK → instant retrans seq=%u\n", seq);
            pthread_mutex_unlock(&pend_mtx);
            return 0;
        }
    }
    pthread_mutex_unlock(&pend_mtx);
    return -1;
}

static int common_udp_init(uint16_t port) {
    udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock < 0) return -1;
    struct sockaddr_in a = {
        .sin_family      = AF_INET,
        .sin_port        = htons(port),
        .sin_addr.s_addr = htonl(INADDR_ANY)
    };
    if (bind(udp_sock, (struct sockaddr*)&a, sizeof a) < 0) return -1;
    pthread_t th;
    pthread_create(&th, NULL, retrans_loop, NULL);
    pthread_detach(th);
    return 0;
}

static void *retrans_loop(void *arg) {
    (void)arg;
    while (1) {
        pthread_mutex_lock(&pend_mtx);
        uint32_t now = now_ms();
        for (int i = 0; i < MAX_PENDING; ++i) {
            if (!pend[i].in_use) continue;
            uint32_t sent_ms = pend[i].ts.tv_sec * 1000 + pend[i].ts.tv_usec / 1000;
            if (now - sent_ms < pend[i].to_ms) continue;

            if (pend[i].retries >= max_retries) {
                fprintf(stderr,
                    "[PUDP] seq %u DROPPED after %d tries\n",
                    pend[i].seq, pend[i].retries);
                send_sync_message(&pend[i].dst, pend[i].seq, seq_tx);
                last_evt_status = -1;
                last_evt_seq = pend[i].seq;
                pend[i].in_use = 0;
                continue;
            }

            sendto(udp_sock, pend[i].data, pend[i].len, 0,
                   (struct sockaddr*)&pend[i].dst, sizeof pend[i].dst);
            gettimeofday(&pend[i].ts, NULL);
            pend[i].retries++;
            pend[i].to_ms *= 2;
            fprintf(stderr,
                "[PUDP] retrans seq=%u (try %d, to %ums)\n",
                pend[i].seq, pend[i].retries, pend[i].to_ms);
        }
        pthread_mutex_unlock(&pend_mtx);
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 100000000 };
        nanosleep(&ts, NULL);
    }
    return NULL;
}

int receive_message(void *buf, int buflen) {
    char frame[sizeof(PUDPHeader) + MAX_PAYLOAD];
    struct sockaddr_in src;
    socklen_t sl = sizeof src;
    int n = recvfrom(udp_sock, frame, sizeof frame, 0,
                     (struct sockaddr*)&src, &sl);
    if (n <= 0) return n;

    PUDPHeader *h = (PUDPHeader*)frame;
    h->seq = ntohl(h->seq);

    char src_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &src.sin_addr, src_ip, sizeof(src_ip));

    // Obtém a sequência esperada para este peer
    uint32_t peer_expected_seq = get_peer_seq(src.sin_addr);

    fprintf(stderr, "[PUDP] Received seq=%u from %s (expected=%u)\n",
            h->seq, src_ip, peer_expected_seq);

    if (h->flags & PUDP_F_ACK) {
        pthread_mutex_lock(&pend_mtx);
        ack_pending(h->seq);
        pthread_mutex_unlock(&pend_mtx);
        msleep(1);
        return 0;
    }

    if (h->flags & PUDP_F_NAK) {
        fprintf(stderr, "[PUDP] NAK received from %s for seq=%u\n",
                src_ip, h->seq);
        resend_now(h->seq);
        return 0;
    }

    if (h->flags & PUDP_F_CFG) {
        if ((size_t)n >= sizeof(*h) + sizeof(ConfigMessage))
            apply_config((ConfigMessage*)(frame + sizeof(*h)));
        return 0;
    }

    if (h->flags & PUDP_F_SYNC) {
        if ((size_t)n >= sizeof(*h) + sizeof(SyncMessage)) {
            SyncMessage *sync = (SyncMessage*)(frame + sizeof(*h));
            uint32_t next_seq = ntohl(sync->next_seq);
            
            // Só atualiza se a nova sequência for maior
            if (next_seq > peer_expected_seq) {
                update_peer_seq(src.sin_addr, next_seq);
                fprintf(stderr, "[PUDP] Resync from %s: expected_seq updated to %u\n",
                        src_ip, next_seq);
            }
            
            // Confirma recebimento da mensagem de sync
            send_ack(&src, h->seq);
        }
        return 0;
    }

    // Se a diferença de sequência for muito grande, pede ressincronização
    if (h->seq > peer_expected_seq && h->seq - peer_expected_seq > MAX_SEQ_GAP) {
        fprintf(stderr, "[PUDP] Large sequence gap from %s (%u -> %u), requesting resync\n",
                src_ip, peer_expected_seq, h->seq);
        send_sync_message(&src, peer_expected_seq, h->seq);
        return 0;
    }

    if (h->seq == peer_expected_seq) {
        // Processa a mensagem atual
        update_peer_seq(src.sin_addr, peer_expected_seq + 1);
        send_ack(&src, h->seq);

        fprintf(stderr, "[PUDP] Processing seq=%u from %s\n", h->seq, src_ip);

        int dlen = n - (int)sizeof(*h);
        if (dlen > buflen) dlen = buflen;
        if (buf) memcpy(buf, frame + sizeof(*h), dlen);
        
        msleep(1);
        return dlen;
    } else if (h->seq < peer_expected_seq) {
        // Mensagem antiga ou duplicada, reenvia ACK
        fprintf(stderr, "[PUDP] Duplicate/old seq=%u from %s (expected=%u), sending ACK\n",
                h->seq, src_ip, peer_expected_seq);
        send_ack(&src, h->seq);
        return 0;
    } else {
        // Sequência futura
        fprintf(stderr, "[PUDP] Future seq=%u from %s (expected=%u), sending NAK\n",
                h->seq, src_ip, peer_expected_seq);
        send_nak(&src, peer_expected_seq);
        return 0;
    }
}

int send_message(const char *dest_ip, const void *buf, int len) {
    if (len > MAX_PAYLOAD) { errno = EINVAL; return -1; }
    
    char frame[sizeof(PUDPHeader) + MAX_PAYLOAD];
    PUDPHeader *h = (PUDPHeader*)frame;
    h->seq   = htonl(seq_tx++);
    h->flags = 0;
    memcpy(frame + sizeof(*h), buf, len);
    int flen = sizeof(*h) + len;

    struct sockaddr_in dst = {
        .sin_family = AF_INET,
        .sin_port   = htons(PUDP_DATA_PORT)
    };
    if (inet_pton(AF_INET, dest_ip, &dst.sin_addr) != 1) {
        errno = EINVAL;
        return -1;
    }

    fprintf(stderr, "[PUDP] Sending seq=%u to %s\n", 
            ntohl(h->seq), dest_ip);

    add_pending(ntohl(h->seq), frame, flen, &dst);

    if (drop_probability && (rand() % 100) < drop_probability) {
        fprintf(stderr, "[PUDP] **SIM DROP %d%%**\n", drop_probability);
        return len;
    }

    int sent = sendto(udp_sock, frame, flen, 0,
                     (struct sockaddr*)&dst, sizeof dst);
    
    if (sent != flen) {
        fprintf(stderr, "[PUDP] Send error to %s: %s\n", 
                dest_ip, strerror(errno));
        return -1;
    }
    
    return len;
}

int init_protocol_client(void) {
    return common_udp_init(0);
}

int init_protocol_server(void) {
    return common_udp_init(PUDP_DATA_PORT);
}

void close_protocol(void) {
    if (udp_sock >= 0) close(udp_sock);
}

int inject_packet_loss(int pct) {
    if (pct < 0 || pct > 100) return -1;
    drop_probability = pct;
    return 0;
}

int powerudp_pending_count(void) {
    int c = 0;
    for (int i = 0; i < MAX_PENDING; ++i)
        if (pend[i].in_use) c++;
    return c;
}

int powerudp_last_event(uint32_t *seq, int *status) {
    if (!last_evt_status) return 0;
    *seq    = last_evt_seq;
    *status = last_evt_status;
    last_evt_status = 0;
    return 1;
}
