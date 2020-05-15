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
#define ALPHA 0.125 //used for calculating estimatedRTT
#define BETA 0.25 //used for calculating RTT deviation
#define MAXPACKETS 64; //initial ssthreshold

int next_seqno=0;
int send_base=0;

float window_size = 1; //cwnd
int ssthresh = MAXPACKETS; //ssthresold
int slowStart = 1; //boolean telling me whether or not to be in slow start or congestion avoidance

int getRTT = 1; //if there was no retransmission, then get RTT=1
float estimatedRTT = 1000; //estimated RTT with high initial value
float deviation = 50; //estimated deviation

FILE *cwnd; //file that will store CWND

struct timeval beginning, now;
long mtime, secs, usecs;

int sockfd, serverlen;
struct sockaddr_in serveraddr;
struct itimerval timer; 

tcp_packet *sndpkt;
tcp_packet *rsndpkt; //used specifically to hold the packet at the base of the window to be sent
tcp_packet *recvpkt;

sigset_t sigmask;

//helper function for sending packets (mostly unused)
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

void resend_packets(int sig)
{
    if (sig == SIGALRM)
    {
        VLOG(INFO, "Timout happend, resending packet %d", rsndpkt->hdr.seqno);
        if(sendto(sockfd, rsndpkt, TCP_HDR_SIZE + get_data_size(rsndpkt), 0, 
                    ( const struct sockaddr *)&serveraddr, serverlen) < 0)
        {
            error("sendto");
        }

        //update ssthreshold, set window size to 1, reenter slow start, and dont get RTT for retransmitted package
        ssthresh = (((window_size/2) > (2)) ? (window_size/2) : (2));
        window_size = 1;
        slowStart = 1;
        getRTT = 0;

        gettimeofday(&now, NULL); //get time now
        secs = now.tv_sec - beginning.tv_sec; //get seconds
        usecs = now.tv_usec - beginning.tv_usec; //get microseconds
        mtime = ((secs)*1000 + usecs/1000.0); //get time in milliseconds
        fprintf(cwnd, "%ld,%d,%d\n", mtime, (int) window_size, ssthresh); //store in file
        //if a timeout happened, increase the timer by a factor of 2, known as timer back-off, according to RFC6298
        estimatedRTT=2*estimatedRTT;

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
    char pktbuffer[DATA_SIZE]; //stores buffer data from the recvpkt
    char readbuffer[DATA_SIZE]; //stores buffer data from the reading of files
    FILE *fp;
    cwnd = fopen("CWND.csv", "w"); //open file that stores CWND data

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

    //initial seqno is zero
    next_seqno = 0;

    //used to keep track of the end of the cwnd
    int last_packet_sent = 0;

    //number of packets currently sent but unacked
    int packets_in_flight = 0;

    //whether or not end of file has been reached
    int end_of_file = 0;

    //stores the total length of the file
    int file_length = 0;

    //boolean for exiting outside loop
    int break_all = 0;
    
    //number of duplicate acks received
    int duplicateAcks = 0;

    //sample RTT
    int sampleRTT;

    //boolean whether or not to start the timer on the next cycle
    int startTimer = 1;
    struct timeval start, stop; //start and stop times for rtt

    //iterate until I set break all to 1
    gettimeofday(&beginning, NULL);
    
    gettimeofday(&now, NULL); //get time now
    secs = now.tv_sec - beginning.tv_sec; //get seconds
    usecs = now.tv_usec - beginning.tv_usec; //get microseconds
    mtime = ((secs)*1000 + usecs/1000.0); //get time in milliseconds
    fprintf(cwnd, "%ld,%d,%d\n", mtime, (int) window_size, ssthresh); //store in file

    while (break_all == 0)
    {
        while ((packets_in_flight < ((int) window_size)) && (end_of_file==0)){

            //read file and get data
            fseek(fp, last_packet_sent, SEEK_SET);
            len = fread(readbuffer, 1, DATA_SIZE, fp);

            //if the end of file has been reached, store its size and set EoF to 1
            if ( len <= 0)
            {
                printf("End of file has been reached\n");
                file_length = last_packet_sent;
                end_of_file=1;
                break;
            }

            //send the packet with size len and data in the readbuffer, and seqno equal to lastpacketsent
            send_packet(len, last_packet_sent, readbuffer);

            VLOG(DEBUG, "Sending packet %d to %s", 
                    last_packet_sent, inet_ntoa(serveraddr.sin_addr));

            //update last packet send
            last_packet_sent += len;          
            
            //increase the number of packets in flight
            packets_in_flight++;
        }
        
        //go back to the send base
        fseek(fp, send_base, SEEK_SET);
        len = fread(readbuffer, 1, DATA_SIZE, fp);
        
        //free rsndpkt, then make a new one with the data from the send base
        free(rsndpkt);
        rsndpkt = make_packet(len);
        memcpy(rsndpkt->data, readbuffer, len);
        rsndpkt->hdr.seqno = send_base;

        //update next expected seqno
        next_seqno = send_base + len;
        // fseek(fp, send_base, SEEK_SET);
        do {

            //if you haven't received a duplicate ack and this is the first cycle, start the timer
            if (startTimer){
                //the delay is equal to estimated RTT + 4*deviation. A minimum delay of 500ms is set according to RFC6298.
                //This is half the value specified by them, as it works better for this network
                float delay = ((estimatedRTT + 4*deviation) > (500)) ? (estimatedRTT + 4*deviation) : (500);
                //initialize the timer
                init_timer(delay, resend_packets);
                //start a clock that will measure the RTT
                gettimeofday(&start, NULL);
                printf("\nTIMEOUT DELAY: %.3f\n", delay);

                //start the timer
                start_timer();
            }
            
            //recv file
            if(recvfrom(sockfd, pktbuffer, MSS_SIZE, 0,
                        (struct sockaddr *) &serveraddr, (socklen_t *)&serverlen) < 0)
            {
                error("recvfrom");
            }

            recvpkt = (tcp_packet *)pktbuffer;
            printf("ACK no: %d \n", recvpkt->hdr.ackno);
            // if there's a negative acknowledgement, that indicates that the receiver received the last packet.
            //in that case break everything
            if (recvpkt->hdr.ackno < 0){
                stop_timer();
                fclose(cwnd);
                fclose(fp);
                free(rsndpkt);
                break_all=1;
                break;
            }

            //if the last packet is received, send an empty packet, stop the timer and start a timer for the final acknowledgement
            if (recvpkt->hdr.ackno == file_length){
                stop_timer();
                startTimer=1;
                sndpkt = make_packet(0);
                sendto(sockfd, sndpkt, TCP_HDR_SIZE,  0,
                        (const struct sockaddr *)&serveraddr, serverlen);
                break;
            }

            //if the ack is greater than or equal to the next expected sequence number
            if (recvpkt->hdr.ackno >= next_seqno){
                stop_timer(); //stop the timer
                startTimer=1; //set the timer to start on the next cycle
                if (getRTT){ //if it wasn't a retransmission, calculate the time taken for the trip
                    
                    gettimeofday(&stop, NULL);
                    secs = stop.tv_sec - start.tv_sec; //get seconds
                    usecs = stop.tv_usec - start.tv_usec; //get microseconds
                    sampleRTT = ((secs)*1000 + usecs/1000.0);; //get time in ms
                    estimatedRTT = (1-ALPHA)*estimatedRTT + ALPHA*sampleRTT; //calculate new estimateRTT
                    deviation = (1-BETA)*deviation + BETA*abs(sampleRTT - estimatedRTT); //calculate new deviation
                }
                //otherwise reset get RTT
                else{
                    getRTT = 1;
                }
                printf("Packet Received\n");
                //If in slow start, increase window size by 1
                if (slowStart){
                    window_size++;

                    gettimeofday(&now, NULL); //get time now
                    secs = now.tv_sec - beginning.tv_sec; //get seconds
                    usecs = now.tv_usec - beginning.tv_usec; //get microseconds
                    mtime = ((secs)*1000 + usecs/1000.0); //get time in milliseconds
                    fprintf(cwnd, "%ld,%d,%d\n", mtime, (int) window_size, ssthresh); //store in file
                    
                    if (window_size == ssthresh){ //if ssthreshold is reached, set slow start to 0, and enter congestion avoidance
                        slowStart=0;
                    }
                }
                else{ //if in congestion avoidance, update window by 1/cwnd
                    window_size += 1/window_size;

                    gettimeofday(&now, NULL); //get time now
                    secs = now.tv_sec - beginning.tv_sec; //get seconds
                    usecs = now.tv_usec - beginning.tv_usec; //get microseconds
                    mtime = ((secs)*1000 + usecs/1000.0); //get time in milliseconds
                    fprintf(cwnd, "%ld,%d,%d\n", mtime, (int) window_size, ssthresh); //store in file
                }
                assert(get_data_size(recvpkt) <= DATA_SIZE);
                
                packets_in_flight--; //decrement the number of packets in flight
                while(next_seqno != recvpkt->hdr.ackno){ //if it was a cumulative ack, decrement the window size by number of packets acked
                    packets_in_flight--;
                    len = fread(readbuffer, 1, DATA_SIZE, fp);
                    next_seqno += len;
                }
                send_base = recvpkt->hdr.ackno; //slide the window forward
                duplicateAcks = 0; //reset the number of duplicate acknowledgements
                break;
            }

            printf("Expecting ACK %d \n", next_seqno);
            // otherwise its a duplicate acknowledgement
            duplicateAcks++;
            // do not start the timer on the next round
            startTimer=0;

            //if there are 3 duplicate acknowledgements
            if (duplicateAcks >= 3){
                //stop the timer and start the timer on next cycle
                stop_timer();
                startTimer=1;
                // fast retransmit

                VLOG(DEBUG, "Resending packet %d to %s", send_base, inet_ntoa(serveraddr.sin_addr));
                if(sendto(sockfd, rsndpkt, TCP_HDR_SIZE + get_data_size(rsndpkt), 0, 
                            ( const struct sockaddr *)&serveraddr, serverlen) < 0)
                {
                    error("sendto");
                }
                // reset the number of duplicate acknowledgements
                duplicateAcks = 0;
                //update the ssthreshold, reenter slow start, and set window size to 1. Dont get RTT for retransmitted package
                ssthresh = (((window_size/2) > (2)) ? (window_size/2) : (2));
                window_size = 1;
                slowStart = 1;
                getRTT = 0;

                gettimeofday(&now, NULL); //get time now
                secs = now.tv_sec - beginning.tv_sec; //get seconds
                usecs = now.tv_usec - beginning.tv_usec; //get microseconds
                mtime = ((secs)*1000 + usecs/1000.0); //get time in milliseconds
                fprintf(cwnd, "%ld,%d,%d\n", mtime, (int) window_size, ssthresh); //store in file
            }

        } while(1);
    }

    return 0;

}



