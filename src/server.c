/* ============================ src/server.c ========================= */
#include "powerudp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>

static int tcp_sock = -1;

/* ----------- thread que recebe dados UDP e já devolve ACK (dentro da
             receive_message() o ACK é enviado automaticamente) ------- */
static void *udp_loop(void *arg)
{
    (void)arg;
    char msg[512];

    while (1) {
        int n = receive_message(msg, sizeof msg - 1);
        if (n <= 0) continue;         /* 0 = ACK, <0 = nada */

        msg[n] = '\0';
        printf("[SRV] UDP '%s'\n", msg);
    }
    return NULL;
}

/* ---------------- thread por ligação TCP --------------------------- */
static void *client_thr(void *arg)
{
    int csock = *(int *)arg;
    free(arg);

    RegisterMessage reg;
    if (recv(csock, &reg, sizeof reg, MSG_WAITALL) != sizeof reg) {
        close(csock);
        return NULL;
    }

    if (strcmp(reg.psk, "mypsk") != 0) {
        printf("[SRV] Bad PSK\n");
        close(csock);
        return NULL;
    }

    printf("[SRV] PSK OK\n");
    pause();                          /* mantém a thread viva */
    return NULL;
}

/* ----------------------------- main -------------------------------- */
int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <tcp-port>\n", argv[0]);
        return 1;
    }
    int port = atoi(argv[1]);

    /* 1) Inicia PowerUDP (cria socket UDP e thread de retransmissão) */
    if (init_protocol_server() != 0) return 1;

    /* 2) Arranca thread que fica a escutar pacotes UDP */
    pthread_t udp_th;
    pthread_create(&udp_th, NULL, udp_loop, NULL);
    pthread_detach(udp_th);

    /* 3) Prepara socket TCP (registo dos clientes) */
    tcp_sock = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    setsockopt(tcp_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

    struct sockaddr_in s = {0};
    s.sin_family      = AF_INET;
    s.sin_addr.s_addr = htonl(INADDR_ANY);
    s.sin_port        = htons(port);

    if (bind(tcp_sock, (struct sockaddr *)&s, sizeof s) < 0 ||
        listen(tcp_sock, 8) < 0) {
        perror("TCP bind/listen");
        return 1;
    }

    printf("[SRV] TCP %d  UDP %d ready\n", port, PUDP_DATA_PORT);

    /* 4) Loop principal: aceita cada novo cliente TCP */
    while (1) {
        struct sockaddr_in c;
        socklen_t          cl = sizeof c;
        int                *cs = malloc(sizeof(int));

        *cs = accept(tcp_sock, (struct sockaddr *)&c, &cl);
        if (*cs < 0) { free(cs); continue; }

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &c.sin_addr, ip, sizeof ip);
        printf("[SRV] New TCP client %s:%d\n", ip, ntohs(c.sin_port));

        pthread_t t;
        pthread_create(&t, NULL, client_thr, cs);
        pthread_detach(t);
    }
}
/* =================================================================== */
