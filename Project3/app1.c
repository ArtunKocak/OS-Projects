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
#include "mf.h"

#define COUNT 10
char *semname1 = "/semaphore1";
char *semname2 = "/semaphore2";
sem_t *sem1, *sem2;
char *mqname1 = "msgqueue1";

int 
main(int argc, char **argv)
{

    mf_connect();
    mf_create("mq1",16);
    mf_create("mq2",16);
    mf_create("mq3",16);
    mf_create("mq4",16);
    mf_remove( "mq2");
    mf_create("mq5", 32);

    mf_create("mq6",16);
    mf_disconnect();
    return 0;
}
