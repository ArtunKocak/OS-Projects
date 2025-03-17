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

#define COUNT 10

char *mqname1 = "msgqueue1";

int
main(int argc, char **argv)
{
    mf_connect();
    mf_create("mq1",16);
    mf_create("mq2",16);
    mf_create("mq3",16);

    mf_remove("mq2");
    mf_create("mq4" , 16);


    char *bufptr = (char *) malloc (8);
    mf_send (0, (void *) bufptr, 4);
    printf("%s\n",(char*)bufptr);
    char *bufptre = (char *) malloc (8);
    mf_send (0, (void *) bufptre, 8);
    char *bufptrew = (char *) malloc (8);
    mf_send (0, (void *) bufptrew, 4);

    char *bufptrewww = (char *) malloc (16);
    mf_recv(0,bufptrewww,32);
    printf("%s\n",(char*)bufptrewww);


    return 0;
}
