/* =========================== src/server.c ============================ */
#include "powerudp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <errno.h>

#define MY_PSK "mypsk"
#define BACKLOG 10

struct client_info {
    int sock;
    struct sockaddr_in addr;
};

static void *client_thread(void *arg) {
    struct client_info *ci = (struct client_info *)arg;
    int sock = ci->sock;
    char ipbuf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &ci->addr.sin_addr, ipbuf, sizeof ipbuf);

    struct RegisterMessage regmsg;
    ssize_t n = recv(sock, &regmsg, sizeof(regmsg), MSG_WAITALL);
    if (n != sizeof(regmsg)) {
        fprintf(stderr, "[SRV] %s disconnected during register\n", ipbuf);
        close(sock);
        free(ci);
        return NULL;
    }
    if (strncmp(regmsg.psk, MY_PSK, sizeof(regmsg.psk)) != 0) {
        fprintf(stderr, "[SRV] %s sent wrong PSK → closing\n", ipbuf);
        close(sock);
        free(ci);
        return NULL;
    }
    printf("[SRV]   -> PSK OK from %s\n", ipbuf);

    /* Here we would add the client to some list and start exchanging messages */

    /* Keep connection open for now */
    pause();
    close(sock);
    free(ci);
    return NULL;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <tcp-port>\n", argv[0]);
        return 1;
    }
    int port = atoi(argv[1]);
    int srv_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (srv_sock < 0) { perror("socket"); return 1; }

    int yes = 1;
    setsockopt(srv_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

    struct sockaddr_in srv_addr = {0};
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    srv_addr.sin_port = htons(port);
    if (bind(srv_sock, (struct sockaddr*)&srv_addr, sizeof srv_addr) < 0) {
        perror("bind"); return 1;
    }
    if (listen(srv_sock, BACKLOG) < 0) { perror("listen"); return 1; }
    printf("[SRV] Listening on 0.0.0.0:%d\n", port);

    while (1) {
        struct client_info *ci = malloc(sizeof *ci);
        socklen_t len = sizeof(ci->addr);
        ci->sock = accept(srv_sock, (struct sockaddr*)&ci->addr, &len);
        if (ci->sock < 0) { perror("accept"); free(ci); continue; }

        char ipbuf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &ci->addr.sin_addr, ipbuf, sizeof ipbuf);
        printf("[SRV] New TCP client %s:%d\n", ipbuf, ntohs(ci->addr.sin_port));

        pthread_t tid;
        if (pthread_create(&tid, NULL, client_thread, ci) != 0) {
            perror("pthread_create");
            close(ci->sock);
            free(ci);
            continue;
        }
        pthread_detach(tid);
    }
}


