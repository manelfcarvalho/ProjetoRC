/* ============================== client.c ============================ */
#include "powerudp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define BUFSZ 512

static int tcp_register(const char *srv_ip, int port, const char *psk)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return -1; }

    struct sockaddr_in dst = {0};
    dst.sin_family = AF_INET;
    dst.sin_port   = htons(port);
    if (inet_pton(AF_INET, srv_ip, &dst.sin_addr) != 1) {
        fprintf(stderr, "invalid IP\n"); close(sock); return -1;
    }
    if (connect(sock, (struct sockaddr*)&dst, sizeof dst) < 0) {
        perror("connect"); close(sock); return -1;
    }

    RegisterMessage reg = {0};
    strncpy(reg.psk, psk, sizeof reg.psk - 1);
    if (send(sock, &reg, sizeof reg, 0) != sizeof reg) {
        perror("send reg"); close(sock); return -1;
    }
    printf("[CLI] Registered at %s:%d (PSK=%s)\n", srv_ip, port, psk);
    return sock; /* mantemos aberto (futuro) */
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <server_ip> <tcp_port> <psk>\n", argv[0]);
        return 1;
    }
    const char *ip   = argv[1];
    int         port = atoi(argv[2]);
    const char *psk  = argv[3];

    if (init_protocol(ip, port, psk) != 0) return 1;
    int tcp_sock = tcp_register(ip, port, psk);
    if (tcp_sock < 0) return 1;

    char line[BUFSZ];
    printf("[CLI] Type message + ENTER (Ctrl-D to quit)\n");
    while (fgets(line, sizeof line, stdin)) {
        size_t len = strlen(line);
        if (len && line[len-1]=='\n') line[--len]='\0';
        if (!len) continue;

        if (send_message(ip, line, (int)len) < 0) perror("send_message");
        else printf("[CLI] UDP sent (%zu bytes)\n", len);
    }
    close_protocol();
    close(tcp_sock);
    return 0;
}
/* =================================================================== */
