
#ifndef _SMJPEG_FILE_H_
#define _SMJPEG_FILE_H_

/* Possible audio encoding parameters */
#define AUDIO_ENCODING_NONE     "NONE"
#define AUDIO_ENCODING_ADPCM    "APCM"

/* Possible video encoding parameters */
#define VIDEO_ENCODING_JPEG     "JFIF"

/* Magic constants for the file format */
#define SMJPEG_FORMAT_VERSION   0
#define AUDIO_HEADER_MAGIC      "_SND"
#define VIDEO_HEADER_MAGIC      "_VID"
#define HEADER_END_MAGIC        "HEND"
#define AUDIO_DATA_MAGIC        "sndD"
#define VIDEO_DATA_MAGIC        "vidD"
#define DATA_END_MAGIC          "DONE"
#define MAGIC_EQUALS(X, Y)       (memcmp(X, Y, 4) == 0)

/* Macros to assist in reading/writing the SMJPEG file format */

#define READ8(val, fp) \
    val = (Uint8)fgetc(fp);
#define READ16(val, fp) \
    val = (Uint8)fgetc(fp); \
    val <<= 8; \
    val |= (Uint8)fgetc(fp);
#define READ32(val, fp) \
    val = (Uint8)fgetc(fp); \
    val <<= 8; \
    val |= (Uint8)fgetc(fp); \
    val <<= 8; \
    val |= (Uint8)fgetc(fp); \
    val <<= 8; \
    val |= (Uint8)fgetc(fp);

#define WRITE8(val, fp) \
    fputc((Uint8)(val), fp);
#define WRITE16(val, fp) \
    fputc((((Uint16)(val))>>8)&0xFF, fp); \
    fputc(((Uint16)(val))&0xFF, fp);
#define WRITE32(val, fp) \
    fputc((((Uint32)(val))>>24)&0xFF, fp); \
    fputc((((Uint32)(val))>>16)&0xFF, fp); \
    fputc((((Uint32)(val))>>8)&0xFF, fp); \
    fputc(((Uint32)(val))&0xFF, fp);

#endif /* _SMJPEG_FILE_H_ */
