#include <stdlib.h>
#include <mqueue.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>  


#define MAX_ARGS 10
#define BUFFER_SIZE 1024
#define HEADER_SIZE 8 
typedef enum {
    CONREQUEST = 1,
    CONREPLY,
    COMLINE,
    COMRESULT,
    QUITREQ,
    QUITREPLY,
    QUITALL
} MessageType;
struct connection_request {
    int client_id;
    char cs_pipe_name[64];
    char sc_pipe_name[64];
    int wsize;
};


void encode_message(uint8_t type, const void* data, size_t dataSize, uint8_t** encodedMessage, uint32_t* messageSize) {
    *messageSize = HEADER_SIZE + dataSize; 
    *encodedMessage = malloc(*messageSize);


    (*encodedMessage)[0] = (uint8_t)(*messageSize & 0xFF);
    (*encodedMessage)[1] = (uint8_t)((*messageSize >> 8) & 0xFF);
    (*encodedMessage)[2] = (uint8_t)((*messageSize >> 16) & 0xFF);
    (*encodedMessage)[3] = (uint8_t)((*messageSize >> 24) & 0xFF);

    (*encodedMessage)[4] = type;


    memcpy(*encodedMessage + HEADER_SIZE, data, dataSize);
}

void decode_message(const uint8_t* encodedMessage, uint8_t* type, void* data, uint32_t* dataSize) {
    if (encodedMessage == NULL || type == NULL || data == NULL || dataSize == NULL) {
        fprintf(stderr, "Invalid argument to decode_message\n");
        return;
    }

    *dataSize = (uint32_t)(encodedMessage[0]) |
                ((uint32_t)(encodedMessage[1]) << 8) |
                ((uint32_t)(encodedMessage[2]) << 16) |
                ((uint32_t)(encodedMessage[3]) << 24);

    *dataSize -= HEADER_SIZE;

    *type = encodedMessage[4];

    memcpy(data, encodedMessage + HEADER_SIZE, *dataSize);
}


void parse_command(char* cmd, char* args[] ) {
    int i = 0;
    char *token = strtok(cmd, " ");
    while (token != NULL && i < MAX_ARGS) {
        args[i++] = token;
        token = strtok(NULL, " ");
    }
    args[i] = NULL; 
}

char* childserver(struct connection_request *request,char*cmd) {
    char *returnval = NULL;
    char *args1[MAX_ARGS + 1];
    char *args2[MAX_ARGS + 1];
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }
    pid_t cpid1, cpid2;

    if (cmd == NULL) {
        printf("Error reading command.\n");
        exit(EXIT_FAILURE);
    }

    cmd[strcspn(cmd, "\n")] = 0;
    FILE* tmpf = tmpfile();
    int stdoutCopy = dup(fileno(stdout));
    dup2 (fileno(tmpf), fileno(stdout));

    if (strchr(cmd, '|') != NULL) {
        char *part1 = strtok(cmd, "|");
        char *part2 = strtok(NULL, "");

        if (part1 == NULL || part2 == NULL) {
            printf("Error: Invalid command format.\n");
            exit(EXIT_FAILURE);
        }

        parse_command(part1, args1);
        parse_command(part2, args2);



        cpid1 = fork();
        if (cpid1 == -1) {
            perror("fork");
            exit(EXIT_FAILURE);
        }

        if (cpid1 == 0) { 
            close(pipefd[0]); 
            dup2(pipefd[1], STDOUT_FILENO); 
            close(pipefd[1]);

            execvp(args1[0], args1);
            perror("execvp args1");
            exit(EXIT_FAILURE);
        } else {
            cpid2 = fork();
            if (cpid2 == -1) {
                perror("fork");
                exit(EXIT_FAILURE);
            }

            if (cpid2 == 0) { 
                close(pipefd[1]); 
                dup2(pipefd[0], STDIN_FILENO); 
                close(pipefd[0]);

                execvp(args2[0], args2);
                perror("execvp args2");
                exit(EXIT_FAILURE);
            } else {
                close(pipefd[0]);
                close(pipefd[1]);
                wait(NULL);
                wait(NULL);
                rewind(tmpf);
                dup2(stdoutCopy, fileno(stdout));

                char *buffer = NULL;
                size_t len;
                ssize_t bytes_read = getdelim(&buffer, &len, '\0', tmpf);

                if (bytes_read != -1) {
                    returnval = buffer;
                    close(stdoutCopy);
                }
            }
        }
    } else {
        parse_command(cmd, args1);

        cpid1 = fork();
        if (cpid1 == -1) {
            perror("fork");
            exit(EXIT_FAILURE);
        }

        if (cpid1 == 0) {
            execvp(args1[0], args1);
            perror("execvp single command");
            exit(EXIT_FAILURE);
        } else {
            wait(NULL); 
            rewind(tmpf);
            dup2(stdoutCopy, fileno(stdout));

            size_t len;
            ssize_t bytes_read = getdelim(&returnval, &len, '\0', tmpf);
            if (bytes_read != -1) {
                close(stdoutCopy);
            }
            close(stdoutCopy);
            close(fileno(tmpf));
        }
    }
    return returnval;
}



void deserialize_connection_request(const char *inStr, struct connection_request *req) {
    sscanf(inStr, "%d,%63[^,],%63[^,],%d", &req->client_id, req->cs_pipe_name, req->sc_pipe_name, &req->wsize);
}


int main(int argc, char* argv[]) {

    if (argc != 2) {
        fprintf(stderr, "Usage: %s /message_queue_name\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    char* mqname = argv[1];

    mqd_t mq;
    struct mq_attr mq_attr;
    int n;
    int bufferlen;
    char* bufferp;

    mq = mq_open(mqname, O_RDONLY | O_CREAT, 0666, NULL);
    if (mq == (mqd_t)-1) {
        perror("mq_open");
        exit(EXIT_FAILURE);
    }
    printf("mq created, mq id = %d\n", (int)mq);

    mq_getattr(mq, &mq_attr);
    printf("mq maximum msgsize = %ld\n", mq_attr.mq_msgsize);

    uint8_t* receivedMessage; 
    uint32_t receivedMessageSize; 
    receivedMessageSize = mq_attr.mq_msgsize; 
    receivedMessage = (uint8_t*)malloc(receivedMessageSize);

    while (1) {
        n = mq_receive(mq, receivedMessage, receivedMessageSize, NULL);
        if (n == -1) {
            perror("mq_receive failed");
            continue;  
        }
        printf("mq_receive success, message size = %d\n", n);

   
        uint8_t receivedType;
        uint32_t receivedDataSize;
        uint8_t receivedData[1024];
        decode_message(receivedMessage, &receivedType, &receivedData, &receivedDataSize);


        struct connection_request* request = malloc(sizeof(struct connection_request));
        if (request == NULL) {
            perror("Failed to allocate memory for request");
            exit(EXIT_FAILURE);
        }
        deserialize_connection_request(receivedData,request);
        printf("Server main: CONREQUEST message received: pid=%d, cs=%s sc=%s, wsize=not entered by user (1024) \n"
                ,request->client_id,request->cs_pipe_name,request->sc_pipe_name);
        pid_t pid = fork();
        if (pid == -1) {
            perror("fork failed");
            continue;
        }
        else if (pid == 0) {  
            printf("cs: %s\n",request->cs_pipe_name);
            int cs_fd = open(request->cs_pipe_name, O_RDONLY);
            if (cs_fd == -1) {
                perror("open cs_pipe failed");
                exit(EXIT_FAILURE);
            }
            close(cs_fd);


            int sc_fd = open(request->sc_pipe_name, O_WRONLY);
            if (sc_fd == -1) {
                perror("open sc_pipe failed");
                close(cs_fd);
                exit(EXIT_FAILURE);
            }


            char* msg = "Connection established";
            uint8_t* finalMessage = NULL;
            uint32_t messageSize = 0;
            encode_message(CONREPLY,msg,strlen(msg), &finalMessage,&messageSize);

            if (write(sc_fd, finalMessage, messageSize) == -1) {
                perror("write to sc_pipe failed");

                close(sc_fd);
                close(cs_fd);
                exit(EXIT_FAILURE);
            }

            uint8_t coded[request->wsize];
            uint8_t receivedType1 = 0;
            uint32_t receivedDataSize1 = 0;
            uint8_t receivedData1[1024];
            memset(receivedData1, 0, sizeof(receivedData1));
            cs_fd = open(request->cs_pipe_name, O_RDONLY);
            if (cs_fd == -1) {
                perror("open cs_pipe failed");
                exit(EXIT_FAILURE);
            }
            while(1){
                ssize_t len = read(cs_fd, coded, 1024);
                printf("\nserver child: COMLINE message received: len=%zd, type=3, data=%s\n"
                       ,len,coded);
                if (len == -1) {
                    perror("read from cs_pipe failed");
                }
                else {
                    coded[len] = '\0';
                    char *output = NULL;
                    output = childserver(request,coded);
                    printf("command execution finished");
                    write(sc_fd, output, strlen(output)+1);
                }
            }

            /*



            if (write(sc_fd, "ok We did it", sizeof(coded) == -1)) {
                perror("read from cs_pipe failed");
            }

             */

            free(request);
            close(cs_fd);
            close(sc_fd);
            exit(0); 
        }

    }

    free(bufferp);
    mq_close(mq);
    mq_unlink(mqname);
    return 0;
}
