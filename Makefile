# ps5link: the linker (a host program) and the startup object every title
# links first.
#
#   make                 linker/link_real and linker/crt1_ps5.o
#   make test            the linker's host-side unit tests
#   make examples        examples/*/eboot.bin (needs SHARPPROSPERO and .NET to sign)
#
# PS5_PAYLOAD_SDK names the ps5-payload-sdk install that provides prospero-clang.

HOST_CC ?= cc
HOST_CFLAGS ?= -O2 -Wall

PS5_PAYLOAD_SDK ?= /opt/ps5-payload-sdk
PS5_CC := $(PS5_PAYLOAD_SDK)/bin/prospero-clang

LINKER_SRCS := linker/link_real.c linker/elf_object.c linker/linker.c linker/dynwriter.c \
               linker/catalog.c linker/catalog_extra.c linker/catalog_lookup.c linker/nid.c linker/sha1.c
LINKER_HDRS := $(wildcard linker/*.h)
LIB_SRCS := $(filter-out linker/link_real.c,$(LINKER_SRCS))

TESTS := linker/test_nid linker/test_catalog linker/test_elf_object linker/test_linker
EXAMPLES := $(wildcard examples/*/main.c)

all: linker/link_real linker/crt1_ps5.o

linker/link_real: $(LINKER_SRCS) $(LINKER_HDRS)
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $(LINKER_SRCS)

linker/crt1_ps5.o: linker/crt1.S
	@test -x $(PS5_CC) || { echo "prospero-clang not found: set PS5_PAYLOAD_SDK"; exit 1; }
	$(PS5_CC) -c $< -o $@

linker/test_%: linker/test_%.c $(LIB_SRCS) $(LINKER_HDRS)
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $< $(LIB_SRCS)

# The object reader and the resolver are run over the startup object and an
# example compiled for the console.
test: $(TESTS) linker/crt1_ps5.o
	$(PS5_CC) -c -O2 examples/hello_notify/main.c -o linker/test_input.o
	./linker/test_nid
	./linker/test_catalog
	./linker/test_elf_object linker/test_input.o
	./linker/test_linker linker/crt1_ps5.o linker/test_input.o

# Each example: compile, link, sign.
examples: all $(EXAMPLES:main.c=eboot.bin)

examples/%/eboot.bin: examples/%/main.c linker/link_real linker/crt1_ps5.o
	@test -n "$(SHARPPROSPERO)" || { echo "set SHARPPROSPERO to a SharpProspero checkout"; exit 1; }
	$(PS5_CC) -c -O2 -Wall $< -o examples/$*/main.o
	./linker/link_real examples/$*/app.elf linker/crt1_ps5.o examples/$*/main.o
	cd $(SHARPPROSPERO)/tools/SharpProspero.Bindings.Generator && \
		dotnet run -c Release -- self --sign --in $(CURDIR)/examples/$*/app.elf --out $(CURDIR)/$@

clean:
	rm -f linker/link_real linker/crt1_ps5.o $(TESTS) examples/*/main.o examples/*/app.elf examples/*/eboot.bin

.PHONY: all test examples clean
