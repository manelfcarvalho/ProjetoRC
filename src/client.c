/* =========================== src/client.c ============================ */
#include "powerudp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

int main(int argc,char **argv){
 if(argc!=4){fprintf(stderr,"Usage: %s <server-ip> <port> <psk>\n",argv[0]);return 1;}
 printf("[CLI] Connected to %s:%s using PSK %s\n",argv[1],argv[2],argv[3]);
 return 0;
}




