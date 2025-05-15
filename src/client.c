/* =========================== src/client.c ============================ */
#include "powerudp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>

static void fill_register(struct RegisterMessage *m, const char *psk) {
    memset(m, 0, sizeof *m);
    strncpy(m->psk, psk, sizeof(m->psk) - 1);
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <server-ip> <port> <psk>\n", argv[0]);
        return 1;
    }
    const char *srv_ip = argv[1];
    int port = atoi(argv[2]);
    const char *psk = argv[3];

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    struct sockaddr_in srv_addr = {0};
    srv_addr.sin_family = AF_INET;
    inet_pton(AF_INET, srv_ip, &srv_addr.sin_addr);
    srv_addr.sin_port = htons(port);

    if (connect(sock, (struct sockaddr*)&srv_addr, sizeof srv_addr) < 0) {
        perror("connect"); return 1; }

    struct RegisterMessage regmsg;
    fill_register(&regmsg, psk);
    if (write(sock, &regmsg, sizeof regmsg) != sizeof regmsg) {
        perror("write"); return 1; }

    printf("[CLI] Register message sent to %s:%d\n", srv_ip, port);

    /* For now just exit */
    close(sock);
    return 0;
}
