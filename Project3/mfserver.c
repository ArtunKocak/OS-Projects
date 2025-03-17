#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <malloc.h>
#include <assert.h>
#include <string.h>
#include "mf.h"
#include <signal.h>

// Signal handler function for termination requests
void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        printf("Termination signal received. Cleaning up...\n");
        mf_destroy(); // Call mf_destroy() for cleanup
        exit(0);
    }
}

int main(int argc, char *argv[]) {
    printf("mfserver pid=%d\n", (int)getpid());

    // Register the signal handler function
    signal(SIGINT, signal_handler); // Handle Ctrl-C
    signal(SIGTERM, signal_handler); // Handle termination signal

    mf_init(); // Initialize MF library

    // Perform any additional initialization if needed

    while (1) {
        sleep(1000); // Sleep to keep the server running
    }

    return 0; // This line will not be reached
}



