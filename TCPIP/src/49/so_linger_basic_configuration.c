#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

// Demonstrates three different SO_LINGER configurations

void set_linger_default(int sockfd) {
    struct linger sl;
    sl.l_onoff = 0;  // Disable linger
    sl.l_linger = 0;
    
    if (setsockopt(sockfd, SOL_SOCKET, SO_LINGER, &sl, sizeof(sl)) < 0) {
        perror("setsockopt SO_LINGER (default)");
    } else {
        printf("SO_LINGER: Default behavior (l_onoff=0)\n");
        printf("  - close() returns immediately\n");
        printf("  - TCP sends data in background\n");
        printf("  - Normal TIME_WAIT state\n\n");
    }
}

void set_linger_graceful(int sockfd, int timeout_seconds) {
    struct linger sl;
    sl.l_onoff = 1;  // Enable linger
    sl.l_linger = timeout_seconds;
    
    if (setsockopt(sockfd, SOL_SOCKET, SO_LINGER, &sl, sizeof(sl)) < 0) {
        perror("setsockopt SO_LINGER (graceful)");
    } else {
        printf("SO_LINGER: Graceful linger (l_onoff=1, l_linger=%d)\n", 
               timeout_seconds);
        printf("  - close() blocks up to %d seconds\n", timeout_seconds);
        printf("  - Waits for data to be sent and ACKed\n");
        printf("  - Returns -1 on timeout\n\n");
    }
}

void set_linger_abortive(int sockfd) {
    struct linger sl;
    sl.l_onoff = 1;  // Enable linger
    sl.l_linger = 0; // Zero timeout = abortive close
    
    if (setsockopt(sockfd, SOL_SOCKET, SO_LINGER, &sl, sizeof(sl)) < 0) {
        perror("setsockopt SO_LINGER (abortive)");
    } else {
        printf("SO_LINGER: Abortive close (l_onoff=1, l_linger=0)\n");
        printf("  - Sends RST instead of FIN\n");
        printf("  - Discards pending data\n");
        printf("  - Avoids TIME_WAIT state\n");
        printf("  - WARNING: Can lose data!\n\n");
    }
}

int main() {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }
    
    printf("=== SO_LINGER Configuration Examples ===\n\n");
    
    // Demonstrate each configuration
    set_linger_default(sockfd);
    set_linger_graceful(sockfd, 10);
    set_linger_abortive(sockfd);
    
    // Query current SO_LINGER setting
    struct linger current_linger;
    socklen_t optlen = sizeof(current_linger);
    
    if (getsockopt(sockfd, SOL_SOCKET, SO_LINGER, 
                   &current_linger, &optlen) == 0) {
        printf("Current SO_LINGER setting:\n");
        printf("  l_onoff  = %d\n", current_linger.l_onoff);
        printf("  l_linger = %d seconds\n", current_linger.l_linger);
    }
    
    close(sockfd);
    return 0;
}