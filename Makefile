
# Libraries used by the SMJPEG encoder/decoder
JPEGDIR = jpeg-6b
ADPCMDIR = adpcm

OPTIMIZE = -O2 -funroll-loops
CFLAGS = -g $(OPTIMIZE) -I$(JPEGDIR) -I$(ADPCMDIR)

LIBRARIES = $(JPEGDIR)/libjpeg.a $(ADPCMDIR)/libadpcm.a
ifeq ($(static), true)
SDLLIB = -lSDL -ldl -lpthread -lesd -L/usr/X11R6/lib -lX11 -lXext -lXxf86dga -lXxf86vm
else
SDLLIB = -lSDL -ldl -lpthread -lesd
endif

all: smjpeg_encode smjpeg_decode libsmjpeg.a

smjpeg_encode: smjpeg_encode.o $(LIBRARIES)
	$(CC) -o $@ $^ -L$(JPEGDIR) -ljpeg -L$(ADPCMDIR) -ladpcm

smjpeg_decode: smjpeg_decode.o play_smjpeg.o $(LIBRARIES)
	$(CC) -o $@ $^ -L$(JPEGDIR) -ljpeg -L$(ADPCMDIR) -ladpcm $(SDLLIB)

# Rule to build the decoding library
libsmjpeg.a: smjpeg_decode.o $(LIBRARIES)
	$(AR) crv $@ smjpeg_decode.o $(JPEGDIR)/*.o $(ADPCMDIR)/*.o

$(JPEGDIR)/libjpeg.a:
	$(MAKE) -C $(JPEGDIR) libjpeg.a

$(ADPCMDIR)/libadpcm.a:
	$(MAKE) -C $(ADPCMDIR) libadpcm.a

clean:
	rm -f smjpeg_encode smjpeg_decode libsmjpeg.a
	rm -f *.o
	$(MAKE) -C $(JPEGDIR) clean
	$(MAKE) -C $(ADPCMDIR) clean
