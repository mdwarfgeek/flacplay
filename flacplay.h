#ifndef FLACPLAY_H
#define FLACPLAY_H

#include <pthread.h>

#include <all.h>  /* FLAC */
#include <ao/ao.h>

/* Audio buffer size in quanta (times), must be a power of two */
#define AOBUFSIZE (1<<18)  /* 1MB for 2ch, 16 bits/sample */
#define AOBUFMASK (AOBUFSIZE-1)

/* Watermark to start playback */
#define AOBUFHWM (1<<17)

/* Maximum number of quanta to send to libao at a time */
#define AOMAXQ (1<<12)

struct aobuf_handle {
  int aodriver;
  struct ao_option *aoopt;
  ao_sample_format aofmt;

  pthread_t thr;
  pthread_mutex_t mtx;
  pthread_cond_t icv;
  pthread_cond_t ocv;

  char *buf;
  unsigned int ip;
  unsigned int op;
  unsigned int n;    /* number of quanta in buffer */
  unsigned int bps;  /* bytes per sample */
  unsigned int bpq;  /* bytes per quantum */

  unsigned long nplayed;

  unsigned char opened;
  unsigned char run;
  unsigned char finish;
};

int aobuf_init (struct aobuf_handle *ah,
                int aodriver,
                struct ao_option *aoopt);
void aobuf_free (struct aobuf_handle *ah);

void aobuf_play (struct aobuf_handle *ah,
                 const FLAC__int32 * const buffer[],
                 unsigned int nq,
                 unsigned int nc);
void aobuf_finish (struct aobuf_handle *ah);
unsigned long aobuf_tell (struct aobuf_handle *ah);

#endif  /* FLACPLAY_H */
