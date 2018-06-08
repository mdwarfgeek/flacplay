#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "flacplay.h"
#include "util.h"

static void *aobuf_thread (void *data) {
  struct aobuf_handle *ah = (struct aobuf_handle *) data;
  ao_device *aodev = (ao_device *) NULL;

  unsigned int n;

  /* Open audio device */
  aodev = ao_open_live(ah->aodriver, &(ah->aofmt), ah->aoopt);
  if(!aodev)
    fatal(1, "could not open audio output device");

  for(;;) {
    /* How many bytes in buffer? */
    pthread_mutex_lock(&(ah->mtx));

    if(ah->n <= 0) {  /* buffer underrun */
      while(!ah->finish && ah->n < AOBUFHWM)  /* wait for high water mark */
        pthread_cond_wait(&(ah->icv), &(ah->mtx));
      
      if(ah->finish && ah->n <= 0) {
        pthread_mutex_unlock(&(ah->mtx));
        break;
      }
    }

    n = ah->n;

    pthread_mutex_unlock(&(ah->mtx));
    
    /* Make sure we don't wrap */
    if(n > AOBUFSIZE - ah->op)
      n = AOBUFSIZE - ah->op;  /* so we don't wrap */

    /* Limit number of quanta */
    if(n > AOMAXQ)
      n = AOMAXQ;

    /* Play these samples */
    if(!ao_play(aodev, ah->buf + ah->op * ah->bpq, n * ah->bpq))
      fatal(1, "could not play samples");

    /* Update pointer */
    ah->op += n;
    ah->op &= AOBUFMASK;

    /* Release buffers and inform the other end */
    pthread_mutex_lock(&(ah->mtx));

    if(ah->n >= AOBUFSIZE)  /* buffer was full */
      pthread_cond_signal(&(ah->ocv));

    ah->n -= n;
    ah->nplayed += n;

    pthread_mutex_unlock(&(ah->mtx));
  }

  /* Close audio device */
  if(!ao_close(aodev))
    fatal(1, "error closing audio output device");

  return(NULL);
}

int aobuf_init (struct aobuf_handle *ah,
                int aodriver,
                struct ao_option *aoopt) {
  pthread_attr_t attr;

  ah->aodriver = aodriver;
  ah->aoopt = aoopt;

  ah->ip = 0;
  ah->op = 0;
  ah->n = 0;

  ah->bps = (ah->aofmt.bits + 7) / 8;
  ah->bpq = ah->aofmt.channels * ah->bps;

  ah->nplayed = 0;

  /* Allocate audio out buffer */
  ah->buf = (char *) malloc(AOBUFSIZE * ah->bpq * sizeof(char));
  if(!ah->buf)
    error(1, "malloc");

  /* Create audio out thread */
  if(pthread_mutex_init(&(ah->mtx), (pthread_mutexattr_t *) NULL))
    error(1, "pthread_mutex_init");

  if(pthread_cond_init(&(ah->icv), (pthread_condattr_t *) NULL))
    error(1, "pthread_cond_init");

  if(pthread_cond_init(&(ah->ocv), (pthread_condattr_t *) NULL))
    error(1, "pthread_cond_init");

  if(pthread_attr_init(&attr))
    error(1, "pthread_attr_init");

  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

  if(pthread_create(&(ah->thr), &attr,
                    aobuf_thread, (void *) ah))
    error(1, "pthread_create");

  pthread_attr_destroy(&attr);

  ah->opened = 1;

  return(0);
}

void aobuf_free (struct aobuf_handle *ah) {
  pthread_join(ah->thr, (void **) NULL);

  pthread_mutex_destroy(&(ah->mtx));
  pthread_cond_destroy(&(ah->icv));
  pthread_cond_destroy(&(ah->ocv));

  free((void *) ah->buf);
  ah->buf = (char *) NULL;

  ah->opened = 0;
}

void aobuf_play (struct aobuf_handle *ah,
                 const FLAC__int32 * const buffer[],
                 unsigned int nq,
                 unsigned int nc) {
  unsigned int iq, i, n, c;
  char *p;

  iq = 0;

  while(nq > 0) {
    /* How much space in buffer? */
    pthread_mutex_lock(&(ah->mtx));

    while(ah->n >= AOBUFSIZE)
      pthread_cond_wait(&(ah->ocv), &(ah->mtx));

    n = AOBUFSIZE - ah->n;

    pthread_mutex_unlock(&(ah->mtx));  

    /* Make sure we don't wrap */
    if(n > AOBUFSIZE - ah->ip)
      n = AOBUFSIZE - ah->ip;

    /* Limit to what we have */
    if(n > nq)
      n = nq;

    /* Fill buffer */
    p = ah->buf + ah->ip * ah->bpq;

    for(i = 0; i < n; i++)
      for(c = 0; c < nc; c++) {
        memcpy(p, buffer[c] + iq + i, ah->bps);
        p += ah->bps;
      }

    iq += n;
    nq -= n;

    /* Update pointer */
    ah->ip += n;
    ah->ip &= AOBUFMASK;

    /* Update buffer info */
    pthread_mutex_lock(&(ah->mtx));

    if(ah->n < AOBUFHWM)
      pthread_cond_signal(&(ah->icv));

    ah->n += n;

    pthread_mutex_unlock(&(ah->mtx));
  }
}

void aobuf_finish (struct aobuf_handle *ah) {
  pthread_mutex_lock(&(ah->mtx));
  ah->finish = 1;
  pthread_cond_signal(&(ah->icv));
  pthread_mutex_unlock(&(ah->mtx));
}

unsigned long aobuf_tell (struct aobuf_handle *ah) {
  unsigned long rv;

  pthread_mutex_lock(&(ah->mtx));
  rv = ah->nplayed;
  pthread_mutex_unlock(&(ah->mtx));

  return(rv);
}
