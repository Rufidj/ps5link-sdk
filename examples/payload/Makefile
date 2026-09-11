# The payload examples: built against ps5-payload-sdk's SDL2, run with its ELF
# loader rather than installed as titles. Their entry point is SDL_main().
#
#   make EXAMPLE=hello_pad
#   make EXAMPLE=fb_explorer PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
#
# The result, build/<example>.elf, is sent to the console's ELF loader
# (port 9021 with etaHEN).

PS5_PAYLOAD_SDK ?= /opt/ps5-payload-sdk
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

EXAMPLE ?= hello_notify
OUTDIR   = build
SDK      = ../../sdk

PS5_HB_LIB := $(PS5_PAYLOAD_SDK)/target/user/homebrew/lib
PS5_HB_INC := $(PS5_PAYLOAD_SDK)/target/user/homebrew/include

SDL2CFLAGS := -I$(PS5_HB_INC) -I$(PS5_HB_INC)/SDL2 -D_REENTRANT
SDL2LIBS   := $(PS5_HB_LIB)/libSDL2main.a $(PS5_HB_LIB)/libSDL2.a \
              $(PS5_HB_LIB)/libSDL2_ttf.a $(PS5_HB_LIB)/libfreetype.a \
              -lbz2 -lpng -liconv -lm -lz

CFLAGS = -Wall -O1 -I$(SDK) $(SDL2CFLAGS)
LIBS   = $(SDL2LIBS) \
         -lkernel_sys -lSceSystemService -lSceUserService \
         -lScePad -lSceVideoOut -lSceAudioOut \
         -lSceKeyboard -lSceImeDialog

all: $(OUTDIR)/$(EXAMPLE).elf

$(OUTDIR):
	mkdir -p $(OUTDIR)

$(OUTDIR)/$(EXAMPLE).elf: $(EXAMPLE).c | $(OUTDIR)
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

clean:
	rm -rf $(OUTDIR)

.PHONY: all clean
