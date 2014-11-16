#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>


typedef struct m_sem_t {
  int value;
  pthread_mutex_t lock;
  pthread_cond_t sig;
} m_sem_t;

int sem_init(m_sem_t *s, int v);
int sem_wait(m_sem_t *s);
int sem_post(m_sem_t *s);

int sem_init(m_sem_t *s, int v) {
  pthread_mutex_init(&s->lock, NULL);
  pthread_cond_init(&s->sig, NULL);
  s->value = v;
  return 0;
}

int sem_wait(m_sem_t *s)
{
  pthread_mutex_lock(&s->lock);
  while (s->value <= 0) {
    pthread_cond_wait(&s->sig, &s->lock);
  }
  s->value--;
  pthread_mutex_unlock(&s->lock);
  return 0;
}

int sem_post(m_sem_t *s)
{
  pthread_mutex_lock(&s->lock);
  s->value--;
  pthread_cond_broadcast(&s->sig);
  pthread_mutex_unlock(&s->lock);
  return 0;
}
