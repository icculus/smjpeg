
#ifndef _SMJPEG_DECODE_H_
#define _SMJPEG_DECODE_H_

#include <sys/types.h>
#include <stdio.h>
#include <jpeglib.h>
#include "SDL.h"
#include "SDL_byteorder.h"

#define SMJPEG_AUDIO_BUFFERS    32
#define SMJPEG_AUDIO_MAX_CHUNK  4096

typedef struct SMJPEG {
    /* The data source */
    FILE *src;

    int at_end;     /* Non-zero if at the end of the stream */

    Uint32 start;   /* Playback start time */
    Uint32 current; /* Current time offset in stream */
    Uint32 length;  /* Total length in milliseconds */

    int use_timing; /* Non-zero if time synchronization is done */

    /* Status information block (code < 0 when an error occurs) */
    struct {
        int code;
        char message[1024];
    } status;

    /* Audio information block */
    struct {
        int enabled;
        int rate;
        int bits;
        int channels;

        /* Output buffer information */
        Uint8 encoding[4];
        struct dataring {
            int read;
            int write;
            int used;
            struct {
                int len;
                Uint8 buf[SMJPEG_AUDIO_MAX_CHUNK];
            } ringbuf[SMJPEG_AUDIO_BUFFERS]; 
        } ring;
    } audio;

    /* Video information block */
    struct {
        int enabled;
        Uint32 frames;  /* Length in video frames */
        int ms_per_frame;
        int width;
        int height;
        Uint32 frame;   /* Current frame */

        /* Output target information */
        Uint8 encoding[4];
        SDL_mutex *target_lock;
        int doubled;
        int target_x;
        int target_y;
        SDL_Surface *target;
        Uint8 **target_rows;
        void (*target_update)(SDL_Surface *target, int x, int y, unsigned int w, unsigned int h);
    } video;

    /* JFIF decode information */
    int jpeg_colorspace;
    struct jpeg_error_mgr jpeg_errmgr;
    struct smjpeg_source_mgr {
        struct jpeg_source_mgr pub;

        struct SMJPEG *movie;
        FILE *stream;
        Uint32 length;
        Uint8 buffer[4096];
    } jpeg_srcmgr;
    struct jpeg_decompress_struct jpeg_cinfo;

} SMJPEG;


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */ 
/* The library API interface for the SMJPEG decoder                  */
/*                                                                   */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */ 

#ifdef __cplusplus
extern "C" {
#endif

extern int SMJPEG_load(SMJPEG *movie, const char *file);

extern void SMJPEG_free(SMJPEG *movie);

/* Turn on or off pixel doubling for SMJPEG display.
   You must call SMJPEG_target() after you call this function.
 */
extern void SMJPEG_double(SMJPEG *movie, int state);

/* Set the target display for video playback of an SMJPEG video */
extern int SMJPEG_target(SMJPEG *movie,
       SDL_mutex *lock, int x, int y, SDL_Surface *target,
       void (*update)(SDL_Surface *, int, int, unsigned int, unsigned int));

/* Seek to a particular offset in the MJPEG stream */
extern int SMJPEG_seek(SMJPEG *movie, Uint32 ms);

/* Functions for saving the current position and restoring it */
extern Uint32 SMJPEG_getposition(SMJPEG *movie);
extern void SMJPEG_setposition(SMJPEG *movie, Uint32 pos);

/* Rewind to the start of an MJPEG stream */
extern void SMJPEG_rewind(SMJPEG *movie);

/* Start the playback of a movie, optionally specifying time synchronization */
extern void SMJPEG_start(SMJPEG *movie, int use_timing);

/* Advance the specified number of frames, or the whole movie if -1 */
extern int SMJPEG_advance(SMJPEG *movie, int num_frames, int do_wait);

/* Stop playback of a movie */
extern void SMJPEG_stop(SMJPEG *movie);

/* Function that can be passed to SDL as an audio callback */
extern void SMJPEG_feedaudio(void *udata, Uint8 *stream, int len);

#ifdef __cplusplus
};
#endif

#endif /* _SMJPEG_DECODE_H_ */
