/* Shir Frenkel hw 5 OS *
 * ID : 318879830
 * */

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <stdint.h>  // for uint32_t
#include <signal.h>


void server_exit();

// ------------------- GLOBAL VARIABLES -------------------
// processing_a_client_flag = 1 in the period of time starting when accept() returns the client’s socket
// and until closing its socket, otherwise, processing_a_client_flag = 0
int processing_a_client_flag = 0;
int terminate_flag = 0;  // when the server receives a SIGUSR1 signal, terminate_flag <-- 1

// ------------------- pcc_total global data structure -------------------
uint32_t pcc_total[95];

void pcc_total_init(){
    int i;
    for (i=0; i<95; i++)
        pcc_total[i] = 0;
}

void pcc_total_increment(int byte_val){
    // @pre: 32 <= byte_val <= 126
    pcc_total[byte_val - 32]++;
}

uint32_t pcc_total_get_count(int byte_val){
    // @pre: 32 <= byte_val <= 126
    return pcc_total[byte_val - 32];
}

// ----------------------------- SIGUSR1 HANDLER -----------------------------

void SIGUSR1_handler(int signum){
    /* SIGUSR1 handling function
     * This function receives the signal number as its only argument
     * (as required in https://manpages.ubuntu.com/manpages/bionic/man2/sigaction.2.html ) */

    if (processing_a_client_flag == 1){
        // the server is processing a client, so we set terminate_flag = 1
        // and the server will exit after finishing processing that client
        terminate_flag = 1;
        return;
    }
    server_exit();
}


// ----------------------------- HELPER FUNCTIONS -----------------------------

void write_from_buff(int fd, char* buff, uint32_t len_in_bytes){
    int totalsent, notwritten, nsent;

    totalsent = 0;
    notwritten = len_in_bytes;

    // keep looping until nothing left to write
    while (notwritten > 0)
    {
        // notwritten = how much we have left to write
        // totalsent  = how much we've written so far
        // nsent = how much we've written in last write() call */
        nsent = write(fd,
                      buff + totalsent,
                      notwritten);
        // check if error occurred
        if (nsent < 0){
            perror("write failed");
            exit(1);
        }
        //printf("Server: wrote %d bytes\n", nsent);

        totalsent  += nsent;
        notwritten -= nsent;
    }
}

void read_to_buff(int fd, char* buff, uint32_t len_in_bytes){
    int totalread, notreadyet, nread;

    totalread = 0;
    notreadyet = len_in_bytes;

    // keep looping until nothing left to read
    while (notreadyet > 0)
    {
        // notreadyet = how much we have left to read
        // totalread  = how much we've read so far
        // nread = how much we've read in last read() call */
        nread = read(fd,
                     buff + totalread,
                     notreadyet);
        // check if error occurred
        if (nread < 0){
            perror("read failed");
            exit(1);
        }
        //printf("Server: read %d bytes\n", nread);

        totalread  += nread;
        notreadyet -= nread;
    }
}

void server_exit(){
    int char_val;
    for (char_val=32; char_val<=126; char_val++){
        printf("char '%c' : %u times\n", char_val, pcc_total_get_count(char_val));
    }
    exit(0);
}

// ----------------------------- MAIN -----------------------------

int main(int argc, char *argv[])
{
    int optval;
    char *msg_buff;
    uint32_t msg_size, number_of_printable_chr, i;
    struct sigaction sa;
    int listenfd  = -1;
    int connfd    = -1;

    // ----------- init SIGUSR1 handler -----------

    // define full sa_mask - delay handling of signals from sa_mask until sa.sa_handler finishes (not mandatory)
    sigfillset(&sa.sa_mask);
    // assign pointer to the handler function
    sa.sa_handler = &SIGUSR1_handler;
    // SA_RESTART :  returning from the handler resumes the library function (such as open, read or write)
    sa.sa_flags = SA_RESTART;
    // register the handler
    if( 0 != sigaction(SIGUSR1, &sa, NULL) )
    {
        perror("signal handle registration failed");
        exit(1);
    }

    // ----------- initialize pcc_total -----------
    pcc_total_init();

    // ----------- inputs handling -----------
    // validate that the correct number of command line arguments
    if (argc != 2){
        errno = EINVAL;
        perror("invalid number of arguments");
        exit(1);
    }

    struct sockaddr_in serv_addr;
    socklen_t addrsize = sizeof(struct sockaddr_in );

    if((listenfd = socket( AF_INET, SOCK_STREAM, 0)) < 0){
        perror("socket failed");
        exit(1);
    }

    optval = 1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int)) != 0){
        perror("setsockopt failed");
        exit(1);
    }
    memset( &serv_addr, 0, addrsize );

    serv_addr.sin_family = AF_INET;
    // INADDR_ANY = any local machine address
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(atoi(argv[1]));  // argv[1]: server’s port (assume a 16-bit unsigned integer)

    if( 0 != bind( listenfd,
                 (struct sockaddr*) &serv_addr,
                 addrsize ) )
    {
        perror("bind failed");
        exit(1);
    }

    // Listen to incoming TCP connections on the specified server port
    if( 0 != listen( listenfd, 10 ) )
    {
        perror("listen failed");
        exit(1);
    }

    while( 1 )
    {
    // Accept a connection
    connfd = accept( listenfd, NULL, NULL);
    processing_a_client_flag = 1;

    if( connfd < 0 )
    {
      printf("\n Error : Accept Failed. %s \n", strerror(errno));
      exit(1);
    }

    // ----------- write & read over TCP connection -----------

    // receive the number of bytes that will be transferred (msg_size)
    read_to_buff(connfd, (char*) &msg_size, sizeof(msg_size));
    msg_size = ntohl(msg_size);  // change to host byte order

    // receive the message
    msg_buff = malloc(msg_size);
    if (msg_buff == NULL){
        perror("malloc failed");
        exit(1);
    }
    read_to_buff(connfd, msg_buff, msg_size);

    // computes the message printable character count
    number_of_printable_chr = 0;
    for (i=0; i<msg_size; i++){
        if (32 <= msg_buff[i] && msg_buff[i] <= 126)
            number_of_printable_chr++;
    }

    // send printable character count to client (number_of_printable_chr)
    number_of_printable_chr = htonl(number_of_printable_chr);
    write_from_buff(connfd, (char*) &number_of_printable_chr, sizeof(number_of_printable_chr));

    // updates the pcc_total global data structure
    for (i=0; i<msg_size; i++){
        if (32 <= msg_buff[i] && msg_buff[i] <= 126)
            pcc_total_increment(msg_buff[i]);
    }

    // close socket
    close(connfd);
    processing_a_client_flag = 0;
    if (terminate_flag == 1)
        server_exit();
    }
}
