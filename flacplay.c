#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <unistd.h>

#include <all.h>  /* FLAC */
#include <ao/ao.h>

#include "util.h"

struct file_info {
  unsigned char abort_flag;

  unsigned long nsamples;
  unsigned int bits;
  unsigned int bytes;
  unsigned int rate;
  unsigned int channels;
  unsigned long length;  /* in 1/100 sec */

  unsigned long cursamp;
  unsigned long curpos;
  unsigned char inum;
};

extern char *optarg;
extern int optind;

static unsigned char vflag = 0;
static unsigned char qflag = 0;

static int aodriver = -1;
static ao_device *aodev = (ao_device *) NULL;
static ao_sample_format aofmt;
static char aobuf[128 * 1024];  /* 128k */
static unsigned int aobufpos = 0;

static struct file_info inf;

static int play_file (FLAC__StreamDecoder *decoder,
		      char *filename, unsigned long skip_hsec, char *errstr);

static FLAC__StreamDecoderWriteStatus
       write_callback (const FLAC__StreamDecoder *decoder,
		       const FLAC__Frame *frame,
		       const FLAC__int32 * const buffer[],
		       void *client_data);
static void metadata_callback (const FLAC__StreamDecoder *decoder,
			       const FLAC__StreamMetadata *metadata,
			       void *client_data);
static void error_callback (const FLAC__StreamDecoder *decoder,
			    FLAC__StreamDecoderErrorStatus status,
			    void *client_data);

static void usage (char *av) {
  fprintf(stderr, "Usage:\t%s [-d device] [-k secs] [-qv] file [...]\n", av);
  exit(1);
}

int main (int argc, char *argv[]) {
  char *pn = (char *) NULL, errstr[ERRSTR_LEN], *avzero, *ep;
  int c, f;

  float skip_sec = 0.0;
  unsigned long skip_hsec = 0;

  FLAC__StreamDecoder *decoder;

  /* Set the program name for error reporting */
  if(argv[0])
    pn = basename(argv[0]);

  if(!pn)
    pn = "flacplay";

  setprogname(pn);

  avzero = argv[0];

  /* Get default libao driver */
  ao_initialize();

  aodriver = ao_default_driver_id();
  if(aodriver == -1)
    /* Try to get the null device instead */
    aodriver = ao_driver_id("null");

  /* Extract command-line arguments */
  while((c = getopt(argc, argv, "d:k:qv")) != -1)
    switch(c) {
    case 'd':
      aodriver = ao_driver_id(optarg);
      if(aodriver == -1)
	fatal(1, "unknown driver: %s", optarg);
      break;
    case 'k':
      skip_sec = (float) strtod(optarg, &ep);
      if(*ep || !*optarg || skip_sec < 0.0)
	fatal(1, "bad skip argument: %s: must be a positive number", optarg);
      skip_hsec = (unsigned long) ceil(skip_sec * 100);
      break;
    case 'q':
      qflag++;
      break;
    case 'v':
      vflag++;
      break;
    case '?':
    default:
      usage(avzero);
    }

  argc -= optind;
  argv += optind;

  if(argc < 1)
    usage(avzero);

  /* Make sure we got a libao driver */
  if(aodriver == -1)
    fatal(1, "could not find audio output driver");

  /* Create a new decoder instance */
  decoder = FLAC__stream_decoder_new();
  if(!decoder)
    fatal(1, "could not initialise FLAC decoder");

  /* Set stdout to unbuffered mode */
  setvbuf(stdout, (char *) NULL, _IONBF, 0);

  /* Play the files */
  for(f = 0; f < argc; f++) {
    if(play_file(decoder, argv[f], skip_hsec, errstr))
      fatal(1, "%s", errstr);
  }

  /* Done, clean up */
  if(decoder &&
     FLAC__stream_decoder_get_state(decoder) !=
     FLAC__STREAM_DECODER_UNINITIALIZED)
    FLAC__stream_decoder_finish(decoder);

  FLAC__stream_decoder_delete(decoder);
  decoder = (FLAC__StreamDecoder *) NULL;

  if(aodev) {
    if(ao_close(aodev) == 0)
      fatal(1, "error closing audio output device");
    aodev = (ao_device *) NULL;
  }

  ao_shutdown();

  return(0);
}

static int play_file (FLAC__StreamDecoder *decoder,
		      char *filename, unsigned long skip_hsec, char *errstr) {
  FLAC__StreamDecoderState state;

  /* Clean up any existing decoder state */
  if(decoder &&
     FLAC__stream_decoder_get_state(decoder) !=
     FLAC__STREAM_DECODER_UNINITIALIZED)
    FLAC__stream_decoder_finish(decoder);

  /* Setup decoder for this file */
  inf.abort_flag = 0;

  FLAC__stream_decoder_set_md5_checking(decoder, 0);
  FLAC__stream_decoder_set_metadata_ignore_all(decoder);
  FLAC__stream_decoder_set_metadata_respond(decoder,
					    FLAC__METADATA_TYPE_STREAMINFO);
  if(FLAC__stream_decoder_init_file(decoder, filename,
				    write_callback,
				    metadata_callback,
				    error_callback,
				    NULL)
     != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
    report_err(errstr, "could not initialise FLAC decoder");
    goto error;
  }

  /* Read the metadata */
  if(!FLAC__stream_decoder_process_until_end_of_metadata(decoder)) {
    report_err(errstr, "could not read file metadata");
    goto error;
  }

  /* Setup output device if necessary */
  if(!aodev ||
     (inf.bits != aofmt.bits ||
      inf.rate != aofmt.rate ||
      inf.channels != aofmt.channels)) {
    if(aodev) {
      if(ao_close(aodev) == 0) {
	report_err(errstr, "error closing audio output device");
	goto error;
      }
    }

    aofmt.bits = inf.bits;
    aofmt.rate = inf.rate;
    aofmt.channels = inf.channels;
    aofmt.byte_format = AO_FMT_NATIVE;

    aodev = ao_open_live(aodriver, &aofmt, (ao_option *) NULL);
    if(!aodev) {
      report_err(errstr, "could not open audio output device");
      goto error;
    }
  }

  /* Seek to desired start position */
  inf.cursamp = (skip_hsec * inf.rate) / 100;
  inf.curpos = skip_hsec;
  inf.inum = 0;

  if(inf.cursamp > 0) {
    if(inf.cursamp > inf.nsamples) {
      report_err(errstr, "desired start position is past end of file");
      goto error;
    }

    if(!FLAC__stream_decoder_seek_absolute(decoder, inf.cursamp)) {
      report_err(errstr, "could not seek to sample %ld", inf.cursamp);
      goto error;
    }
  }

  /* Play the file */
  FLAC__stream_decoder_process_until_end_of_stream(decoder);

  if(vflag)
    printf("\n");

  /* Check state */
  state = FLAC__stream_decoder_get_state(decoder);
  if(state != FLAC__STREAM_DECODER_END_OF_STREAM) {
    report_err(errstr, "error decoding file");
    goto error;
  }

  /* Done */
  FLAC__stream_decoder_finish(decoder);

  return(0);

 error:

  return(1);
}

static FLAC__StreamDecoderWriteStatus
       write_callback (const FLAC__StreamDecoder *decoder,
		       const FLAC__Frame *frame,
		       const FLAC__int32 * const buffer[],
		       void *client_data) {
  unsigned long samp, nsamp;
  unsigned int chan, nchan;

  unsigned long tmp;
  unsigned int el_min, el_sec, el_hsec, rem_min, rem_sec, rem_hsec;

  /* Check flags */
  if(inf.abort_flag)
    return(FLAC__STREAM_DECODER_WRITE_STATUS_ABORT);

  /* Extract relevant frame info */
  nsamp = frame->header.blocksize;
  nchan = frame->header.channels;

  /* Copy into audio buffer */
  aobufpos = 0;

  for(samp = 0; samp < nsamp; samp++)
    for(chan = 0; chan < nchan; chan++) {
      if(sizeof(aobuf) - aobufpos < inf.bytes)
	fatal(1, "buffer overflow");

      memcpy(aobuf + aobufpos, buffer[chan] + samp, inf.bytes);
      aobufpos += inf.bytes;
    }

  /* Play samples */
  if(!ao_play(aodev, aobuf, aobufpos))
    return(FLAC__STREAM_DECODER_WRITE_STATUS_ABORT);

  /* Update time */
  inf.cursamp += nsamp;
  inf.curpos = inf.cursamp / (inf.rate / 100);

  if(vflag && inf.inum == 2) {
    tmp = inf.curpos;

    el_min = tmp / 6000;
    tmp -= el_min * 6000;
    el_sec = tmp / 100;
    tmp -= el_sec * 100;
    el_hsec = tmp;

    tmp = inf.length - inf.curpos;

    rem_min = tmp / 6000;
    tmp -= rem_min * 6000;
    rem_sec = tmp / 100;
    tmp -= rem_sec * 100;
    rem_hsec = tmp;

    printf("\rTime: %02d:%02d.%02d [%02d:%02d.%02d]",
	   el_min, el_sec, el_hsec, rem_min, rem_sec, rem_hsec);

    inf.inum = 0;
  }

  inf.inum++;

  return(FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE);
}

static void metadata_callback (const FLAC__StreamDecoder *decoder,
			       const FLAC__StreamMetadata *metadata,
			       void *client_data) {
  if(metadata->type == FLAC__METADATA_TYPE_STREAMINFO) {
    /* We can only handle upto 4 Gsamples */
    FLAC__ASSERT(metadata->data.stream_info.total_samples < 0x100000000);
  
    /* Copy file info into structure */
    inf.nsamples = metadata->data.stream_info.total_samples & 0xffffffff;
    inf.bits = metadata->data.stream_info.bits_per_sample;
    inf.bytes = (inf.bits + 7) / 8;
    inf.rate = metadata->data.stream_info.sample_rate;
    inf.channels = metadata->data.stream_info.channels;
    inf.length = inf.nsamples / (inf.rate / 100);
  }
}

static void error_callback (const FLAC__StreamDecoder *decoder,
			    FLAC__StreamDecoderErrorStatus status,
			    void *client_data) {
  /* Abort if there was a real error */
  if(status != FLAC__STREAM_DECODER_ERROR_STATUS_LOST_SYNC)
    inf.abort_flag = 1;
}

