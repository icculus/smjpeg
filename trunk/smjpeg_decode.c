
/* This file decodes a single frame of SMJPEG animation */

#include <errno.h>
#include <stdarg.h>
#include <string.h>

#include "adpcm.h"
#include "smjpeg_file.h"
#include "smjpeg_decode.h"

/* Only define this when analyzing the performance on slow systems */
/*#define DEBUG_TIMING*/

/* Macro for detecting the end of the SMJPEG stream */
#define END_OF_STREAM(movie, magic) \
    (movie->at_end || feof(movie->src) || MAGIC_EQUALS(magic, DATA_END_MAGIC))

/* Return values for block parsing functions */
enum {
    EARLY_RETURN = -1,
    BLOCK_SKIPPED = 0,
    BLOCK_PLAYED = 1
};

/* Function for setting the status of an SMJPEG stream */
static void SMJPEG_status(SMJPEG *movie, int code, char *fmt, ...)
{
    va_list ap;

    movie->status.code = code;
    if ( fmt ) {
        va_start(ap, fmt);
        vsnprintf(movie->status.message,(sizeof movie->status.message),fmt,ap);
        va_end(ap);
    } else {
        movie->status.message[0] = '\0';
    }
}

/* Called by jpeg_read_header before any data is actually read */
static void jpegsrc_init (j_decompress_ptr cinfo)
{
    return;
}

/*
 * Fill the input buffer --- called whenever buffer is emptied.
 */
static int jpegsrc_fill (j_decompress_ptr cinfo)
{
    struct smjpeg_source_mgr *src = (struct smjpeg_source_mgr *)cinfo->src;
    Uint32 length;

    /* Get the data */
    length = sizeof(src->buffer);
    if ( length > src->length ) {
        length = src->length;
    }
    if ( ! fread(src->buffer, length, 1, src->stream) ) {
        /* Uh oh.. */
        SMJPEG_status(src->movie, -1, "Truncated SMJPEG file - aborting.");
        return(FALSE);
    }

    /* Update the length */
    src->length -= length;

    /* Check for end-of-stream */
    if ( length == 0 ) {
        /* Insert a fake EOI marker */
        src->buffer[0] = (Uint8) 0xFF;
        src->buffer[1] = (Uint8) JPEG_EOI;
        length = 2;
    }

    /* Set up the JPEG read pointer */
    src->pub.next_input_byte = src->buffer;
    src->pub.bytes_in_buffer = length;
    return(TRUE);
}

/*
 * Skip data --- used to skip over a potentially large amount of
 * uninteresting data (such as an APPn marker).
 */
static void jpegsrc_skip (j_decompress_ptr cinfo, long num_bytes)
{
    struct smjpeg_source_mgr *src = (struct smjpeg_source_mgr *)cinfo->src;

    if ( num_bytes > 0 ) {
        while ( num_bytes > (long) src->pub.bytes_in_buffer ) {
            num_bytes -= (long) src->pub.bytes_in_buffer;
            src->pub.fill_input_buffer(cinfo);
            /* note we assume that fill_input_buffer will never
             * return FALSE, so suspension need not be handled.
             */
        }
        src->pub.next_input_byte += (size_t) num_bytes;
        src->pub.bytes_in_buffer -= (size_t) num_bytes;
    }
}

/* Called by jpeg_finish_decompress after all data has been read */
static void jpegsrc_quit (j_decompress_ptr cinfo)
{
    return;
}

static void jpeg_smjpeg_src (j_decompress_ptr cinfo, SMJPEG *movie)
{
    struct smjpeg_source_mgr *src;

    src = &movie->jpeg_srcmgr;
    cinfo->src = (struct jpeg_source_mgr *)src;
    src->movie = movie;
    src->pub.init_source = jpegsrc_init;
    src->pub.fill_input_buffer = jpegsrc_fill;
    src->pub.skip_input_data = jpegsrc_skip;
    src->pub.resync_to_restart = jpeg_resync_to_restart; /* default method */
    src->pub.term_source = jpegsrc_quit;
    src->stream = movie->src;
    src->pub.bytes_in_buffer = 0; /* forces fill_input_buffer on first read */
    src->pub.next_input_byte = NULL; /* until buffer loaded */
}

void SMJPEG_free(SMJPEG *movie)
{
    if ( movie->src ) {
        fclose(movie->src);
        movie->src = NULL;
    }
    if ( movie->video.target_rows ) {
        free(movie->video.target_rows);
        movie->video.target_rows = NULL;
    }
}

int SMJPEG_load(SMJPEG *movie, const char *file)
{
    const Uint8 smjpeg_magic[] = { '\0', '\n', 'S','M','J','P','E','G' };
    Uint32 version;
    Uint8 buffer[BUFSIZ];
    Uint32 length;

    /* Clear everything out */
    memset(movie, 0, (sizeof *movie));

    /* Open the SMJPEG file */
    movie->src = fopen(file, "r");
    if ( movie->src == NULL ) {
        SMJPEG_status(movie,-1, "Couldn't open %s: %s", file, strerror(errno));
        goto error_return;
    }

    /* Load the SMJPEG header */
    if ( ! fread(buffer, sizeof(smjpeg_magic), 1, movie->src) ||
         (memcmp(buffer, smjpeg_magic, (sizeof smjpeg_magic)) != 0) ) {
        SMJPEG_status(movie, -1, "%s is not an SMJPEG animation", file);
        goto error_return;
    }
    READ32(version, movie->src);
    if ( version != SMJPEG_FORMAT_VERSION ) {
        SMJPEG_status(movie, -1, "Unknown SMJPEG file version (%d)", version);
        goto error_return;
    }
    READ32(movie->length, movie->src);

    /* Load additional media headers */
    do {
        if ( ! fread(buffer, 4, 1, movie->src) ) {
            SMJPEG_status(movie, -1, "Short read while loading header");
            goto error_return;
        }
        if ( MAGIC_EQUALS(buffer, AUDIO_HEADER_MAGIC) ) {
            READ32(length, movie->src);
            movie->audio.enabled = 1;
            READ16(movie->audio.rate, movie->src);
            READ8(movie->audio.bits, movie->src);
            READ8(movie->audio.channels, movie->src);
            fread(movie->audio.encoding, 4, 1, movie->src);
            if ( ! MAGIC_EQUALS(movie->audio.encoding, AUDIO_ENCODING_NONE) &&
                 ! MAGIC_EQUALS(movie->audio.encoding, AUDIO_ENCODING_ADPCM) ) {
                SMJPEG_status(movie, 0,
                            "Warning: Unknown audio encoding (%c%c%c%c)\n",
                            movie->audio.encoding[0], movie->audio.encoding[1],
                            movie->audio.encoding[2], movie->audio.encoding[3]);
                movie->audio.enabled = 0;
            }
        }
        if ( MAGIC_EQUALS(buffer, VIDEO_HEADER_MAGIC) ) {
            READ32(length, movie->src);
            movie->video.enabled = 1;
            READ32(movie->video.frames, movie->src);
            movie->video.ms_per_frame = movie->length/movie->video.frames;
            READ16(movie->video.width, movie->src);
            READ16(movie->video.height, movie->src);
            movie->video.frame = 0;
            fread(movie->video.encoding, 4, 1, movie->src);
            if ( ! MAGIC_EQUALS(movie->video.encoding, VIDEO_ENCODING_JPEG) ) {
                SMJPEG_status(movie, 0,
                            "Warning: Unknown video encoding (%c%c%c%c)\n",
                            movie->video.encoding[0], movie->video.encoding[1],
                            movie->video.encoding[2], movie->video.encoding[3]);
                movie->video.enabled = 0;
            }
            movie->video.target_rows =
              (Uint8 **)malloc(movie->video.height*sizeof(Uint8 *));
            if ( movie->video.target_rows == NULL ) {
                SMJPEG_status(movie, -1, "Out of memory");
                goto error_return;
            }
        }
    } while ( ! MAGIC_EQUALS(buffer, HEADER_END_MAGIC) );

    /* Reset any other values needed for playing */
    SMJPEG_rewind(movie);

    /* Initialize JPEG decoder */
    movie->jpeg_cinfo.err = jpeg_std_error(&movie->jpeg_errmgr);
    jpeg_create_decompress(&movie->jpeg_cinfo);
    jpeg_smjpeg_src(&movie->jpeg_cinfo, movie);

    /* Perform fast decoding */
    movie->jpeg_cinfo.dct_method = JDCT_FASTEST;
    movie->jpeg_cinfo.do_fancy_upsampling = FALSE;

    /* Successful header load! */
    return(0);

error_return:
    if ( movie->src ) {
        fclose(movie->src);
    }
    return(-1);
}

/* Turn on or off pixel doubling for SMJPEG display.
   You must call SMJPEG_target() after you call this function.
 */
void SMJPEG_double(SMJPEG *movie, int state)
{
    movie->video.doubled = state;
}

/* Set the target display for video playback of an SMJPEG video */
int SMJPEG_target(SMJPEG *movie,
       SDL_mutex *lock, int x, int y, SDL_Surface *target,
       void (*update)(SDL_Surface *, int, int, unsigned int, unsigned int))
{
    int row;
    int pitch;

    if ( ((x+movie->video.width) > target->w) ||
         ((y+movie->video.height) > target->h) ) {
        SMJPEG_status(movie, -1, "Target area not within target surface");
        return(-1);
    }
    switch (target->format->BitsPerPixel) {
        case 15:
        case 16:
            if ( (target->format->Rmask == 0x7C00) &&
                 (target->format->Gmask == 0x03E0) &&
                 (target->format->Bmask == 0x001F) ) {
                if ( movie->video.doubled ) {
                    movie->jpeg_colorspace = JCS_RGB16_555_DBL;
                } else {
                    movie->jpeg_colorspace = JCS_RGB16_555;
                }
            } else {
                if ( movie->video.doubled ) {
                    movie->jpeg_colorspace = JCS_RGB16_565_DBL;
                } else {
                    movie->jpeg_colorspace = JCS_RGB16_565;
                }
            }
            break;
        case 24:
            if ( (target->format->Rmask == 0x0000FF) &&
                 (target->format->Gmask == 0x00FF00) &&
                 (target->format->Bmask == 0xFF0000) ) {
                if ( movie->video.doubled ) {
                    SMJPEG_status(movie, -1,
                        "Doubling not supported on 24-bit target");
                    return(-1);
                } else {
                    movie->jpeg_colorspace = JCS_RGB;
                }
                break;
            }
        default:
            SMJPEG_status(movie, -1, "Unsupported target color format");
            return(-1);
    }
    movie->video.target = target;
    movie->video.target_lock = lock;
    movie->video.target_x = x;
    movie->video.target_y = y;
    movie->video.target_rows[0] = (Uint8 *)target->pixels + y*target->pitch +
                                            x*target->format->BytesPerPixel;
    if ( movie->video.doubled ) {
        pitch = target->pitch * 2;
    } else {
        pitch = target->pitch;
    }
    for ( row=1; row<movie->video.height; ++row ) {
        movie->video.target_rows[row] = movie->video.target_rows[row-1] + pitch;
    }
    movie->video.target_update = update;

    return(0);
}

/* Private function to display a frame of JFIF encoded animation
   - the FILE pointer is assumed to be at the start of a jpeg frame
 */
static void SMJPEG_displayJFIF(SMJPEG *movie)
{
    struct jpeg_decompress_struct *cinfo;

    /* Initialize the source manager */
    READ32(movie->jpeg_srcmgr.length, movie->src);
    movie->jpeg_srcmgr.pub.bytes_in_buffer = 0;
    movie->jpeg_srcmgr.pub.next_input_byte = NULL;

    /* Skip the video frame if video is not enabled */
    if ( ! movie->video.enabled ) {
        fseek(movie->src, movie->jpeg_srcmgr.length, SEEK_CUR);
        return;
    }

    /* Start the decompression engine */
    cinfo = &movie->jpeg_cinfo;
    jpeg_read_header(cinfo, TRUE);
    cinfo->dct_method = JDCT_IFAST;
    cinfo->out_color_space = movie->jpeg_colorspace;

    /* Lock the display target, if necessary */
    if ( movie->video.target_lock ) {
        SDL_mutexP(movie->video.target_lock);
    }

    /* Decompress to the target surface */
    jpeg_start_decompress(cinfo);
    while ( cinfo->output_scanline < cinfo->output_height ) {
        jpeg_read_scanlines(cinfo,
                        &movie->video.target_rows[cinfo->output_scanline],
                                cinfo->output_height-cinfo->output_scanline);
    }
    if ( movie->video.doubled ) {
        int row;

        for ( row=0; row < movie->video.height; ++row ) {
            memcpy(movie->video.target_rows[row]+movie->video.target->pitch,
                movie->video.target_rows[row], movie->video.target->pitch);
        }
    }
    jpeg_finish_decompress(cinfo);

    /* Update the screen */
    if ( movie->video.target_update ) {
        if ( movie->video.doubled ) {
            movie->video.target_update(movie->video.target,
                               movie->video.target_x, movie->video.target_y,
                               2*cinfo->output_width, 2*cinfo->output_height);
        } else {
            movie->video.target_update(movie->video.target,
                               movie->video.target_x, movie->video.target_y,
                               cinfo->output_width, cinfo->output_height);
        }
    }

    /* Unlock the display target, if necessary */
    if ( movie->video.target_lock ) {
        SDL_mutexV(movie->video.target_lock);
    }
}

/* Seek to a particular offset in the MJPEG stream
   - we take the easy route and seek from the beginning
*/
int SMJPEG_seek(SMJPEG *movie, Uint32 ms)
{
    Uint8 magic[8];
    Uint32 length;

    /* Seek to the beginning */
    movie->audio.ring.used = 0;
    SMJPEG_stop(movie);
    if ( fseek(movie->src, 0, SEEK_SET) < 0 ) {
        return(-1);
    }
    movie->current = 0;
    movie->video.frame = 0;

    /* Skip SMJPEG header */
    fread(magic, 8, 1, movie->src);
    READ32(length, movie->src);
    READ32(length, movie->src);
    do {
        if ( fread(magic, 4, 1, movie->src) ) {
            if ( ! MAGIC_EQUALS(magic, HEADER_END_MAGIC) ) {
                READ32(length, movie->src);
                fseek(movie->src, length, SEEK_CUR);
            }
        }
    } while ( !feof(movie->src) && ! MAGIC_EQUALS(magic, HEADER_END_MAGIC) );

    /* Seek to the proper time offset */
    length = 0;
    movie->at_end = 0;
    do {
        /* Seek past last chunk */
        if ( MAGIC_EQUALS(magic, VIDEO_DATA_MAGIC) ) {
            ++movie->video.frame;
        }
        fseek(movie->src, length, SEEK_CUR);

        /* Read magic, timestamp, and chunk length */
        if ( fread(magic, 4, 1, movie->src) ) {
            if ( MAGIC_EQUALS(magic, DATA_END_MAGIC) ) {
                /* Back up so it will be read again */
                fseek(movie->src, -4, SEEK_CUR);
            } else {
                READ32(movie->current, movie->src);
                READ32(length, movie->src);
            }
        }
    } while ( !END_OF_STREAM(movie, magic) && (ms < movie->current) );

    /* If we haven't reached end, we need to back up to start of chunk */
    if ( !END_OF_STREAM(movie, magic) ) {
        fseek(movie->src, -12, SEEK_CUR);
    }
    movie->at_end = 1;

    /* We're done... */
    return(0);
}

/* Rewind to the start of an MJPEG stream */
void SMJPEG_rewind(SMJPEG *movie)
{
    SMJPEG_seek(movie, 0);
}

/* Start the playback of a movie, optionally specifying time synchronization */
void SMJPEG_start(SMJPEG *movie, int use_timing)
{
    movie->use_timing = use_timing;
    if ( use_timing ) {
        movie->start = (Sint32)SDL_GetTicks();
    }
    movie->at_end = 0;
}

/* Functions for saving the current position and restoring it */
Uint32 SMJPEG_getposition(SMJPEG *movie)
{
    return ftell(movie->src);
}
void SMJPEG_setposition(SMJPEG *movie, Uint32 pos)
{
    fseek(movie->src, pos, SEEK_SET);
    movie->at_end = 0;
}

static int SkipBlock(SMJPEG *movie, const char *magic)
{
    Uint32 length;

    /* Skip past the body of the data chunk */
    READ32(length, movie->src);
    fseek(movie->src, length, SEEK_CUR);
#ifdef DEBUG_TIMING
printf("Skipping chunk\n");
#endif
    return(BLOCK_SKIPPED);
}

static int ParseAudio(SMJPEG *movie)
{
    struct dataring *ring;
    Uint32 length;
    Uint32 extra;

    /* Wait for a while if the audio buffer is full */
    ring = &movie->audio.ring;
    if ( ring->used == SMJPEG_AUDIO_BUFFERS ) {
        /* Uh oh, the audio is way behind... */
#ifdef DEBUG_TIMING
printf("Waiting for audio queue to empty\n");
#endif
        while ( (ring->used == SMJPEG_AUDIO_BUFFERS) && movie->audio.enabled ) {
            SDL_Delay(10);
        }
    }

    /* Copy audio data into ring buffer and increment */
    READ32(length, movie->src);
    if ( length > SMJPEG_AUDIO_MAX_CHUNK ) {
        /* Silently truncate overlarge chunks */
        extra = (length-SMJPEG_AUDIO_MAX_CHUNK);
        length = SMJPEG_AUDIO_MAX_CHUNK;
    } else {
        extra = 0;
    }
    /* Handle ADPCM compressed audio data */
    if ( MAGIC_EQUALS(movie->audio.encoding, AUDIO_ENCODING_ADPCM) ) {
        struct adpcm_state adpcm;
        Uint8 unused;
        Uint8 encoded[SMJPEG_AUDIO_MAX_CHUNK];

        /* Read the predictor values for this packet */
        READ16(adpcm.valprev, movie->src);
        READ8(adpcm.index, movie->src);
        READ8(unused, movie->src);

        /* Read the encoded data */
        length -= 4;
        fread(encoded, length, 1, movie->src);

        /* Decode and queue the data */
        length *= 4;
        ring->ringbuf[ring->write].len = length;
        adpcm_decoder(encoded, (short *)ring->ringbuf[ring->write].buf,
                                       length/2, &adpcm);
    } else {
        /* Just read the data into the queue */
        ring->ringbuf[ring->write].len = length;
        fread(ring->ringbuf[ring->write].buf, length, 1, movie->src);
    }
    ring->write = (ring->write+1)%SMJPEG_AUDIO_BUFFERS;
    ++ring->used;

    /* Seek past extra data, if we overflowed */
    if ( extra ) {
        fseek(movie->src, extra, SEEK_CUR);
    }
    return(BLOCK_PLAYED);
}

static int ParseVideo(SMJPEG *movie)
{
    /* For now, only JPEG is supported */
    SMJPEG_displayJFIF(movie);
    return(BLOCK_PLAYED);
}

static int ParseBlock(SMJPEG *movie, int do_wait)
{
    const int TIMESLICE = 10;       /* OS timeslice, in milliseconds */
    Uint8 magic[4];
    Uint32 min_timestamp;
    Uint32 max_timestamp;
    Sint32 timenow;

    /* Read this chunk type */
    if ( !fread(magic,4,1,movie->src) || MAGIC_EQUALS(magic,DATA_END_MAGIC) ) {
        movie->at_end = 1;
        if ( !feof(movie->src) ) {
            fseek(movie->src, -4, SEEK_CUR);
        }
        return(EARLY_RETURN);
    } 

    /* Perform block accounting */
    if ( MAGIC_EQUALS(magic, VIDEO_DATA_MAGIC) ) {
        ++movie->video.frame;
    }

    /* Check the timestamps, and do timing work */
    READ32(min_timestamp, movie->src);
    //READ32(max_timestamp, movie->src);
    max_timestamp = min_timestamp+90;
    if ( movie->use_timing ) {
        timenow = SDL_GetTicks() - movie->start;

#ifdef DEBUG_TIMING
//printf("Time now: %d, timestamp: %d\n", timenow, min_timestamp);
#endif
        if ( timenow > max_timestamp ) {
            SkipBlock(movie, magic);
            return(BLOCK_SKIPPED);
        }
    }
    movie->current = min_timestamp;

    /* Time to handle data -- handle known data packets */
    if ( MAGIC_EQUALS(magic, AUDIO_DATA_MAGIC) ) {
        return(ParseAudio(movie));
    }
    if ( MAGIC_EQUALS(magic, VIDEO_DATA_MAGIC) ) {
        /* The video is the bounding stream */
        if ( movie->use_timing ) {
            if ( timenow < min_timestamp ) {
                if ( do_wait ) {
                    int timediff = min_timestamp - timenow;
                    if ( timediff > TIMESLICE ) {
                        timediff -= TIMESLICE;
#ifdef DEBUG_TIMING
printf("Sleeping for %d milliseconds\n", timediff);
#endif
                        SDL_Delay(timediff);
                    }
                } else {
                    /* Seek to beginning of chunk */
                    fseek(movie->src, -8, SEEK_CUR);
                    return(EARLY_RETURN);
                }
            }
        }
        return(ParseVideo(movie));
    }

    /* Unknown data chunk */
    SkipBlock(movie, magic);
    return(BLOCK_SKIPPED);
}

/* Advance the specified number of frames, or the whole movie if -1 */
/* FIXME:  Clean up this mess a little bit */
int SMJPEG_advance(SMJPEG *movie, int num_frames, int do_wait)
{
    int status;

    while ( num_frames && !movie->at_end ) {
        status = ParseBlock(movie, do_wait);
        switch (status) {
            case BLOCK_PLAYED:
                --num_frames;
                break;
            case BLOCK_SKIPPED:
                break;
            case EARLY_RETURN:
                num_frames = 0;
                break;
        }
    }
    return(status == BLOCK_PLAYED);
}

/* Stop playback of a movie */
void SMJPEG_stop(SMJPEG *movie)
{
    /* Wait for the audio to get flushed */
    while ( (movie->audio.ring.used > 0) && movie->audio.enabled ) {
        SDL_Delay(10);
    }
    movie->at_end = 1;
}

void SMJPEG_feedaudio(void *udata, Uint8 *stream, int len)
{
    SMJPEG *movie = (SMJPEG *)udata;
    struct dataring *ring;

    ring = &movie->audio.ring;
    if ( movie->audio.enabled && ring->used ) {
        while ( ring->used && (len > 0) ) {
            if ( ring->ringbuf[ring->read].len <= len ) {
                memcpy(stream, ring->ringbuf[ring->read].buf,
                                ring->ringbuf[ring->read].len);
                ring->read = (ring->read+1)%SMJPEG_AUDIO_BUFFERS;
                --ring->used;
                len -= ring->ringbuf[ring->read].len;
            } else {
                memcpy(stream, ring->ringbuf[ring->read].buf, len);
                /* WARNING: requires an overlapping memcpy */
                ring->ringbuf[ring->read].len -= len;
                memcpy(ring->ringbuf[ring->read].buf,
                            &ring->ringbuf[ring->read].buf[len],
                            ring->ringbuf[ring->read].len);
                len = 0;
            }
        }
    }
}
