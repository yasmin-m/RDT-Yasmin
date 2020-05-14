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

int sortPackets(const void *a, const void *b) {
    tcp_packet *a_ = (tcp_packet *) a; 
    tcp_packet *b_ = (tcp_packet *) b;

    // We double check all of the 3 possible cases where one of the packets
    // is empty, to be completely sure of the ordering we want to impose.
    if (a_ == NULL && b_ != NULL)
    {
        return -1;
    }
    // If a_ is NULL and b isn't, return that a is less than b
    if ( a_ != NULL && b_== NULL)
    {
        return 1;
    }
    if (a_ == NULL && b_ == NULL)
    {
        return 0;
    }     
     
    // b is stored earlier in the array than a if positive
    return a_->hdr.seqno -b_->hdr.seqno; 
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

    tcp_packet *packetBuffer[MAX];
    for(int i=0; i<MAX; i++){
        packetBuffer[i] = NULL;
    }

    clientlen = sizeof(clientaddr);
    int next_seqno = 0; //number of next packet expected

    // int started = 0;
    int noOfPackets = 0;
    // clock_t start, stop;

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
            VLOG(DEBUG, "%lu, %d, %d   - DUPLICATE PACKET", tp.tv_sec, recvpkt->hdr.data_size, recvpkt->hdr.seqno);
        }

        else{
            VLOG(DEBUG, "%lu, %d, %d   - PACKET BUFFERED", tp.tv_sec, recvpkt->hdr.data_size, recvpkt->hdr.seqno);
            packetBuffer[noOfPackets] = recvpkt;
            noOfPackets++;
            qsort(packetBuffer, noOfPackets, sizeof(tcp_packet), sortPackets);

            int x=noOfPackets;
            for(int i=0; i<x; i++){
                printf("noOfPackets: %d\n", noOfPackets);
                if(packetBuffer[i]==NULL){
                    printf("NULL\n");
                }
                if (next_seqno == packetBuffer[i]->hdr.seqno){
                    VLOG(DEBUG, "%lu, %d, %d   - FROM BUFFER", tp.tv_sec, packetBuffer[i]->hdr.data_size, packetBuffer[i]->hdr.seqno);
                    fseek(fp, packetBuffer[i]->hdr.seqno, SEEK_SET);
                    fwrite(packetBuffer[i]->data, 1, packetBuffer[i]->hdr.data_size, fp);
                    next_seqno = packetBuffer[i]->hdr.data_size + packetBuffer[i]->hdr.seqno;
                    packetBuffer[i] = NULL;
                    noOfPackets--;
                }
            }

        }

        sndpkt = make_packet(0);
        sndpkt->hdr.ackno = next_seqno;
        sndpkt->hdr.ctr_flags = ACK;
        if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0, 
                (struct sockaddr *) &clientaddr, clientlen) < 0) {
            error("ERROR in sendto");
        }

        // if (next_seqno == recvpkt->hdr.seqno){
        //     VLOG(DEBUG, "%lu, %d, %d", tp.tv_sec, recvpkt->hdr.data_size, recvpkt.hdr.seqno);
        //     fseek(fp, recvpkt.hdr.seqno, SEEK_SET);
        //     fwrite(recvpkt.data, 1, recvpkt.hdr.data_size, fp);
        //     next_seqno = recvpkt.hdr.seqno + recvpkt.hdr.data_size;

        //     int x=noOfPackets;
        //     for(int i=0; i<x; i++){
        //         if (next_seqno == packetBuffer[i].hdr.seqno){
        //             VLOG(DEBUG, "%lu, %d, %d   - FROM BUFFER", tp.tv_sec, packetBuffer[i].hdr.data_size, packetBuffer[i].hdr.seqno);
        //             fseek(fp, packetBuffer[i].hdr.seqno, SEEK_SET);
        //             fwrite(packetBuffer[i].data, 1, packetBuffer[i].hdr.data_size, fp);
        //             next_seqno = packetBuffer[i].hdr.data_size + packetBuffer[i].hdr.seqno;
        //             packetBuffer[i] = (tcp_packet){0};
        //             packetBuffer[i].hdr.data_size = -1;
        //             noOfPackets--;
        //         }
        //     }

        //     qsort(packetBuffer, noOfPackets, sizeof(tcp_packet), sortPackets);
        // }

        // if (next_seqno < recvpkt.hdr.seqno){
        //     packetBuffer[noOfPackets] = recvpkt;
        //     noOfPackets++;
        //     qsort(packetBuffer, noOfPackets, sizeof(tcp_packet), sortPackets);
        //     VLOG(DEBUG, "%lu, %d, %d   - PACKET BUFFERED", tp.tv_sec, recvpkt.hdr.data_size, recvpkt.hdr.seqno);
        // }

        // sndpkt = make_packet(0);
        // sndpkt->hdr.ackno = next_seqno;
        // sndpkt->hdr.ctr_flags = ACK;
        // if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0, 
        //         (struct sockaddr *) &clientaddr, clientlen) < 0) {
        //     error("ERROR in sendto");
        // }
    }

    return 0;
}
