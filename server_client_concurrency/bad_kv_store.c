#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "ring_buffer.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdatomic.h>

#define MAX_THREADS 128

char shm_file[] = "shmem_file";
char *shmem_area = NULL;
struct ring *ring = NULL;
pthread_t threads[MAX_THREADS];

int num_threads = 1;
int verbose = 0;
int table_size = 1000;

struct thread_context {
	int tid; /* thread ID */
	int num_reqs; /* # of requests that this thread is responsible for */
	struct request *reqs; /* requests assigned to this thread */
};

typedef struct {
    key_type k;
    value_type v;
} kv_entry;

// Hash table
typedef struct {
    kv_entry *entries;
    int size;
} hash_table;

hash_table table;

// Insert or update a key-value pair in the hash table
void put(key_type k, value_type v) {
  printf("pt4\n"); 
    int index = hash_function(k, table_size);
    kv_entry *entry = &table.entries[index];
    
    if (entry != NULL) {
      if (entry->k != k) {
	do {
	  index = (index + 1) % table.size;
	  entry = &table.entries[index];
	} while (entry != NULL);
      }

      if (entry->k == k) {
	table.entries[index].v = v;
      }
    }
printf("pt5\n"); 
    // Add new key-value pair
    table.entries[index].k = k;
    table.entries[index].v = v;
    atomic_fetch_add(&table.size, 1);
}

// Retrieve the value associated with a key from the hash table
value_type get(key_type k) {

    int index = hash_function(k, table_size);
    kv_entry *entry = &table.entries[index];
    
    if (entry != NULL) {
        if (entry->k != k) {
            do {
                index = (index + 1) % table.size;
                entry = &table.entries[index];
            } while (entry->k != k);
        }

        if (entry->k == k) {
	    return entry->v;
        }
    }
    printf("pt3\n"); 

    return 0;
}

// Server thread function
void *thread_function(void *arg) {
    struct buffer_descriptor *bd;
    bd = malloc(sizeof(struct buffer_descriptor));
    struct buffer_descriptor *result;
    while (1) {
        do {
	    ring_get(ring, bd);
        } while (bd->k == 0);
	
	result = (struct buffer_descriptor *)(shmem_area + bd->res_off);
	memcpy(result, bd, sizeof(struct buffer_descriptor));
        if (bd->req_type == PUT) {
            put(bd->k, bd->v);
        } else if (bd->req_type == GET) {
            value_type res = get(bd->k);
            result->v = res;
        }
        // Update the request status 
        result->ready = 1;
    }
}

static int parse_args(int argc, char **argv) {
    int op;
    while ((op = getopt(argc, argv, "n:s:v")) != -1) {
        switch (op) {
        case 'n':
            num_threads = atoi(optarg);
            break;

        case 'v':
            verbose = 1;
            break;

        case 's':
            table_size = atoi(optarg);
            break;

        default:
            printf("failed getting arg in main %d;\n", op);
            return 1;
        }
    }

    return 0;
}

int main(int argc, char *argv[]) {
    if (parse_args(argc, argv) != 0) {
        exit(EXIT_FAILURE);
    }
    // Initialize the hash table
    table.size = 0;
    table.entries = calloc(table_size, sizeof(kv_entry));
    if (table.entries == NULL) {
      perror("mallo error\n");
      exit(1);
    }

    // get shared memory region and ring
    int fd = open(shm_file, O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    if (fd < 0) {
        perror("open");
    }

    struct stat file_info;
    if (fstat(fd, &file_info) == -1) {
      perror("file");
    }
    printf("%d\n", file_info.st_size-1);
    char *shmem_area = mmap(NULL, file_info.st_size-1, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
    if (shmem_area == (void *)-1) {
        perror("mmap");
    }

    close(fd);
    ring = (struct ring *)shmem_area;

    // Create server threads
    for (int i = 0; i < num_threads; i++) {
        struct thread_context context;
        context.tid = i;
        if (pthread_create(&threads[i], NULL, &thread_function, &context)) {
	    perror("pthread_create");
	}
    }

    // Wait for server threads to finish
    for (int i = 0; i < num_threads; i++) {
        if (pthread_join(threads[i], NULL)) {
	    perror("pthread_join");
        }
    }

    // Clean up
    /*
    free(threads);
    for (int i = 0; i < table.size; i++) {
        free(table.entries[i]);
    }
    free(table.entries);*/
    return 0;
}
 
