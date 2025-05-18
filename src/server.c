/* =========================== src/server.c =========================== */
#include "powerudp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define MAX_TCP_QUEUE  8
#define BUF_CMD       128

/* -------- sockets globais -------- */
static int tcp_sock = -1;          /* socket TCP para registo de clientes  */
static int mc_sock  = -1;          /* socket UDP para multicast ConfigMessage */
static struct sockaddr_in mc_dst;  /* destino multicast (239.0.0.100:6000)   */

/* --------- envia ConfigMessage em multicast ---------- */
static void multicast_config(uint16_t to_ms, uint8_t max_rtx)
{
    char frame[sizeof(PUDPHeader) + sizeof(ConfigMessage)];
    PUDPHeader *h = (PUDPHeader *)frame;
    h->seq   = htonl(0);            /* seq=0 sinaliza pacote de configuração */
    h->flags = PUDP_F_CFG;          /* marca como ConfigMessage       */

    ConfigMessage *c = (ConfigMessage *)(frame + sizeof(PUDPHeader));
    c->base_timeout_ms = to_ms;
    c->max_retries     = max_rtx;

    sendto(mc_sock, frame, sizeof frame, 0,
           (struct sockaddr *)&mc_dst, sizeof mc_dst);

    printf("[SRV] Multicast CFG  timeout=%u  max_retries=%u\n",
           to_ms, max_rtx);
}

/* --------- thread TCP por cliente -------------------- */
static void *client_thr(void *arg)
{
    int csock = *(int *)arg; free(arg);
    char buf[BUF_CMD];

    /* 1) Recebe e valida PSK de registo */
    RegisterMessage reg;
    if (recv(csock, &reg, sizeof reg, MSG_WAITALL) != sizeof reg) {
        close(csock); return NULL;
    }
    if (strcmp(reg.psk, "mypsk") != 0) {
        printf("[SRV] Bad PSK – ligação fechada\n");
        close(csock); return NULL;
    }
    printf("[SRV] PSK OK\n");

    /* 2) Loop de comandos :setcfg <timeout_ms> <max_rtx> */
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

/* ------------------------------ main ------------------------------ */
int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <tcp-port>\n", argv[0]);
        return 1;
    }
    int port = atoi(argv[1]);

    /* 1) Prepara socket multicast para ConfigMessage */
    mc_sock = socket(AF_INET, SOCK_DGRAM, 0);
    int ttl = 1;
    setsockopt(mc_sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof ttl);
    mc_dst.sin_family = AF_INET;
    mc_dst.sin_port   = htons(PUDP_CFG_PORT);           /* 6000 */
    inet_pton(AF_INET, PUDP_CFG_MC_ADDR, &mc_dst.sin_addr);

    /* 2) TCP listen para registo de clientes */
    tcp_sock = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    setsockopt(tcp_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

    struct sockaddr_in s = {0};
    s.sin_family      = AF_INET;
    s.sin_addr.s_addr = htonl(INADDR_ANY);
    s.sin_port        = htons(port);

    if (bind(tcp_sock, (struct sockaddr *)&s, sizeof s) < 0 ||
        listen(tcp_sock, MAX_TCP_QUEUE) < 0) {
        perror("TCP bind/listen");
        return 1;
    }

    printf("[SRV] TCP %d ready (ConfigServer)\n", port);

    /* 3) Aceita clientes indefinidamente */
    while (1) {
        struct sockaddr_in c; socklen_t cl = sizeof c;
        int *cs = malloc(sizeof(int));
        *cs = accept(tcp_sock, (struct sockaddr *)&c, &cl);
        if (*cs < 0) { free(cs); continue; }

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &c.sin_addr, ip, sizeof ip);
        printf("[SRV] New TCP client %s:%d\n", ip, ntohs(c.sin_port));

        pthread_t th;
        pthread_create(&th, NULL, client_thr, cs);
        pthread_detach(th);
    }

    return 0;
}
