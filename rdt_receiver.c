#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <assert.h>

#include "common.h"
#include "packet.h"

#define MAX 64

/*
 * You ar required to change the implementation to support
 * window size greater than one.
 * In the currenlt implemenetation window size is one, hence we have
 * onlyt one send and receive packet
 */
tcp_packet *recvpkt;
tcp_packet *sndpkt;

typedef struct myTcpPacket{
    int datasize;
    int seqno;
    char *data;
} myTcpPacket;

unsigned long hash(char* str){
    unsigned long hash = 5381;
    int c;

    while ((c = *str++)){
        hash = ((hash << 5) + hash) + c;
    }

    return hash;
}

int sortPackets(const void *a, const void *b){
    myTcpPacket *A = (myTcpPacket *) a;
    myTcpPacket *B = (myTcpPacket *) b;

    if (A->seqno == -1 && B->seqno != -1)
    {
        return 1;
    }
    if (A->seqno != -1 && B->seqno == -1)
    {
        return -1;
    }
    if (A->seqno == -1 && B->seqno == -1)
    {
        return 0;
    }        
    return A->seqno - B->seqno; 
}

int main(int argc, char **argv) {
    int sockfd; /* socket */
    int portno; /* port to listen on */
    int clientlen; /* byte size of client's address */
    struct sockaddr_in serveraddr; /* server's addr */
    struct sockaddr_in clientaddr; /* client addr */
    int optval; /* flag value for setsockopt */
    FILE *fp;
    char buffer[MSS_SIZE];
    struct timeval tp;

    /* 
     * check command line arguments 
     */
    if (argc != 3) {
        fprintf(stderr, "usage: %s <port> FILE_RECVD\n", argv[0]);
        exit(1);
    }
    portno = atoi(argv[1]);

    fp  = fopen(argv[2], "w");
    if (fp == NULL) {
        error(argv[2]);
    }

    /* 
     * socket: create the parent socket 
     */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");

    /* setsockopt: Handy debugging trick that lets 
     * us rerun the server immediately after we kill it; 
     * otherwise we have to wait about 20 secs. 
     * Eliminates "ERROR on binding: Address already in use" error. 
     */
    optval = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, 
            (const void *)&optval , sizeof(int));

    /*
     * build the server's Internet address
     */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((unsigned short)portno);

    /* 
     * bind: associate the parent socket with a port 
     */
    if (bind(sockfd, (struct sockaddr *) &serveraddr, 
                sizeof(serveraddr)) < 0) 
        error("ERROR on binding");

    /* 
     * main loop: wait for a datagram, then echo it
     */
    VLOG(DEBUG, "epoch time, bytes received, sequence number");

    myTcpPacket packetBuffer[MAX];
    for(int i=0; i<MAX; i++){
        packetBuffer[i].datasize = -1;
        packetBuffer[i].seqno = -1;
    }

    clientlen = sizeof(clientaddr);
    int next_seqno = 0; //number of next packet expected

    // int started = 0;
    int noOfPackets = 0;

    int wait = 1; 
    fd_set rfds;
    struct timeval tv;
    int activity;

    while (1) {
        /*
         * recvfrom: receive a UDP datagram from a client
         */
        //VLOG(DEBUG, "waiting from server \n");

        if (recvfrom(sockfd, buffer, MSS_SIZE, 0,
                (struct sockaddr *) &clientaddr, (socklen_t *)&clientlen) < 0) {
            error("ERROR in recvfrom");
        }
        recvpkt = (tcp_packet *) buffer;
        assert(get_data_size(recvpkt) <= DATA_SIZE);
        if ( recvpkt->hdr.data_size == 0) {
            //VLOG(INFO, "End Of File has been reached");
            sndpkt = make_packet(0);
            sndpkt->hdr.ackno = -1;
            sndpkt->hdr.ctr_flags = ACK;
            if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0, 
                    (struct sockaddr *) &clientaddr, clientlen) < 0) {
                error("ERROR in sendto");
            }
            fclose(fp);
            printf("End Of File has been reached\n");
            break;
        }
        /* 
         * sendto: ACK back to the client 
         */
        gettimeofday(&tp, NULL);

        if (next_seqno > recvpkt->hdr.seqno){
            // VLOG(DEBUG, "%lu, %d, %d   - DUPLICATE PACKET", tp.tv_sec, recvpkt->hdr.data_size, recvpkt->hdr.seqno);
        }

        if (next_seqno == recvpkt->hdr.seqno){
            // VLOG(DEBUG, "%lu, %d, %d", tp.tv_sec, recvpkt->hdr.data_size, recvpkt->hdr.seqno);
            fseek(fp, recvpkt->hdr.seqno, SEEK_SET);
            fwrite(recvpkt->data, 1, recvpkt->hdr.data_size, fp);
            next_seqno = recvpkt->hdr.seqno + recvpkt->hdr.data_size;

            int x=noOfPackets;
            for(int i=0; i<x; i++){
                if (next_seqno == packetBuffer[i].seqno){
                    fseek(fp, packetBuffer[i].seqno, SEEK_SET);
                    fwrite(packetBuffer[i].data, 1, packetBuffer[i].datasize, fp);
                    next_seqno += packetBuffer[i].datasize;

                    packetBuffer[i].datasize = -1;
                    packetBuffer[i].seqno = -1;
                    noOfPackets--;
                }
            }

            // qsort(packetBuffer, noOfPackets, sizeof(myTcpPacket), sortPackets);
        }

        if (next_seqno < recvpkt->hdr.seqno){
            if (noOfPackets != MAX){
                packetBuffer[noOfPackets].datasize = recvpkt->hdr.data_size;
                packetBuffer[noOfPackets].seqno = recvpkt->hdr.seqno;
                packetBuffer[noOfPackets].data = malloc(recvpkt->hdr.data_size);
                memcpy(packetBuffer[noOfPackets].data, recvpkt->data, recvpkt->hdr.data_size);
                noOfPackets++;
                qsort(packetBuffer, noOfPackets, sizeof(myTcpPacket), sortPackets);
                // VLOG(DEBUG, "%lu, %d, %d   - PACKET BUFFERED", tp.tv_sec, recvpkt->hdr.data_size, recvpkt->hdr.seqno);
            }
        }

        if (wait==1){
            FD_ZERO(&rfds);
            FD_SET(sockfd, &rfds);
            tv.tv_sec = 0;
            tv.tv_usec = 200000;
            activity = select(sockfd + 1, &rfds, NULL, NULL, &tv);

            if (activity == -1){
                printf("ERROR IN SELECT FUNCTION\n");
                return 0;
            }

            if (activity){
                wait = 0;
                continue;
            }
        }

        wait = 1;
        sndpkt = make_packet(0);
        sndpkt->hdr.ackno = next_seqno;
        sndpkt->hdr.ctr_flags = ACK;
        if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0, 
                (struct sockaddr *) &clientaddr, clientlen) < 0) {
            error("ERROR in sendto");
        }
    }

    return 0;
}
