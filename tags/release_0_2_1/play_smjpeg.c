
/* This file plays SMJPEG format Motion JPEG movies */

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>

/* Define this if you actually want to play the file */
#define PLAY_SMJPEG

#include "smjpeg_decode.h"

void Usage(const char *argv0)
{
    printf("SMJPEG " VERSION " decoder, Loki Entertainment Software\n");
    printf("Usage: %s [-2] [-l] file.mjpg [file.mjpg ...]\n", argv0);
}

int main(int argc, char *argv[])
{
    SMJPEG movie;
    int i;
    int doubleflag;
    int loopflag;

    if ( SDL_Init(SDL_INIT_AUDIO|SDL_INIT_VIDEO) < 0 ) {
        fprintf(stderr, "Couldn't init SDL: %s\n", SDL_GetError());
        exit(1);
    }
    atexit(SDL_Quit);

    doubleflag = 0;
    loopflag = 0;
    for ( i=1; argv[i]; ++i ) {
        if ( (strcmp(argv[i], "-h") == 0) ||
             (strcmp(argv[i], "--help") == 0) ) {
            Usage(argv[0]);
            exit(0);
        }
        if ( strcmp(argv[i], "-2") == 0 ) {
            doubleflag = !doubleflag;
            continue;
        }
        if ( strcmp(argv[i], "-l") == 0 ) {
            loopflag = !loopflag;
            continue;
        }

        /* Load and play the animation */
        if ( SMJPEG_load(&movie, argv[i]) < 0 ) {
            fprintf(stderr, "%s\n", movie.status.message);
            continue;
        }
        /* Print out information about the file */
        if ( movie.audio.enabled ) {
            printf("Audio stream: %d bit %s audio at %d Hz\n",
                movie.audio.bits,
                (movie.audio.channels == 1) ? "mono" : "stereo",
                movie.audio.rate);
        }
        if ( movie.video.enabled ) {
            printf("Video stream: %d frames of %dx%d animation\n",
                movie.video.frames, movie.video.width, movie.video.height);
        }
#ifdef PLAY_SMJPEG
        if ( movie.video.enabled ) {
            SDL_Surface *screen;
            int width, height;

            width = movie.video.width;
            height = movie.video.height;
            if ( doubleflag ) {
                width *= 2;
                height *= 2;
            }
            screen = SDL_SetVideoMode(width, height, 16, SDL_SWSURFACE);
            if ( screen == NULL ) {
                fprintf(stderr, "Couldn't set %dx%d 16-bit video mode: %s\n",
                                                            SDL_GetError());
                continue;
            }
            SMJPEG_double(&movie, doubleflag);
            SMJPEG_target(&movie, NULL, 0, 0, screen, SDL_UpdateRect);
        }
        if ( movie.audio.enabled ) {
            SDL_AudioSpec spec;

            spec.freq = movie.audio.rate;
            switch (movie.audio.bits) {
                case 8:
                    spec.format = AUDIO_U8;
                    break;
                case 16:
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
                    spec.format = AUDIO_S16LSB;
#else
                    spec.format = AUDIO_S16MSB;
#endif
                    break;
                default:
                    /* Uh oh... */
                    fprintf(stderr, "Unknown audio format in SMJPEG\n");
                    spec.format = 0;
                    break;
            }
            spec.channels = movie.audio.channels;
            spec.samples = 512;
            spec.callback = SMJPEG_feedaudio;
            spec.userdata = &movie;

            if ( SDL_OpenAudio(&spec, NULL) < 0 ) {
                movie.audio.enabled = 0;
            } else {
                SDL_PauseAudio(0);
            }
        }
        do {
            SMJPEG_start(&movie, 1);
            while ( ! movie.at_end ) {
                SDL_Event event;

                SMJPEG_advance(&movie, 1, 1);

                if ( SDL_PollEvent(&event) ) {
                    switch(event.type) {
                        case SDL_KEYDOWN:
                        case SDL_QUIT:
                            loopflag = 0;
                            SMJPEG_stop(&movie);
                            break;
                    }
                }
            }
            SMJPEG_stop(&movie);

            if ( loopflag ) {
                SMJPEG_rewind(&movie);
            }
        } while ( loopflag );

        if ( movie.audio.enabled ) {
            SDL_CloseAudio();
        }
        SMJPEG_free(&movie);
#endif /* PLAY_SMJPEG */
    }
}
