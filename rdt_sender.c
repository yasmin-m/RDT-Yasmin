#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <assert.h>

#include"packet.h"
#include"common.h"

#define STDIN_FD    0
#define ALPHA 0.125
#define BETA 0.25
#define MAXPACKETS 64; 

int next_seqno=0;
int send_base=0;
float window_size = 1;
int ssthresh = MAXPACKETS;
int slowStart = 1;

int getRTT = 1;
float estimatedRTT = 1000;
float deviation = 50;

int sockfd, serverlen;
struct sockaddr_in serveraddr;
struct itimerval timer; 
tcp_packet *sndpkt;
tcp_packet *rsndpkt;
tcp_packet *recvpkt;
sigset_t sigmask;

void send_packet(int dataSize, int seqno, char *data){
    sndpkt = make_packet(dataSize);
    memcpy(sndpkt->data, data, dataSize);
    sndpkt->hdr.seqno = seqno;

    if(sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0, 
                ( const struct sockaddr *)&serveraddr, serverlen) < 0)
    {
        error("sendto");
    }

    free(sndpkt);
}

// typedef struct packet_info{
//     int data_size;
//     int seqno;
// }

void resend_packets(int sig)
{
    if (sig == SIGALRM)
    {
        //Resend all packets range between 
        //sendBase and nextSeqNum
        // VLOG(INFO, "Timout happend, resending packet %d", rsndpkt->hdr.seqno);
        if(sendto(sockfd, rsndpkt, TCP_HDR_SIZE + get_data_size(rsndpkt), 0, 
                    ( const struct sockaddr *)&serveraddr, serverlen) < 0)
        {
            error("sendto");
        }
        ssthresh = (((window_size/2) > (2)) ? (window_size/2) : (2));
        window_size = 1;
        slowStart = 1;
        getRTT = 0;
        // estimatedRTT=2*estimatedRTT;

    }
}


void start_timer()
{
    sigprocmask(SIG_UNBLOCK, &sigmask, NULL);
    setitimer(ITIMER_REAL, &timer, NULL);
}


void stop_timer()
{
    sigprocmask(SIG_BLOCK, &sigmask, NULL);
}


/*
 * init_timer: Initialize timeer
 * delay: delay in milli seconds
 * sig_handler: signal handler function for resending unacknoledge packets
 */
void init_timer(int delay, void (*sig_handler)(int)) 
{
    signal(SIGALRM, resend_packets);
    timer.it_interval.tv_sec = delay / 1000;    // sets an interval of the timer
    timer.it_interval.tv_usec = (delay % 1000) * 1000;  
    timer.it_value.tv_sec = delay / 1000;       // sets an initial value
    timer.it_value.tv_usec = (delay % 1000) * 1000;

    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGALRM);
}


int main (int argc, char **argv)
{
    int portno, len;
    int next_seqno;
    char *hostname;
    char pktbuffer[DATA_SIZE];
    char readbuffer[DATA_SIZE];
    FILE *fp;

    /* check command line arguments */
    if (argc != 4) {
        fprintf(stderr,"usage: %s <hostname> <port> <FILE>\n", argv[0]);
        exit(0);
    }
    hostname = argv[1];
    portno = atoi(argv[2]);
    fp = fopen(argv[3], "r");
    if (fp == NULL) {
        error(argv[3]);
    }

    /* socket: create the socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");


    /* initialize server server details */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serverlen = sizeof(serveraddr);

    /* covert host into network byte order */
    if (inet_aton(hostname, &serveraddr.sin_addr) == 0) {
        fprintf(stderr,"ERROR, invalid host %s\n", hostname);
        exit(0);
    }

    /* build the server's Internet address */
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(portno);

    assert(MSS_SIZE - TCP_HDR_SIZE > 0);
    // init_timer(150, resend_packets);

    //Stop and wait protocol
    next_seqno = 0;
    int last_packet_sent = 0;
    int packets_in_flight = 0;
    int end_of_file = 0;
    int file_length = 0;
    int break_all = 0;
    
    int duplicateAcks = 0;

    int sampleRTT;

    int startTimer = 1;
    clock_t start, stop;

    while (break_all == 0)
    {
        //send_base = next_seqno;
        printf("\nCURRENT WINDOW SIZE: %.2f\n", (window_size));

        while ((packets_in_flight < ((int) window_size)) && (end_of_file==0)){
            fseek(fp, last_packet_sent, SEEK_SET);
            len = fread(readbuffer, 1, DATA_SIZE, fp);
            if ( len <= 0)
            {
                printf("End of file has been reached\n");
                file_length = last_packet_sent;
                end_of_file=1;
                break;
            }

            send_packet(len, last_packet_sent, readbuffer); 
            last_packet_sent += len;          

            // VLOG(DEBUG, "Sending packet %d to %s", 
                    // last_packet_sent, inet_ntoa(serveraddr.sin_addr));
            
            packets_in_flight++;
        }
        
        fseek(fp, send_base, SEEK_SET);
        len = fread(readbuffer, 1, DATA_SIZE, fp);
        
        free(rsndpkt);
        rsndpkt = make_packet(len);
        memcpy(rsndpkt->data, readbuffer, len);
        rsndpkt->hdr.seqno = send_base;

        next_seqno = send_base + len;
        // fseek(fp, send_base, SEEK_SET);
        do {
            if (startTimer){
                float delay = (((estimatedRTT + 4*deviation) > (1000)) ? (estimatedRTT + 4*deviation) : (1000));
                init_timer(delay, resend_packets);
                start = clock();
                // printf("TIMEOUT DELAY: %.3f\n", delay);
                start_timer();
            }
            
            //ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
            //struct sockaddr *src_addr, socklen_t *addrlen);

            if(recvfrom(sockfd, pktbuffer, MSS_SIZE, 0,
                        (struct sockaddr *) &serveraddr, (socklen_t *)&serverlen) < 0)
            {
                error("recvfrom");
            }

            recvpkt = (tcp_packet *)pktbuffer;
            // printf("ACK no: %d \n", recvpkt->hdr.ackno);
            if (recvpkt->hdr.ackno < 0){
                stop_timer();
                break_all=1;
                break;
            }

            if (recvpkt->hdr.ackno == file_length){
                stop_timer();
                startTimer=1;
                sndpkt = make_packet(0);
                sendto(sockfd, sndpkt, TCP_HDR_SIZE,  0,
                        (const struct sockaddr *)&serveraddr, serverlen);
                break;
            }

            if (recvpkt->hdr.ackno >= next_seqno){
                stop_timer();
                startTimer=1;
                if (getRTT){
                    stop = clock();

                    sampleRTT = 1000*((stop-start)/CLOCKS_PER_SEC);
                    estimatedRTT = (1-ALPHA)*estimatedRTT + ALPHA*sampleRTT;
                    deviation = (1-BETA)*deviation + BETA*abs(sampleRTT - estimatedRTT);
                }
                else{
                    getRTT = 1;
                }
                // printf("Packet Received\n");
                if (slowStart){
                    window_size++;
                    if (window_size == ssthresh){
                        slowStart=0;
                    }
                }
                else{
                    window_size += 1/window_size;
                }
                assert(get_data_size(recvpkt) <= DATA_SIZE);
                
                packets_in_flight--;
                while(next_seqno != recvpkt->hdr.ackno){
                    packets_in_flight--;
                    len = fread(readbuffer, 1, DATA_SIZE, fp);
                    next_seqno += len;
                }
                send_base = recvpkt->hdr.ackno;
                duplicateAcks = 0;
                break;
            }

            // printf("Expecting ACK %d \n", next_seqno);
            duplicateAcks++;
            startTimer=0;
            if (duplicateAcks >= 3){
                stop_timer();
                startTimer=1;
                // VLOG(DEBUG, "Resending packet %d to %s", send_base, inet_ntoa(serveraddr.sin_addr));
                if(sendto(sockfd, rsndpkt, TCP_HDR_SIZE + get_data_size(rsndpkt), 0, 
                            ( const struct sockaddr *)&serveraddr, serverlen) < 0)
                {
                    error("sendto");
                }
                duplicateAcks = 0;
                ssthresh = (((window_size/2) > (2)) ? (window_size/2) : (2));
                window_size = 1;
                slowStart = 1;
                getRTT = 0;
            }

        } while(1);
    }

    return 0;

}



