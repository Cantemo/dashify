PKG_CONFIG_PATH:=/usr/local/ffmpeg/lib/pkgconfig/

# use pkg-config for getting CFLAGS and LDLIBS
FFMPEG_LIBS=                                       \
                libavformat                        \
                libavcodec                         \
                libavutil                          \

CFLAGS += -Wall -g -O0
CFLAGS := $(shell pkg-config --cflags $(FFMPEG_LIBS)) $(CFLAGS)
LDLIBS := $(shell pkg-config --libs $(FFMPEG_LIBS)) $(LDLIBS)

LDFLAGS := -g -O0
EXAMPLES=dashify

OBJS=$(addsuffix .o,$(EXAMPLES))

# the following examples make explicit use of the math library
avcodec:           LDLIBS += -lm
decoding_encoding: LDLIBS += -lm
muxing:            LDLIBS += -lm
resampling_audio:  LDLIBS += -lm

.phony: all clean-test clean

all: $(OBJS) $(EXAMPLES)

clean-test:
	$(RM) test*.pgm test.h264 test.mp2 test.sw test.mpg

clean: clean-test
	$(RM) $(EXAMPLES) $(OBJS)
