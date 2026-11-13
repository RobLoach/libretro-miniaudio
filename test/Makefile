# Builds and runs the libretro-miniaudio tests: make -C test [SANITIZE=1]
TARGET   := libretro_miniaudio_test
CFLAGS   ?= -O1 -Wall
CFLAGS   += -std=gnu99
CPPFLAGS += -I.. -I../example/libretro-common/include
LDLIBS   += -lm

ifeq ($(SANITIZE),1)
CFLAGS  += -fsanitize=address,undefined -fno-sanitize-recover=all -g
LDFLAGS += -fsanitize=address,undefined
endif

all: test

test: $(TARGET)
	./$(TARGET) ../example/amen.wav

$(TARGET): test.c ../libretro-miniaudio.h ../miniaudio.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ test.c $(LDFLAGS) $(LDLIBS)

clean:
	rm -f $(TARGET)

.PHONY: all test clean
