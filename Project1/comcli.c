#include <stdlib.h>
#include <mqueue.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h> 
#include <string.h>
#include <stdint.h>
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

#define HEADER_SIZE 8 

void serialize_connection_request( struct connection_request *req, char *outStr) {
    sprintf(outStr, "%d,%s,%s,%d", req->client_id, req->cs_pipe_name, req->sc_pipe_name, req->wsize);
}
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

void handleinput(char* input, uint8_t** finalMessage, uint32_t* messageSize) {
    MessageType type = COMLINE; 
    if (strncmp(input, "QUITALL", 7) == 0) {
        type = QUITALL;
    } else if (strncmp(input, "QUIT", 4) == 0) {
        type = QUITREQ;
    } else {
        type = COMLINE;
    }

    encode_message(type, input, strlen(input), finalMessage, messageSize);


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
    struct connection_request request;

    mq = mq_open(mqname, O_WRONLY);
    if (mq == (mqd_t)-1) {
        perror("mq_open");
        exit(EXIT_FAILURE);
    }
    printf("mq opened, mq id = %d\n", (int) mq);

    if (mq_getattr(mq, &mq_attr) == -1) {
        perror("Failed to get MQ attributes");
        exit(1);
    }
    printf("MQ maximum msgsize = %ld\n", mq_attr.mq_msgsize);

    request.client_id = getpid();

    sprintf(request.cs_pipe_name, "/tmp/cs_pipe_%d", getpid());
    sprintf(request.sc_pipe_name, "/tmp/sc_pipe_%d", getpid());

    char message[1024];
    serialize_connection_request(&request,message);


    uint8_t* finalMessage = NULL;
    uint32_t messageSize = 0;

    encode_message(CONREQUEST, message, sizeof(request), &finalMessage, &messageSize);


    if (mq_send(mq, (char *)finalMessage, messageSize, 0) == -1) {
        perror("MQ_send failed");
        free(finalMessage);
        exit(EXIT_FAILURE);
    }

    free(finalMessage); 
    

    request.wsize = 1024;
    if ( mkfifo(request.cs_pipe_name, 0666) == -1 && errno != EEXIST) {
        perror("Could not create CS pipe");
        exit(EXIT_FAILURE);
    }

    if (mkfifo(request.sc_pipe_name, 0666) == -1 && errno != EEXIST) {
        perror("Could not create SC pipe");
        exit(EXIT_FAILURE);
    }

    int cs_fd = open(request.cs_pipe_name,O_WRONLY);
    if (cs_fd == -1) {
        perror("open cs_pipe failed");
        exit(EXIT_FAILURE);
    }

    int sc_fd = open(request.sc_pipe_name, O_RDONLY);
    if (sc_fd == -1) {
        perror("Could not open SC pipe for reading");
        exit(EXIT_FAILURE);
    }





    char msg[1024];
    char cmd[1024];
    ssize_t num_bytes_read = read(sc_fd, msg, sizeof(msg) - 1); 
    uint8_t receivedType = 0;
    uint32_t receivedDataSize;
    uint8_t receivedData[1024];
    decode_message(msg, &receivedType, &receivedData, &receivedDataSize);

    printf("Client: CONREPLY message received: cs=%s sc=%s, data= %s \n"
            ,request.cs_pipe_name,request.sc_pipe_name,receivedData);
    if (num_bytes_read == -1) {
        perror("Error reading from SC pipe");
        exit(EXIT_FAILURE);
    }

    while(1) {
        uint32_t messageSize1 = 0;
        uint8_t* finalMessagefork = NULL;
        char cmd[1024]; 
        printf("Type Command:");
        if (fgets(cmd, sizeof(cmd), stdin) == NULL) {
            printf("Error reading command.\n");
            exit(EXIT_FAILURE);
        }


        //handleinput(cmd, &finalMessagefork, &messageSize1);

        write(cs_fd, cmd, strlen(cmd) + 1);



        uint8_t* response[8192];
        ssize_t lenr = read(sc_fd, response, sizeof(response) - 1);
        if (lenr > 0) {
            response[lenr] = '\0';
            printf("Server Response:\n %s", response);
        } else if (lenr == -1) {
            printf("read from sc_fd failed");
        } else {
            printf("Server closed the connection\n");
        }



        free(finalMessagefork);
    }

    mq_close(mq);
    return 0;
}
