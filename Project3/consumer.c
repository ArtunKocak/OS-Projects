//// run consumer first, then producer.

#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "mf.h"

char mqname[32] = "mq1";

int
main(int argc, char **argv)
{
    int qid;
    char recvbuffer[MAX_DATALEN];
    int i = 0;

    if (argc != 1) {
        printf ("usage: ./consumer\n");
        exit(1);
    }
 
    mf_connect();
    mf_create (mqname, 16);
    qid = mf_open(mqname);
    while (1) {
        mf_recv(qid, (void *) recvbuffer, MAX_DATALEN);
        if (recvbuffer[0] == -1)
            break;
        i++;
        printf ("received data message %d\n", i);
    }
    mf_close(qid);
    mf_remove(mqname); 
    mf_disconnect();
	return 0;
}


