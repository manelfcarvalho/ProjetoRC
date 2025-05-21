/* =========================== src/server.c =========================== */
#include "powerudp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <time.h>
#include <errno.h>
#include <sys/time.h>

#define MAX_TCP_QUEUE  8
#define BUF_CMD       128

static int tcp_sock = -1;
static int mc_sock  = -1;
static struct sockaddr_in mc_dst;

/* Envia ConfigMessage em multicast para DATA_PORT (6001) */
static void multicast_config(uint16_t to_ms, uint8_t max_rtx)
{
    // Configura TTL para permitir que as mensagens alcancem todos os clientes
    int ttl = 5;
    if (setsockopt(mc_sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) < 0) {
        perror("IP_MULTICAST_TTL");
        return;
    }

    // Configura timeout para recepção de ACKs
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 200000;  // 200ms timeout
    if (setsockopt(mc_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("SO_RCVTIMEO");
        return;
    }

    // Buffer para receber ACKs
    char ack_buf[sizeof(PUDPHeader)];
    struct sockaddr_in from;
    socklen_t fromlen = sizeof(from);

    // Envia a mensagem várias vezes para garantir que todos os clientes recebam
    int max_attempts = 5;  // Aumentado para 5 tentativas
    for (int i = 0; i < max_attempts; i++) {
        char frame[sizeof(PUDPHeader) + sizeof(ConfigMessage)];
        PUDPHeader *h = (PUDPHeader *)frame;
        h->seq   = htonl(0);
        h->flags = PUDP_F_CFG;

        ConfigMessage *c = (ConfigMessage *)(frame + sizeof(PUDPHeader));
        c->base_timeout_ms = (uint32_t)to_ms;
        c->max_retries     = max_rtx;

        // Envia para o endereço multicast
        if (sendto(mc_sock, frame, sizeof frame, 0,
                   (struct sockaddr *)&mc_dst, sizeof mc_dst) < 0) {
            perror("sendto multicast");
            continue;
        }

        printf("[SRV] Sent config (try %d/%d): timeout=%u ms, retries=%u\n",
               i+1, max_attempts, to_ms, max_rtx);

        // Espera por ACKs (opcional - pode ser removido se causar problemas)
        int received_acks = 0;
        while (1) {
            int n = recvfrom(mc_sock, ack_buf, sizeof(ack_buf), 0,
                            (struct sockaddr *)&from, &fromlen);
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;  // Timeout - próxima tentativa
                }
                perror("recvfrom");
                break;
            }
            
            PUDPHeader *ack_h = (PUDPHeader *)ack_buf;
            if (ack_h->flags & PUDP_F_ACK) {
                char ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
                printf("[SRV] Received config ACK from %s\n", ip);
                received_acks++;
            }
        }

        if (received_acks > 0) {
            printf("[SRV] Received %d ACKs for config message\n", received_acks);
            if (i < max_attempts - 1) {
                printf("[SRV] Continuing with remaining attempts for reliability\n");
            }
        }

        // Pequeno delay entre retransmissões
        struct timespec ts = {
            .tv_sec = 0,
            .tv_nsec = 100000000  // 100ms
        };
        nanosleep(&ts, NULL);
    }
}

/* Thread por cliente TCP */
static void *client_thr(void *arg)
{
    int csock = *(int *)arg; free(arg);
    char buf[BUF_CMD];

    RegisterMessage reg;
    if (recv(csock, &reg, sizeof reg, MSG_WAITALL) != sizeof reg) {
        close(csock); return NULL;
    }
    if (strcmp(reg.psk, "mypsk") != 0) {
        printf("[SRV] Bad PSK – ligação fechada\n");
        close(csock); return NULL;
    }
    printf("[SRV] PSK OK\n");

    /* Loop :setcfg */
    while (1) {
        int n = recv(csock, buf, sizeof buf - 1, 0);
        if (n <= 0) break;
        buf[n] = '\0';
        uint16_t to_ms; uint8_t max_r;
        if (sscanf(buf, ":setcfg %hu %hhu", &to_ms, &max_r) == 2) {
            multicast_config(to_ms, max_r);
        }
    }
    close(csock);
    return NULL;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <tcp-port>\n", argv[0]);
        return 1;
    }
    int port = atoi(argv[1]);

    /* 1) Socket multicast sobre o DATA_PORT */
    mc_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (mc_sock < 0) {
        perror("socket multicast");
        return 1;
    }

    // Permite reutilização do endereço
    int yes = 1;
    if (setsockopt(mc_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        perror("SO_REUSEADDR");
        return 1;
    }

    // Configura endereço multicast
    mc_dst.sin_family = AF_INET;
    mc_dst.sin_port   = htons(PUDP_DATA_PORT);
    if (inet_pton(AF_INET, PUDP_CFG_MC_ADDR, &mc_dst.sin_addr) != 1) {
        perror("inet_pton");
        return 1;
    }

    /* 2) TCP listen para registo */
    tcp_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_sock < 0) {
        perror("socket tcp");
        return 1;
    }

    if (setsockopt(tcp_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) < 0) {
        perror("SO_REUSEADDR tcp");
        return 1;
    }

    struct sockaddr_in s = { 
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(port) 
    };

    if (bind(tcp_sock, (struct sockaddr *)&s, sizeof s) < 0) {
        perror("bind");
        return 1;
    }

    if (listen(tcp_sock, MAX_TCP_QUEUE) < 0) {
        perror("listen");
        return 1;
    }

    printf("[SRV] Ready - TCP port %d, Multicast group %s:%d\n", 
           port, PUDP_CFG_MC_ADDR, PUDP_DATA_PORT);

    /* 3) Aceita clientes indefinidamente */
    while (1) {
        struct sockaddr_in c; 
        socklen_t cl = sizeof c;
        int *cs = malloc(sizeof(int));
        *cs = accept(tcp_sock, (struct sockaddr *)&c, &cl);
        if (*cs < 0) { 
            free(cs); 
            continue; 
        }

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &c.sin_addr, ip, sizeof ip);
        printf("[SRV] New client %s:%d\n", ip, ntohs(c.sin_port));

        pthread_t th;
        pthread_create(&th, NULL, client_thr, cs);
        pthread_detach(th);
    }
    return 0;
}
