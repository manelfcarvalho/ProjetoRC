/* =========================== src/powerudp.c ============================ */
#include "powerudp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

/* Simple placeholder implementation */
static int udp_sock = -1;

int init_protocol(const char *server_ip, int server_port, const char *psk) {
    (void)server_ip; (void)server_port; (void)psk;
    udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock < 0) return -1;
    return 0;
}

void close_protocol(void) {
    if (udp_sock != -1) close(udp_sock);
    udp_sock = -1;
}

int request_protocol_config(int a,int b,int c,uint16_t d,uint8_t e){(void)a;(void)b;(void)c;(void)d;(void)e;return 0;}
int send_message(const char *ip,const char *msg,int len){(void)ip;(void)msg;return len;}
int receive_message(char *buf,int buflen){(void)buf;(void)buflen;return 0;}
int get_last_message_stats(int *r,int *d){(void)r;(void)d;return 0;}
void inject_packet_loss(int p){(void)p;}


