#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <semaphore.h>
#include <math.h>
#include "mf.h"

#define MF_ERROR -1
#define MF_SUCCESS 0
#define BLOCK_SIZE 1

typedef struct {
    char shmem_name[256];
    int shmem_size;
    int max_msgs_in_queue;
    int max_queues_in_shmem;
} Config;

typedef struct message {
    int datalength;
    void* bufptr;
    void* head;
    void* tail;
} message_t;

typedef struct QueueNode {
    message_t* data;
    struct QueueNode* next;
} QueueNode;

typedef struct {
    QueueNode* front;
    QueueNode* rear;
} Queue;

typedef struct {
    sem_t mutex;
    void* head;
    void* tail;
    void* base_ptr;
    int capacity;
    int refcount;
    char* name;
    Queue* waitlist;
    char data[];
} message_queue_t;

typedef struct free_block {
    size_t size;
    struct free_block* next;
} free_block_t;

unsigned char *bitmap;
int read_config(Config *config) {
    FILE *file = fopen(CONFIG_FILENAME, "r");
    if (!file) {
        perror("Failed to open configuration file");
        return MF_ERROR;
    }

    char line[1024];
    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '#' || line[0] == '\n') {
            continue;
        }

        char key[256];
        char value[256];
        if (sscanf(line, "%s %s", key, value) == 2) {
            if (strcmp(key, "SHMEM_NAME") == 0) {
                strncpy(config->shmem_name, value, sizeof(config->shmem_name));
            } else if (strcmp(key, "SHMEM_SIZE") == 0) {
                config->shmem_size = atoi(value) * 8;
            }
        }


    }

    bitmap = (unsigned char *)malloc(config->shmem_size / 8);
    if (!bitmap) {
        perror("Failed to allocate memory for bitmap");
        return MF_ERROR;
    }
    memset(bitmap, 0, config->shmem_size / 8);


    fclose(file);
    return MF_SUCCESS;

}


int modif_shm_open(const char *name, int oflag, mode_t mode) {
    static int (*real_open)(const char *, int, mode_t) = NULL;

    char *n = strdup(name);
    char *p = n;
    while (*p) {
        if (*p == '/')
            *p = '_';
        p++;
    }

    int result = shm_open(n, oflag, mode);

    free(n);

    return result;
}

int modif_shm_close(const char *name) {


    char *n = strdup(name);
    char *p = n;
    while (*p) {
        if (*p == '/')
            *p = '_';
        p++;
    }

    int result = shm_unlink(n);

    free(n);

    return result;
}

Config config;
message_queue_t *queue;
void *shm_addr;
void *addr_inc;
int count;
message_queue_t* addr[5];
int unallocated;


int mf_init() {

    if (read_config(&config) == MF_ERROR) {
        fprintf(stderr, "Error reading config\n");
        return MF_ERROR;
    }

    int fd = modif_shm_open(config.shmem_name, O_CREAT | O_RDWR,0666);

    printf("Shared Memory Name: %s\n", config.shmem_name);
    if (fd == -1) {
        perror("shm_open failed");
        return MF_ERROR;
    }

    if (ftruncate(fd, config.shmem_size) == -1) {
        perror("ftruncate failed");
        shm_unlink(config.shmem_name);
        return MF_ERROR;
    }


    shm_addr = mmap(0, config.shmem_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    addr_inc = shm_addr;
    if (shm_addr == MAP_FAILED) {
        perror("mmap failed");
        shm_unlink(config.shmem_name);
        return MF_ERROR;
    }

    queue = (message_queue_t *) shm_addr;
    for (int i = 0; i < 2; i++) {
        char *queue_ptr = (char *)queue + i * (sizeof(message_queue_t) + config.max_msgs_in_queue * MAX_DATALEN);
        queue = (message_queue_t *)queue_ptr;

        if (sem_init(&queue->mutex, 1, 1) != 0) {
            perror("sem_init error");
            munmap(shm_addr, config.shmem_size);
            shm_unlink(config.shmem_name);
            return MF_ERROR;
        }

        queue->head = 0;
        queue->tail = 0;
        queue->capacity = config.max_msgs_in_queue * MAX_DATALEN;
    }



    close(fd);

    return MF_SUCCESS;

}


int mf_destroy() {
    int status = 0;

    if (modif_shm_close(config.shmem_name) == -1) {
        perror("Error unlinking shared memory");
        status = -1;
    }


    for (int i = 0; i < 2; i++) {
        char queue_name[256];
        snprintf(queue_name, sizeof(queue_name), "/libmf_semaphore_%d", i);
        if (sem_unlink(queue_name) == -1) {
            perror("Error unlinking semaphore");
            status = -1;
        }
    }

    return status;
}


int mf_connect()
{

    if (read_config(&config) == MF_ERROR) {
        fprintf(stderr, "Error reading config\n");
        return MF_ERROR;
    }
    printf("%d",config.shmem_size);
    int fd = modif_shm_open(config.shmem_name, O_RDWR, 0666);
    if (fd == -1) {
        perror("shm_open failed");
        return MF_ERROR;
    }

    shm_addr = mmap(0, config.shmem_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    if (shm_addr == MAP_FAILED) {
        perror("mmap failed");
        close(fd);
        return MF_ERROR;
    }

    printf("Connection succesful");
    close(fd);

    return MF_SUCCESS;
}

int mf_disconnect() {
    if (shm_addr == NULL) {
        fprintf(stderr, "No shared memory to disconnect.\n");
        return MF_ERROR;
    }


    if (munmap(shm_addr, config.shmem_size) == -1) {
        perror("Error unmapping shared memory during disconnect");
        return MF_ERROR;
    }

    shm_addr = NULL;
    printf("Disconnect succesful");
    return MF_SUCCESS;
}


void set_bitmap(int start, int count) {
    for (int i = start; i < start + count; i++) {
        bitmap[i / 8] |= 1 << (i % 8);
    }
}

void clear_bitmap(int start, int count) {
    for (int i = start; i < start + count; i++) {
        bitmap[i / 8] &= ~(1 << (i % 8));
    }
    printf("Cleared\n");
}


int allocate(int mqsize, void* shm_addr, message_queue_t** mq, const char *mqname) {
    int free_count = 0, start = -1;
    int num_blocks = 8 * mqsize;
    for (int i = 0; i < config.shmem_size / 8; i++) {
        for (int bit = 0; bit < 8; ++bit) {
            if (!(bitmap[i] & (1 << bit))) {
                if (free_count == 0) start = i * 8 + bit;
                free_count++;
                if (free_count == num_blocks) {
                    set_bitmap(start, num_blocks);
                    *mq = (message_queue_t*)((char*)shm_addr + start);
                    (*mq)->head = (*mq)->data;
                    (*mq)->tail = (*mq)->data + num_blocks;
                    (*mq)->capacity = num_blocks;
                    (*mq)->name = strdup(mqname);
                    (*mq)->base_ptr = (*mq)->head;
                    sem_init(&((*mq)->mutex), 1, 1);
                    printf("Base %p\n", shm_addr);
                    printf("Message queue created with name %s at %p\n", mqname, (*mq)->head);
                    printf("Message queue ends at %p\n", (*mq)->tail);
                    printf("Allocated %d blocks starting at %d (bitmap idx %d, bit %d)\n", num_blocks, start, i, bit);
                    return 0;
                }
            } else {
                free_count = 0;
            }
        }
    }

    return -1;
}

void deallocate(message_queue_t* mq, void* shm_addr) {
    int start = ((char*)mq - (char*)shm_addr) / BLOCK_SIZE;
    int num_blocks = mq->capacity / BLOCK_SIZE;
    clear_bitmap(start, num_blocks);
}


Queue* initializeQueue(Queue* queue) {
    queue = (Queue*)malloc(sizeof(Queue));

    if (queue == NULL) {
        perror("Failed to allocate memory for queue");
        exit(EXIT_FAILURE);
    }

    queue->front = NULL;
    queue->rear = NULL;
    return queue;
}


//offset ekle
int mf_create(char *mqname, int mqsize) {

    message_queue_t *mq;


    int isfull = 0;

    for(int j = 0 ; j < 5 ; j++){
        if(addr[j] != NULL){
            isfull ++;

        }
    }

    if(isfull == 5){
        perror("max number of message queues are already reached");
        return MF_ERROR;
    }

    int stat = allocate(mqsize , shm_addr, &mq, mqname);
    if ( stat == -1) {
        perror("no space for allocation");
        return MF_ERROR;
    }

    for(int i = 0 ; i < 5 ; i++){
        if(addr[i] == NULL){
            addr[i] = mq;
            break;

        }
    }

    mq->waitlist = initializeQueue(mq->waitlist);
    return MF_SUCCESS;

}


int mf_remove(char *mqname) {
    int i;

    for (i = 0; i < 5; i++) {
        if (addr[i] != NULL && strcmp(addr[i]->name, mqname) == 0) {
            if (addr[i]->refcount != 0) {
                printf("The reference count is not zero\n");
                return MF_ERROR;
            }

            deallocate(addr[i], shm_addr);

            addr[i] = NULL;
            break;
        }
    }

    return MF_SUCCESS;
}


int mf_open(char *mqname) {
    for (int i = 0; i < 5; i++) {

        if (addr[i] != NULL && strcmp(addr[i]->name, mqname) == 0) {
            if (sem_wait(&addr[i]->mutex) != 0) {
                perror("Error semaphore");
                return -1;
            }

            addr[i]->refcount++;

            if (sem_post(&addr[i]->mutex) != 0) {
                perror("Error semaphore");
                return -1;
            }
            return i;
        }
    }
    return -1;
}


//qid is passable
int mf_close(int qid) {

    if (qid < 0 || qid >= 5) {
        fprintf(stderr, "Invalid queue ID\n");
        return -1;
    }


    if (addr[qid] == NULL) {
        fprintf(stderr, "No queue exists at this ID\n");
        return -1;
    }


    if (sem_wait(&addr[qid]->mutex) != 0) {
        perror("sem_wait error");
        return -1;
    }

    addr[qid]->refcount--;

    if (addr[qid]->refcount < 0) {
        fprintf(stderr, "Reference count negative. Possible underflow error.\n");
        sem_post(&addr[qid]->mutex);
        return -1;
    }


    if (sem_post(&addr[qid]->mutex) != 0) {
        perror("sem_post error");
        return -1;
    }
    printf("Queue %d closed. Reference count is now %d.\n", qid, addr[qid]->refcount);

    return 0;  }

int isEmpty(Queue* queue) {
    return (queue->front == NULL);
}

void enqueue(Queue* queue, message_t* data) {
    QueueNode* newNode = (QueueNode*)malloc(sizeof(QueueNode));
    if (newNode == NULL) {
        perror("Failed to allocate memory for new node");
        exit(EXIT_FAILURE);
    }
    newNode->data = data;
    newNode->next = NULL;

    if (isEmpty(queue)) {
        queue->front = newNode;
        queue->rear = newNode;
    } else {
        queue->rear->next = newNode;
        queue->rear = newNode;
    }
}

message_t* dequeue(Queue* queue) {
    if (isEmpty(queue)) {
        fprintf(stderr, "Error: Queue is empty\n");
        exit(EXIT_FAILURE);
    }


    message_t* data = queue->front->data;

    QueueNode* temp = queue->front;

    queue->front = queue->front->next;
    free(temp);

    if (queue->front == NULL) {
        queue->rear = NULL;
    }

    return data;
}
void printQueue(Queue* queue) {
    printf("Queue: ");
    QueueNode* current = queue->front;
    while (current != NULL) {
        printf("%d ", current->data);
        current = current->next;
    }
    printf("\n");
}

void destroyQueue(Queue* queue) {
    while (!isEmpty(queue)) {
        dequeue(queue);
    }
    free(queue);}

message_t* peep(Queue* queue) {
    if (queue == NULL || queue->front == NULL) {
        printf("Queue is empty\n");
        return NULL;
    }

    return queue->front->data;
}

message_t* traverseQueue(Queue* queue) {
    QueueNode* temp = queue->front;
    int count = 0;

    while (temp->next != NULL) {
        count++;
        temp = temp->next;
    }

    return temp->data;
}

int mf_send(int qid, void *bufptr, int datalen) {

    if (datalen % 4 != 0) {

        int y = datalen;


        while(y % 4 != 0){
            y++;;
        }

        datalen = y;
    }

    if (qid < 0 || qid >= 5 || addr[qid] == NULL) {
        fprintf(stderr, "Invalid queue ID or queue does not exist\n");
        return -1;
    }

    datalen = datalen * 8;
    message_queue_t *queue = addr[qid];
    if (sem_wait(&queue->mutex) != 0) {
        perror("Error acquiring semaphore");
        return -1;
    }

    int available_space = (char*)queue->data + queue->capacity - (char*)queue->base_ptr;
    if ((char*)queue->base_ptr <= (char*)queue->tail) {
        available_space += (char*)queue->tail - (char*)queue->head;
    } else {
        available_space = (char*)queue->tail - (char*)queue->head;
    }

    printf("Available space: %d, Data length: %d\n", available_space, datalen);
    int total_space_needed = datalen + sizeof(message_t);

    if(datalen + sizeof(message_t) > available_space){
        do {
            int head_index = (char*)queue->head - (char*)queue->data;
            int tail_index = (char*)queue->tail - (char*)queue->data;

            if (tail_index < head_index) {
                available_space = head_index - tail_index - 1;
            } else {
                available_space = queue->capacity - (tail_index - head_index) - 1;
            }

            if (total_space_needed > available_space) {
                unallocated = datalen + sizeof(message_t);
                sem_post(&queue->mutex);
                usleep(100000);
                sem_wait(&queue->mutex);
            }
        } while (total_space_needed > available_space);}

    message_t* message = (message_t*)queue->base_ptr;
    message->datalength = datalen;
    message->bufptr = (char*)message + sizeof(message_t);

    memcpy(message->bufptr, bufptr, datalen);


    queue->base_ptr = (char*)message->bufptr + datalen;


    if (queue->tail == queue->head && queue->base_ptr != queue->head) {
        queue->tail = queue->base_ptr;
    }

    enqueue(addr[qid]->waitlist, message);

    printf("Message added at %p, new base_ptr is %p\n", message, queue->base_ptr);

    sem_post(&queue->mutex);
    return 0;

}



int mf_recv(int qid, void *bufptr, int bufsize) {
    if (qid < 0 || qid >= 5 || addr[qid] == NULL) {
        fprintf(stderr, "Invalid queue ID or queue does not exist.\n");
        return -1;
    }


    message_queue_t *queue = addr[qid];
    if (sem_wait(&queue->mutex) != 0) {
        perror("Error acquiring semaphore");
        return -1;
    }

    while (isEmpty(queue->waitlist)) {
        sem_post(&queue->mutex);
        usleep(100000);
        sem_wait(&queue->mutex);
    }

    if((char*)(traverseQueue(queue->waitlist)->tail) + unallocated > (char*)queue->tail){
        queue->base_ptr = shm_addr;
    }
    message_t* message = dequeue(queue->waitlist);
    if (message->datalength > bufsize) {
        fprintf(stderr, "Provided buffer is too small to hold the message.\n");
        sem_post(&queue->mutex);
        return -1;
    }

    memcpy(bufptr, message->bufptr, message->datalength);


    sem_post(&queue->mutex);
    return message->datalength;

}

int mf_print() {
    /*
    printf("Shared Memory Overview:\n");
    printf("Memory Name: %s\n", config.shmem_name);
    printf("Memory Size: %d bytes\n", config.shmem_size);
    printf("Number of Queues Configured: %d\n", unallocated);

    int count = 0;

    for(int i = 0 ; i < 5 ; i++){
    	if(addr[i] != NULL){
    	    count++;
    	}
    }

    for (int i = 0; i < count; i++) {
        message_queue_t *queue = addr[i];
        if (queue) {

            printf("\nQueue %d: %s\n", i, queue->name);
            printf("  Capacity: %d\n", queue->capacity);
            printf("  Current Base Pointer: %p\n", queue->base_ptr);
            printf("  Begin Address: %p\n", queue->head);
            printf("  End Address: %p\n", queue->tail);
            printf("  Reference Count: %d\n", queue->refcount);
            printf("  Messages in Waitlist:\n");

            if (isEmpty(queue->waitlist)) {
                printf("  Message waitlist is empty.\n");
            } else {
                QueueNode *current = queue->waitlist->front;
                int msg_count = 0;
                while (current != NULL) {
                    printf("    Messages sent: \n");
                    printf("    Message %d: The first messages is mapped to %p \n", msg_count++, (void*)current->data->head);
                    printf("    Message %d: Length %d bytes\n", msg_count++, (void*)current->data->datalength);
                    current = current->next;
                }
            }
        } else {
            printf("\nQueue %d is not initialized or has been removed.\n", i);
        }
    }
    printf("\n");
    return MF_SUCCESS;
    */
    return 0;
}