#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include "ring_buffer.h"
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdatomic.h>
#include <string.h>

#define MAX_THREADS 128
char shm_file[] = "shmem_file";
char *shmem_area = NULL;
struct ring *ring = NULL;
pthread_t threads[MAX_THREADS];
int num_threads = 1;
uint32_t table_size = 1024;
int verbose;

#define PRINTV(...) if (verbose) printf("Server: "); if (verbose) printf(__VA_ARGS__)

struct thread_context {
    int tid;
};

typedef struct pair{
    key_type k;
    value_type v;
    struct pair *next;
} kv_pair;

typedef struct {
    kv_pair *pairs;
    int size;
} kv_list;


typedef struct {
    kv_pair *entries;
    pthread_mutex_t *mutex;
    int num_locks;
} hash_table;

hash_table *table;

void put(key_type k, value_type v) {
    int index = hash_function(k, table_size);
    pthread_mutex_lock(&table->mutex[index / table->num_locks]);
    kv_pair *pair = &table->entries[index];
    if (pair != NULL && pair->k != k) {
        while(pair->next != NULL && pair->next->k != k) {
            pair = pair->next;
        }

        if (pair->next != NULL && pair->next->k == k) {
          pair->next->v = v;
        } else {
          pair->next = malloc(sizeof(kv_pair));
          pair->next->k = k;
          pair->next->v = v;
        }
    } else {
      table->entries[index].v =	v;
    }
    pthread_mutex_unlock(&table->mutex[index / table->num_locks]);
}

value_type get(key_type k) {
    int index = hash_function(k, table_size);
    pthread_mutex_lock(&table->mutex[index / table->num_locks]);
    kv_pair *pair = &table->entries[index];
    if (pair != NULL && pair->k != k) {
        while(pair->next != NULL && pair->next->k != k) {
            pair = pair->next;
        }
	
	if (pair->next != NULL && pair->next->k == k) {
	    pthread_mutex_unlock(&table->mutex[index / table->num_locks]);
	    return pair->next->v;
	}
    } else {
      pthread_mutex_unlock(&table->mutex[index / table->num_locks]);
      return table->entries[index].v;
    }
    pthread_mutex_unlock(&table->mutex[index / table->num_locks]);
    return 0;
}

void *thread_function(void *arg) {
    struct buffer_descriptor bd;
    struct buffer_descriptor *result;
    while (1) {
        ring_get(ring, &bd);
        result = (struct buffer_descriptor *)(shmem_area + bd.res_off);
        memcpy(result, &bd, sizeof(struct buffer_descriptor));
        if (result->req_type == PUT) {
            put(result->k, result->v);
        }
        else {
            result->v = get(result->k);
        }
        result->ready = 1;
    }
}

static int parse_args(int argc, char **argv) {
    int op;
    while ((op = getopt(argc, argv, "n:t:s:v")) != -1) {
        switch (op) {
        case 'n':
            num_threads = atoi(optarg);
	    break;
	case 't':
	    num_threads = atoi(optarg);
	    break;
	case 'v':
	    verbose = 1;
	    break;
        case 's':
            table_size = atoi(optarg);
            break;
        default:
            printf("failed getting arg in main %c\n", op);
            return 1;
        }
    }

    return 0;
}

int main(int argc, char *argv[]) {
    if (parse_args(argc, argv) != 0) {
		exit(1);
    }

    // Allocate space for hash table struct.
    table = malloc(sizeof(hash_table));
    if (table == NULL) {
        exit(1);
    }
    
    // Allocate (zero'd) space for entry buckets.
    table->entries = calloc(table_size, sizeof(kv_pair));
    if (table->entries == NULL) {
        free(table); // error, free table before we return!
        perror("error");
        exit(1);
    }
    
    table->num_locks = (table_size/100)+1;
    table->mutex = calloc(table->num_locks, sizeof(pthread_mutex_t));
    if (table->mutex == NULL) {
        free(table); // error, free table before we return!
        perror("error");
        exit(1);

    }
    
    struct stat file_info;
    int fd = open(shm_file, O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    if (fd < 0) {
        perror("open");
    }
    
    if (fstat(fd, &file_info) == -1) {
        perror("open");
    }
    // points to the beginning of the shared memory region
    shmem_area = mmap(NULL, file_info.st_size - 1, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
    if (shmem_area == (void *)-1) {
        perror("mmap");
    }
    /* mmap dups the fd, no longer needed */
    close(fd);
    ring = (struct ring *)shmem_area;

    // // start threads
    for (int i = 0; i < num_threads; i++) {
        struct thread_context context;
        context.tid = i;
        if (pthread_create(&threads[i], NULL, &thread_function, &context)) {
            perror("pthread_create");
        }
    }

    /// wait for threads
    for (int i = 0; i < num_threads; i++) {
        if (pthread_join(threads[i], NULL)) {
            perror("pthread_join");
        }
    }

    //hash_table_destroy(table);
}
