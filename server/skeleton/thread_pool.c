#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#include "thread_pool.h"

/**
 *  @struct threadpool_task
 *  @brief the work struct
 *
 *  Feel free to make any modifications you want to the function prototypes and structs
 *
 *  @var function Pointer to the function that will perform the task.
 *  @var argument Argument to be passed to the function.
 */

#define MAX_THREADS 20
#define STANDBY_SIZE 8
#define TASK_QUEUE_SIZE 40

typedef struct pool_task_t {
  void (*function)(void*);
  void *argument;
} pool_task_t;


struct pool_t {
  pthread_mutex_t lock;
  pthread_cond_t added;
  pthread_cond_t removed;
  pthread_t *threads;
  int num_threads;
  pool_task_t *queue;
  int queue_size;
  int head;
  int length;
  int stop;
};

static void* thread_do_work(void *pool);

// FILE* f;

/*
 * Create a threadpool, initialize variables, etc
 *
 */
pool_t *pool_create(int queue_size, int num_threads)
{
  int i;
  // f = fopen("/Users/katsuya94/pool.log", "w");
  if (num_threads > MAX_THREADS) num_threads = MAX_THREADS;
  pool_t* pool = (pool_t*) malloc(sizeof(pool_t));
  pthread_mutex_init(&pool->lock, NULL);
  pthread_cond_init(&pool->added, NULL);
  pthread_cond_init(&pool->removed, NULL);
  pool->threads = (pthread_t*) malloc(sizeof(pthread_t) * num_threads);
  pool->num_threads = num_threads;
  pool->queue = (pool_task_t*) malloc(sizeof(pool_task_t) * queue_size);
  pool->queue_size = queue_size;
  pool->head = 0;
  pool->length = 0;
  pool->stop = 0;
  for (i = 0; i < pool->num_threads; i++) {
    if (pthread_create(&pool->threads[i], NULL, thread_do_work, pool) != 0) {
      // fprintf(f, "pthread_create failed\n");
    }
  }
  return pool;
}


/*
 * Add a task to the threadpool
 *
 */
int pool_add_task(pool_t *pool, void (*function)(void *), void *argument)
{
  pthread_mutex_lock(&pool->lock);
  while (pool->length >= pool->queue_size) {
    pthread_cond_wait(&pool->removed, &pool->lock);
  }
  int pos = (pool->head + pool->length) % pool->queue_size;
  pool->queue[pos].function = function;
  pool->queue[pos].argument = argument;
  pool->length++;
  pthread_cond_broadcast(&pool->added);
  // fprintf(f, "added %d to queue, length is now %d\n", *((int*) argument), pool->length);
  pthread_mutex_unlock(&pool->lock);
  return 0;
}



/*
 * Destroy the threadpool, free all memory, destroy threads, etc
 *
 */
int pool_destroy(pool_t *pool)
{
  int i;

  pool->stop = 1;

  for (i = 0; i < pool->num_threads; i++) {
    pthread_cond_broadcast(&pool->added);
  }

  for (i = 0; i < pool->num_threads; i++) {
    pthread_join(pool->threads[i], NULL);
  }

  pthread_mutex_destroy(&pool->lock);
  pthread_cond_destroy(&pool->added);
  pthread_cond_destroy(&pool->removed);

  free(pool->threads);
  free(pool->queue);
  free(pool);

  // fclose(f);

  return 0;
}



/*
 * Work loop for threads. Should be passed into the pthread_create() method.
 *
 */
static void* thread_do_work(void* void_pool)
{
  // pthread_t tid = pthread_self();
  // fprintf(f, "thread %p starting\n", (void*) tid);
  pool_t* pool = (pool_t*) void_pool;
  while (pool->stop == 0) {
    pthread_mutex_lock(&pool->lock);
    while (pool->length <= 0) {
      pthread_cond_wait(&pool->added, &pool->lock);
      if (pool->stop != 0) {
        pthread_mutex_unlock(&pool->lock);
        // fprintf(f, "thread %p finishing\n", (void*) tid);
        return NULL;
      }
    }
    void (*function)(void*) = pool->queue[pool->head].function;
    void* argument = pool->queue[pool->head].argument;
    pool->head = (pool->head + 1) % pool->queue_size;
    pool->length--;
    pthread_cond_broadcast(&pool->removed);
    pthread_mutex_unlock(&pool->lock);
    // fprintf(f, "%p: removed %d from queue, length is now %d, processing...\n", (void*) tid, *((int*) argument), pool->length);
    function(argument);
    // fprintf(f, "%p finished processing\n", (void*) tid);
  }
  // fprintf(f, "thread %p finishing\n", (void*) tid);
  return NULL;
}
