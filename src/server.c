/* ============================== server.c ============================ */
#include "powerudp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <errno.h>

#define MAX_CLIENTS 64

static int tcp_port;
static int tcp_sock = -1;
static int udp_sock = -1;

/* ------------------------- UDP listener */
static void *udp_loop(void *arg)
{
    (void)arg;
    char buf[512];
    struct sockaddr_in src;
    socklen_t slen = sizeof src;

    while (1) {
        int n = recvfrom(udp_sock, buf, sizeof buf - 1, 0,
                         (struct sockaddr*)&src, &slen);
        if (n <= 0) continue;
        buf[n] = '\0';
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &src.sin_addr, ip, sizeof ip);
        printf("[UDP] from %s:%d -> '%s'\n", ip, ntohs(src.sin_port), buf);
    }
    return NULL;
}

/* ------------------------- thread por cliente TCP */
static void *client_thread(void *arg)
{
    int csock = *(int*)arg;
    free(arg);

    RegisterMessage reg;
    int r = recv(csock, &reg, sizeof reg, MSG_WAITALL);
    if (r != sizeof reg) { close(csock); return NULL; }

    if (strcmp(reg.psk, "mypsk") != 0) {
        printf("[SRV]  -> PSK FAIL, closing\n");
        close(csock);
        return NULL;
    }
    printf("[SRV]  -> PSK OK from client\n");

    /* (neste esqueleto não há mais nada a fazer via TCP) */
    pause();   /* mantém thread viva */
    return NULL;
}

/* ------------------------- main */
int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <tcp-port>\n", argv[0]);
        return 1;
    }
    tcp_port = atoi(argv[1]);

    /* 1. socket UDP data */
    udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock < 0) { perror("udp socket"); return 1; }
    struct sockaddr_in u = {0};
    u.sin_family = AF_INET;
    u.sin_addr.s_addr = htonl(INADDR_ANY);
    u.sin_port = htons(PUDP_DATA_PORT);
    if (bind(udp_sock, (struct sockaddr*)&u, sizeof u) < 0) {
        perror("udp bind"); return 1;
    }
    pthread_t th; pthread_create(&th, NULL, udp_loop, NULL);

    /* 2. socket TCP listen */
    tcp_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_sock < 0) { perror("tcp socket"); return 1; }
    int yes = 1;
    setsockopt(tcp_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

    struct sockaddr_in s = {0};
    s.sin_family = AF_INET;
    s.sin_addr.s_addr = htonl(INADDR_ANY);
    s.sin_port = htons(tcp_port);
    if (bind(tcp_sock, (struct sockaddr*)&s, sizeof s) < 0) {
        perror("tcp bind"); return 1;
    }
    listen(tcp_sock, MAX_CLIENTS);
    printf("[SRV] Listening on 0.0.0.0:%d (UDP %d)\n",
           tcp_port, PUDP_DATA_PORT);

    while (1) {
        struct sockaddr_in c; socklen_t clen = sizeof c;
        int *csock = malloc(sizeof(int));
        *csock = accept(tcp_sock, (struct sockaddr*)&c, &clen);
        if (*csock < 0) { free(csock); continue; }

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &c.sin_addr, ip, sizeof ip);
        printf("[SRV] New TCP client %s:%d\n", ip, ntohs(c.sin_port));

        pthread_t th;
        pthread_create(&th, NULL, client_thread, csock);
        pthread_detach(th);
    }
    return 0;
}


