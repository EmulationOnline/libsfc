# force core rebuild, while deps isn't working
.PHONY: libsfc.so clean run runc repl ci deps

ifeq ($(CC),cc)
CC = clang
endif

default: libsfc.so


EMBEDFLAGS=-O3 -fvisibility=hidden -static -fPIC -lm
COREFLAGS := --std=c2x -DHAVE_LOCALTIME_R -DPATH_MAX=4096 -Wfatal-errors -Werror -Wno-narrowing  -I msfc/include/ -I msfc/src/ 
libsfc.so: libsfc.c corelib.h
	echo "cc: $(CC)"
	$(CC) $(CFLAGS) $(EMBEDFLAGS) $(COREFLAGS)  $(LDFLAGS) -shared -o libsfc.so libsfc.c $(SRCS)
	cp libsfc.so libapu.so
	echo "libsfc done"

all: libsfc.so main

ci: deps libsfc.so

deps:
	apt install -y clang
	clang -v

main: main.c corelib.h
	$(CC) -O3 -o main main.c -L. -lsfc $(shell pkg-config --cflags --libs sdl2) -lc -lm ${WARN}
	echo "main done"

clean:
	rm -f libsfc.so main

gdb:
	LD_LIBRARY_PATH=$(shell pwd) gdb --args ./main "$(ROM)"
run:
	LD_LIBRARY_PATH=$(shell pwd) ./main "$(ROM)"
runc:
	LD_LIBRARY_PATH=$(shell pwd) ./main "$(ROM)" c
repl:
	ls Makefile libsfc.c | entr -c make
wrepl:
	ls Makefile libsfc.c | entr -c make libsfc.js

# bsnes-based libsfc.so build
BSNES_DIR=bsnes/bsnes
BSNES_OBJS=$(wildcard $(BSNES_DIR)/obj/*.o)

# Build bsnes object files first (reuses target-libretro build)
bsnes-objs:
	$(MAKE) -C $(BSNES_DIR) target=libretro

# Build libsfc.so using bsnes core
libsfc.so: bsnes-objs libsfc.c corelib.h
	g++ -shared -fPIC -o libsfc.so libsfc.c \
		$(filter-out $(BSNES_DIR)/obj/libretro.o, $(wildcard $(BSNES_DIR)/obj/*.o)) \
		-I$(BSNES_DIR) -I$(BSNES_DIR)/.. \
		-O3 -DBUILD_PERFORMANCE \
		-fopenmp -lpthread -ldl -lX11 -lXext \
		-Wl,--no-undefined
	@echo "libsfc.so (bsnes) done"

EMCC=~/external/emscripten/em++
EXPORTS="['_framebuffer_bytes', '_alloc_rom', '_set_key']"
WASMFLAGS=-Wl,--no-entry -Wl,--export-all -s EXPORTED_FUNCTIONS=$(EXPORTS) -s EXPORTED_RUNTIME_METHODS=['HEAPU8'] -DISWASM -D_POSIX_SOURCE -sALLOW_MEMORY_GROWTH #-sINITIAL_MEMORY=200mb
.PHONY: libsfc.js
libsfc.js:
# $(EMCC) -Wno-div-by-zero $(GBFLAGS) libsfc.c $(SRCS) $(WASMFLAGS)
	 $(EMCC) $(CFLAGS) $(COREFLAGS) $(WASMFLAGS) -o libsfc.js libsfc.c $(SRCS)
	# $(CC) $(CFLAGS)  -c neslib.c -o libnes_wasm.o ${WARN}
	# $(CC) libnes_wasm.o --no-entry $(LIBFLAGS) -o $@
