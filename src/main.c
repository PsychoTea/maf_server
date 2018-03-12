#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <sys/errno.h>
#include <string.h>
#include "lib/libkern.h"
#include "kern.h"

#define MIN(x,y) (((x)<(y)) ? (x) : (y))

uint64_t kaslr_shift;

int recv_buffer(int sockfd, void* buffer, size_t buffer_len) {
    size_t bytes_read = 0;
    while (bytes_read < buffer_len) {
        ssize_t res = recv(sockfd, buffer + bytes_read, buffer_len - bytes_read, 0);
        if (res <= 0) {
            printf("Failed to recv: %zd\n", res);
            return -1;
        }
        bytes_read += res;
    }
    return 0;
}

int handle_client(int clientfd) {
    // Letting the client know what the slide is, so it can adjust pointers accordingly
    send(clientfd, &kaslr_shift, sizeof(kaslr_shift), 0);
    
    // Command loop
    uint64_t mask = 0xFFFFFFFFFFFFFFFFULL;
    while (1) {
        // Reading the command code
        char command;
        if (recv_buffer(clientfd, &command, sizeof(command)) < 0) {
            printf("Failed to read command : %d\n", errno);
            return -1;
        }
    
        // Is this a read command?
        if (command == 'r') {
            uint64_t addr;
            if (recv_buffer(clientfd, &addr, sizeof(addr)) < 0) {
                printf("Failed to read address : %d\n", errno);
                return -1;
            }
            addr ^= mask;
            uint128_t val = rk128(addr);
            printf("Read - Addr: %016llx, Value : %016llx %016llx\n", addr, ((uint64_t*)&val)[0], ((uint64_t*)&val)[1]);
            ((uint64_t*)&val)[0] ^= mask;
            ((uint64_t*)&val)[1] ^= mask;
            
            send(clientfd, &val, sizeof(val), 0);
        }
        // Is this a read chunk command?
        else if (command == 'c') {
            uint64_t args[2];
            if (recv_buffer(clientfd, &args, sizeof(args)) < 0) {
                printf("Failed to read address : %d\n", errno);
                return -1;
            }
            uint64_t addr = args[0] ^ mask;
            uint64_t size = args[1] ^ mask;
            printf("Read Chunk - Addr: %016llx, Size: %016llx\n", addr, size);
            
            uint8_t* buffer = malloc(size);
            if (!buffer)
                return -1;
            uint64_t off = 0;
            
            //Reading the first unaligned address, if necessary
            uint64_t modulo = addr % sizeof(uint128_t);
            if (modulo != 0) {
                uint128_t first_val = rk128(addr - modulo);
                uint64_t len = MIN(sizeof(uint128_t) - modulo, size);
                memcpy(buffer, (uint8_t*)(&first_val) + modulo, len);
                off += len;
            }
            
            while (off < size) {
                uint128_t val = rk128(addr + off);
                uint64_t len = MIN(size - off, sizeof(uint128_t));
                memcpy(buffer + off, &val, len);
                off += len;
            }
            
            //Obfuscating the block's contents
            for (off=0; off<size; off++)
                buffer[off] ^= 0xFF;
            
            //Writing the chunk back
            ssize_t bytes_sent = 0;
            while (bytes_sent < size)
                bytes_sent += send(clientfd, buffer+bytes_sent, size-bytes_sent, 0);
            free(buffer);
        }
        //Is this a write command?
        else if (command == 'w') {
            uint64_t args[2];
            if (recv_buffer(clientfd, &args, sizeof(args)) < 0) {
                printf("Failed to read args : %d\n", errno);
                return -1;
            }
            args[0] ^= mask;
            args[1] ^= mask;
            printf("Write - Addr: %016llx, Value : %016llx\n", args[0], args[1]);
            wk64(args[0], args[1]);
            uint64_t res = 0;
            send(clientfd, &res, sizeof(res), 0);
        }
        //Is this an exec command?
        else if (command == 'x') {
            // TODO: implement kcall
            // i'm just too lazy to set up a fake iouserclient blah blah...
            // this command will probably break stuff since we're not returning a response
            printf("exec is currently disabled :( \n");
            // uint64_t args[3];
            // if (recv_buffer(clientfd, &args, sizeof(args)) < 0) {
            //     printf("Failed to read args : %d\n", errno);
            //     return -1;
            // }
            // args[0] ^= mask;
            // args[1] ^= mask;
            // args[2] ^= mask;
            // printf("Exec - Func: %016llx, Arg1 : %016llx, Arg2 : %016llx\n", args[0], args[1], args[2]);
            // exec2(args[0], args[1], args[2]);
            // uint64_t res = 0;
            // send(clientfd, &res, sizeof(res), 0);
        }
        //Is this a data race command?
        else if (command == 'f') {
            uint64_t args[2];
            if (recv_buffer(clientfd, &args, sizeof(args)) < 0) {
                printf("Failed to read args : %d\n", errno);
                return -1;
            }
            args[0] ^= mask;
            args[1] ^= mask;
            printf("Race - Addr %016llx, Val : %016llx\n", args[0], args[1]);
            if (args[0] % sizeof(uint64_t) == 0) {
                uint64_t prev = rk64(args[0]);
                wk64(args[0], (args[1] & 0xFFFFFFFF) | (prev & 0xFFFFFFFF00000000));
                wk64(args[0], prev);
            }
            else {
                uint64_t prev = rk64(args[0] - sizeof(uint32_t));
                wk64(args[0] - sizeof(uint32_t), (args[1] & 0xFFFFFFFF00000000) | (prev & 0xFFFFFFFF));
                wk64(args[0] - sizeof(uint32_t), prev);
            }
            uint64_t res = 0;
            send(clientfd, &res, sizeof(res), 0);
        }
        else {
            printf("Unsupported command %u!\n", (unsigned)command);
            return -1;
        }
    }
}

int start_server() {
    int sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        printf("Failed to open socket : %d\n", errno);
        return -1;
    }
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_len = sizeof(sin);
    sin.sin_family = AF_INET;
    sin.sin_port = htons(1337);
    sin.sin_addr.s_addr = INADDR_ANY;
    
    struct ifreq ifr;
    ifr.ifr_addr.sa_family = AF_INET;
    ioctl(sock, SIOCGIFADDR, &ifr);
    printf("IP: %s", inet_ntoa(((struct sockaddr_in*)&ifr.ifr_addr)->sin_addr));
    
    if (bind(sock, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        printf("Failed to bind socket : %d\n", errno);
        close(sock);
        return -1;
    }
    if (listen(sock, 1) < 0) {
        printf("Failed to listen for incoming connections: %d\n", errno);
        close(sock);
        return -1;
    }
    printf("Started listening on %d\n", sock);
    struct sockaddr peer_addr;
    
    // Waiting for clients
    while (1) {
        int clientfd;
        socklen_t peerlen = sizeof(peer_addr);
        if ((clientfd = accept(sock, &peer_addr, &peerlen)) < 0) {
            printf("Failed to accept client : %d\n", errno);
            continue;
        }
        
        // Handle the client
        printf("Got a client! %d\n", clientfd);
        int res = handle_client(clientfd);
        printf("Done with client %d, res %d\n", clientfd, res);
        
        close(clientfd);
    }
}

int main(int argc, char *argv[]) {
    printf("wagwan \n");

    kern_return_t err = host_get_special_port(mach_host_self(), HOST_LOCAL_NODE, 4, &tfp0);
    if (err != KERN_SUCCESS) {
        printf("err getting hgsp4: %s", mach_error_string(err));
        return 0;
    }
    printf("got tfp0: %x \n", tfp0);

    kaslr_shift = get_kernel_base() - 0xFFFFFFF007004000;
    printf("got kaslr_shift: %llx \n", kaslr_shift);

    return start_server();
}
