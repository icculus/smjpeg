/*
** adpcm.h - include file for adpcm coder.
**
** Version 1.1, 06 Oct 2003.
*/

struct adpcm_state {
    short	valprev;	/* Previous output value */
    char	index;		/* Index into stepsize table */
};

#ifdef __STDC__
#define ARGS(x) x
#else
#define ARGS(x) ()
#endif

void SMJPEG_adpcm_coder ARGS((short indata[], char outdata[], int len , char channels, struct adpcm_state state[]));
void SMJPEG_adpcm_decoder ARGS((char indata[], short outdata[], int len, char channels, struct adpcm_state state[]));
