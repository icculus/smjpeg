
/* This file takes a stream of raw audio and a series of jpeg images
   and constructs a motion jpeg animation from it.
*/

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>

#include <jpeglib.h>

#include "adpcm.h"
#include "smjpeg_file.h"

/* Default audio encoding parameters */
#define DEFAULT_AUDIO_ENCODING  AUDIO_ENCODING_ADPCM
#define DEFAULT_AUDIO_RATE      22050
#define DEFAULT_AUDIO_BITS      16
#define DEFAULT_AUDIO_CHANNELS  1
#define DEFAULT_AUDIO_FRAME     512    /* Encode 512 samples per frame */

/* Default video encoding parameters */
#define DEFAULT_VIDEO_ENCODING  VIDEO_ENCODING_JPEG
#define DEFAULT_VIDEO_FPS       15.0

#define DEFAULT_JPEG_PREFIX    "input/frame"
#define DEFAULT_AUDIO_INPUT    "audio.raw"
#define DEFAULT_OUTPUT_FILE    "output.mjpg"

typedef unsigned char  Uint8;
typedef unsigned short Uint16;
typedef unsigned int   Uint32;

/* Open a JPEG file and get the image width and height */
int get_jpeg_dimensions(const char *file, Uint16 *w, Uint16 *h)
{
    FILE *input;
    int status;

    status = -1;
    input = fopen(file, "r");
    if ( input ) {
        struct jpeg_error_mgr errmgr;
        struct jpeg_decompress_struct cinfo;

        cinfo.err = jpeg_std_error(&errmgr);
        jpeg_create_decompress(&cinfo);
        jpeg_stdio_src(&cinfo, input);
        jpeg_read_header(&cinfo, TRUE);
        cinfo.out_color_space = JCS_RGB;
        jpeg_calc_output_dimensions(&cinfo);
        *w = cinfo.output_width;
        *h = cinfo.output_height;
        jpeg_destroy_decompress(&cinfo);
        status = 0;
    }
    return(status);
}

static struct adpcm_state adpcm;

int WriteAudioChunk(FILE *input, double timestamp, Uint32 size,
                             const char *encoding, FILE *output)
{
    Uint8 buffer[BUFSIZ];
    int len;

//fprintf(stderr, "A");
    fwrite(AUDIO_DATA_MAGIC, 4, 1, output);
    WRITE32((Uint32)timestamp, output);
    if ( MAGIC_EQUALS(encoding, AUDIO_ENCODING_ADPCM) ) {
        WRITE32(4+(size/4), output);
        WRITE16(adpcm.valprev, output);
        WRITE8(adpcm.index, output);
        WRITE8(0, output);
    } else {
        WRITE32(size, output);
    }
    while ( size > 0 ) {
        if ( size < BUFSIZ ) {
            len = fread(buffer, 1, size, input);
        } else {
            len = fread(buffer, 1, BUFSIZ, input);
        }
        if ( len < 0 ) {
            if ( ferror(input) ) {
                fprintf(stderr, "Error reading input data\n");
            }
            return(-1);
        }
        size -= len;
        if ( MAGIC_EQUALS(encoding, AUDIO_ENCODING_ADPCM) ) {
            Uint8 encoded[BUFSIZ];

            adpcm_coder((short *)buffer, encoded, len/2, &adpcm);
            fwrite(encoded, len/4, 1, output);
        } else {
            fwrite(buffer, len, 1, output);
        }
    }
    return(0);
}

int WriteVideoChunk(FILE *input, double timestamp, Uint32 size,
                             const char *encoding, FILE *output)
{
    Uint8 buffer[BUFSIZ];
    int len;

//fprintf(stderr, "V");
    fwrite(VIDEO_DATA_MAGIC, 4, 1, output);
    WRITE32((Uint32)timestamp, output);
    WRITE32(size, output);
    while ( size > 0 ) {
        if ( size < BUFSIZ ) {
            len = fread(buffer, 1, size, input);
        } else {
            len = fread(buffer, 1, BUFSIZ, input);
        }
        if ( len < 0 ) {
            if ( ferror(input) ) {
                fprintf(stderr, "Error reading input data\n");
            }
            return(-1);
        }
        size -= len;
        fwrite(buffer, len, 1, output);
    }
    return(0);
}

void Usage(const char *argv0)
{
    printf("SMJPEG 0.1 encoder, Loki Entertainment Software\n");
    printf("Usage: %s [-r fps] [-c channels]\n", argv0);
}

int main(int argc, char *argv[])
{
    const Uint8 smjpeg_magic[] = { '\0', '\n', 'S','M','J','P','E','G' };
    Uint16 audio_rate;
    Uint8  audio_bits;
    Uint8  audio_channels;
    char audio_encoding[5];
    double video_fps;
    Uint32 video_nframes;
    Uint16 video_width;
    Uint16 video_height;
    char video_encoding[5];
    int index;
    struct stat sb;
    char jpegprefix[PATH_MAX], jpegfile[PATH_MAX];
    char audiofile[PATH_MAX];
    char outputfile[PATH_MAX];
    FILE *jpeginput, *audioinput, *output;
    Uint32 audio_left;
    Uint32 audio_framesize;
    Uint32 video_framesize;
    double ms_per_audio_frame;
    double ms_per_video_frame;
    double audio_time, video_time;
    int status;

    /* First, set default encoding parameters */
    audio_rate = DEFAULT_AUDIO_RATE;
    audio_bits = DEFAULT_AUDIO_BITS;
    audio_channels = DEFAULT_AUDIO_CHANNELS;
    strcpy(audio_encoding, DEFAULT_AUDIO_ENCODING);
    video_fps = DEFAULT_VIDEO_FPS;
    video_nframes = 0;
    video_width = 0;
    video_height = 0;
    strcpy(video_encoding, DEFAULT_VIDEO_ENCODING);
    strcpy(jpegprefix, DEFAULT_JPEG_PREFIX);
    strcpy(audiofile, DEFAULT_AUDIO_INPUT);
    strcpy(outputfile, DEFAULT_OUTPUT_FILE);

    /* Process command-line options */
    for ( index=1; argv[index]; ++index ) {
        if ( (strcmp(argv[index], "-h") == 0) ||
             (strcmp(argv[index], "--help") == 0) ) {
            Usage(argv[0]);
            exit(0);
        }
        if ( (strcmp(argv[index], "-r") == 0) && argv[index+1] ) {
            ++index;
            video_fps = atoi(argv[index]);
        }
        if ( (strcmp(argv[index], "-c") == 0) && argv[index+1] ) {
            ++index;
            audio_channels = atoi(argv[index]);
        }
    }

    /* Count the number of jpeg frames */
    index = 0;
    do {
        sprintf(jpegfile, "%s%d.jpg", jpegprefix, index++);
    } while ( access(jpegfile, R_OK) == 0 );

    /* Double check that we have some frames */
    video_nframes = index - 1;
    if ( video_nframes == 0 ) {
        fprintf(stderr, "Warning: no video stream - audio only\n");
    }

    /* Get the width and height of the output movie */
    if ( video_nframes > 0 ) {
        sprintf(jpegfile, "%s0.jpg", jpegprefix);
        get_jpeg_dimensions(jpegfile, &video_width, &video_height);
    }

    /* Check to see if there is any audio input */
    stat(audiofile, &sb);
    audio_left = sb.st_size;
    audioinput = fopen(audiofile, "r");
    if ( audioinput == NULL ) {
        fprintf(stderr, "Warning: no audio stream - video only\n");
    }

    /* Check to make sure we have at least audio or video */
    if ( ! video_nframes && ! audioinput ) {
        fprintf(stderr, "No audio or video input - aborting!\n");
        exit(1);
    }

    /* Open the output file */
    output = fopen(outputfile, "w");
    if ( output == NULL ) {
        fprintf(stderr, "Unable to write output to %s\n", outputfile);
        exit(2);
    }

    /* Okay, now we're ready to rock! */
    if ( video_nframes ) {
        printf("Encoding %d %dx%d frames of %s encoded video at %2.2f FPS\n",
          video_nframes, video_width, video_height, video_encoding, video_fps);
    }
    if ( audioinput ) {
        printf("- Multiplexing %d-bit %s audio stream at %d Hz\n",
          audio_bits,(audio_channels == 1)?"mono":"stereo", audio_rate);
    }

    /* Write the main header */
    fwrite(smjpeg_magic, sizeof(smjpeg_magic), 1, output);
    WRITE32(SMJPEG_FORMAT_VERSION, output);
    WRITE32((Uint32)(((double)video_nframes/video_fps)*1000.0), output);

    /* Write the audio header */
    if ( audioinput ) {
        fwrite(AUDIO_HEADER_MAGIC, 4, 1, output);
        WRITE32(8, output);
        WRITE16(audio_rate, output);
        WRITE8(audio_bits, output);
        WRITE8(audio_channels, output);
        fwrite(audio_encoding, 4, 1, output);
    }

    /* Write the video header */
    if ( video_nframes ) {
        fwrite(VIDEO_HEADER_MAGIC, 4, 1, output);
        WRITE32(12, output);
        WRITE32(video_nframes, output);
        WRITE16(video_width, output);
        WRITE16(video_height, output);
        fwrite(video_encoding, 4, 1, output);
    }

    /* Write the end of header marker */
    fwrite(HEADER_END_MAGIC, 4, 1, output);

    /* Multiplex the audio and video data */
    audio_framesize = DEFAULT_AUDIO_FRAME * (audio_bits / 8);
    audio_time = 0.0;
    video_time = 0.0;
    ms_per_audio_frame = (1000.0 * DEFAULT_AUDIO_FRAME) / audio_rate;
    ms_per_video_frame = 1000.0 / video_fps;
    for ( index=0; index<video_nframes; ++index ) {

        /* Encode audio for this frame and one frame ahead */
        while ( audioinput && (audio_left > 0) &&
                (audio_time < (video_time+2*ms_per_video_frame)) ) {
            if ( audio_framesize > audio_left ) {
                audio_framesize = audio_left;
            }
            WriteAudioChunk(audioinput, audio_time, audio_framesize,
                                            audio_encoding, output);
            audio_left -= audio_framesize;
            audio_time += ms_per_audio_frame;

            printf("A"); fflush(stdout);
        }

        /* Encode the video for this frame */
        sprintf(jpegfile, "%s%d.jpg", jpegprefix, index);
        stat(jpegfile, &sb);
        video_framesize = sb.st_size;
        jpeginput = fopen(jpegfile, "rb");
        if ( jpeginput ) {
            WriteVideoChunk(jpeginput, video_time, video_framesize,
                                            video_encoding, output);
            video_time += ms_per_video_frame;
        } else {
            fprintf(stderr, "Couldn't open %s: %s\n", jpegfile,
                            strerror(errno));
            abort();
        }
        fclose(jpeginput);

        printf("V"); fflush(stdout);
    }
    /* Finish writing any audio data that's left */
    while ( audioinput && (audio_left > 0) ) {
        if ( audio_framesize > audio_left ) {
            audio_framesize = audio_left;
        }
        WriteAudioChunk(audioinput, audio_time, audio_framesize,
                                        audio_encoding, output);
        audio_left -= audio_framesize;
        audio_time += ms_per_audio_frame;

        printf("A"); fflush(stdout);
    }

    /* Write the end of data marker */
    fwrite(DATA_END_MAGIC, 4, 1, output);

    /* We're done! */
    printf("\n");
    if ( ferror(output) || (fclose(output) == EOF) ) {
        fprintf(stderr, "Error while writing to output!\n");
        status=6;
    } else {
        printf("Encoding successfully completed.\n");
        status=0;
    }
    exit(status);
}
