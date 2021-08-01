/* Shir Frenkel hw 5 OS *
 * ID : 318879830
 * */

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <stdint.h>  // for uint32_t

// ----------------------------- HELPER FUNCTIONS -----------------------------

long int file_size(FILE *fd){
    long int size, prev;
    prev = ftell(fd);
    if (prev == -1L){
        perror("ftell failed");
        exit(1);
    }
    if (fseek(fd, 0L, SEEK_END) != 0){
        perror("fseek failed");
        exit(1);
    }
    size = ftell(fd);
    if (size == -1L){
        perror("ftell failed");
        exit(1);
    }
    // return to prev location
    if (fseek(fd, prev, SEEK_SET) != 0){
        perror("fseek failed");
        exit(1);
    }
    return size;
}


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
        //printf("Client: wrote %d bytes\n", nsent);

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
        //printf("Client: read %d bytes\n", nread);

        totalread  += nread;
        notreadyet -= nread;
    }
}


// ----------------------------- MAIN -----------------------------

int main(int argc, char *argv[])
{
    char *file_path, *msg_buff;
    uint32_t msg_size, msg_size_net_order, number_of_printable_chr;
    FILE *fd;
    uint16_t srv_port;
    int rval;
    int sockfd = -1;

    struct sockaddr_in serv_sockaddr; // where we Want to get to
    memset(&serv_sockaddr, 0, sizeof(serv_sockaddr));  // needed


    // ----------- inputs handling -----------
    // validate that the correct number of command line arguments
    if (argc != 4){
        errno = EINVAL;
        perror("invalid number of arguments");
        exit(1);
    }

    // argv[1]: server’s IP address
    // set the IP address of serv_sockaddr to the specified server IP
    rval = inet_pton(AF_INET, argv[1], &(serv_sockaddr.sin_addr.s_addr));
    if (rval <= 0){
        if (rval == 0)
            fprintf(stderr, "server’s IP address argument is not in presentation format, Error: %s\n", strerror(EINVAL));
        else
            perror("inet_pton faild");
        exit(1);
    }

    // argv[2]: server’s port (assume a 16-bit unsigned integer)
    srv_port = (uint16_t) atol(argv[2]);
    if (srv_port == 0){
        perror("atol failed");
        exit(1);
    }

    // argv[3]: path of the file to send
    file_path = argv[3];

    // ----------- file handling -----------
    // open the file
    fd = fopen(file_path, "r");
    if (fd == NULL){
        perror("fopen failed");
        exit(1);
    }

    // get file size (in bytes)
    // assumes that the size of the file can be represented with a 32-bit unsigned integer
    msg_size = (uint32_t)file_size(fd);


    // write file content to msg_buff
    msg_buff = malloc(msg_size);
    if (msg_buff == NULL){
        perror("malloc failed");
        exit(1);
    }
    if (fread(msg_buff, 1, msg_size, fd) != msg_size){
        perror("fread failed");
        exit(1);
    }

    fclose(fd);

    // ----------- create a TCP connection -----------

    if( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("socket failed");
        exit(1);
    }

    // define serv_sockaddr
    serv_sockaddr.sin_family = AF_INET;
    serv_sockaddr.sin_port = htons(srv_port);  // Note: htons for endiannes

    //printf("Client: connecting...\n");
    // connect socket to the target address
    if( connect(sockfd,
              (struct sockaddr*) &serv_sockaddr,
              sizeof(serv_sockaddr)) < 0)
    {
        perror("connect failed");
        exit(1);
    }


    // ----------- write & read over TCP connection -----------
    // send the number of bytes that will be transferred (msg_size)
    msg_size_net_order = htonl(msg_size);  // change to network byte order
    write_from_buff(sockfd, (char*) &msg_size_net_order, sizeof(msg_size_net_order));

    // send the file’s content
    write_from_buff(sockfd, msg_buff, msg_size);

    // read data from server into number_of_printable_chr
    read_to_buff(sockfd, (char*) &number_of_printable_chr, sizeof(number_of_printable_chr));
    number_of_printable_chr = ntohl(number_of_printable_chr);
    printf("# of printable characters: %u\n", number_of_printable_chr);

    close(sockfd);
    exit(0);
}
