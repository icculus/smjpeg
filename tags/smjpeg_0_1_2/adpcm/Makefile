
TARGET = libadpcm.a
OBJS   = adpcm.o

#CFLAGS = -O2 -funroll-loops -fomit-frame-pointer
CFLAGS = -O2 -funroll-loops
#CFLAGS = -g

all: $(TARGET)

$(TARGET): $(OBJS)
	$(AR) crv $@ $^

clean:
	rm -f $(TARGET) *.o
