#include "ring_buffer.h"
#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdio.h>
#include<unistd.h>
#include <sched.h>

int init_ring(struct ring *r) {

    // Initialize semaphores
    sem_init(&r->sem_mutex, 1, 1);
    sem_init(&r->sem_empty, 1, RING_SIZE);
    sem_init(&r->sem_full, 1, 0);
    // Initialize indices
    r->p_tail = 0;
    r->p_head = 0;
    r->c_tail = 0;
    r->c_head = 0;

    for(int i = 0; i < RING_SIZE; i++) {
        r->buffer[i].k = 0;
        r->buffer[i].v = 0;
    }
    return 0;
}

// Submit a new item to the ring buffer
void ring_submit(struct ring *r, struct buffer_descriptor *bd) {
    uint32_t p_head;
    sem_wait(&r->sem_empty);
    sem_wait(&r->sem_mutex);
    do {
      p_head = r->p_head;
    } while (!atomic_compare_exchange_strong(&(r->p_head), &(p_head), (r->p_head + 1) % RING_SIZE));
    
    r->buffer[p_head] = *bd;
    bd->ready = 0;
   
    while (!atomic_compare_exchange_strong(&(r->p_tail), &(p_head), (r->p_tail + 1) % RING_SIZE)) {}
    sem_post(&r->sem_mutex);
    sem_post(&r->sem_full);
}

// Retrieve an item from the ring buffer
void ring_get(struct ring *r, struct buffer_descriptor *bd) {
    uint32_t c_head;
    sem_wait(&r->sem_full);
    sem_wait(&r->sem_mutex);
    do {
      c_head = r->c_head;
    } while (!atomic_compare_exchange_strong(&(r->c_head), &(c_head), (r->c_head + 1) % RING_SIZE));

    *bd = r->buffer[c_head];
    
    while (!atomic_compare_exchange_strong(&(r->c_tail), &(c_head), (r->c_tail + 1) % RING_SIZE)) {}
    sem_post(&r->sem_mutex);
    sem_post(&r->sem_empty);
}
