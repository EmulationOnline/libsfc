# force core rebuild, while deps isn't working
.PHONY: libsfc.so clean run runc repl ci deps

ifeq ($(CC),cc)
CC = clang
endif


EMBEDFLAGS=-O3 -fvisibility=hidden -static -fPIC -lm
# CFLAGS=-fvisibility=hidden -ffreestanding -nostdlib -fPIC -O3 -Wfatal-errors -Werror
GBA := msfc/src/sfc/
UTIL := msfc/src/util/
SRCS := $(wildcard $(GBA)/*.c) $(wildcard $(GBA)/cart/*.c) $(wildcard $(GBA)/cheats/*.c) $(wildcard msfc/src/arm/*.c) $(GBA)/sio/gbp.c $(wildcard $(GBA)/renderers/*.c) $(GBA)/../gb/audio.c $(wildcard $(UTIL)/*.c) $(UTIL)/vfs/vfs-mem.c $(filter-out msfc/src/core/scripting.c, $(wildcard msfc/src/core/*.c)) $(GBA)/extra/proxy.c -x c msfc/src/core/version.c.in $(wildcard msfc/src/feature/video-*.c) msfc/src/third-party/inih/ini.c
GBAFLAGS := --std=c2x -DHAVE_LOCALTIME_R -DPATH_MAX=4096 -Wfatal-errors -Werror -Wno-narrowing  -I msfc/include/ -I msfc/src/ 
libsfc.so: libsfc.c corelib.h
	echo "cc: $(CC)"
	$(CC) $(CFLAGS) $(EMBEDFLAGS) $(GBAFLAGS)  $(LDFLAGS) -shared -o libsfc.so libsfc.c $(SRCS)
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

EMCC=~/external/emscripten/em++
EXPORTS="['_framebuffer_bytes', '_alloc_rom', '_set_key']"
WASMFLAGS=-Wl,--no-entry -Wl,--export-all -s EXPORTED_FUNCTIONS=$(EXPORTS) -s EXPORTED_RUNTIME_METHODS=['HEAPU8'] -DISWASM -D_POSIX_SOURCE -sALLOW_MEMORY_GROWTH #-sINITIAL_MEMORY=200mb
.PHONY: libsfc.js
libsfc.js:
# $(EMCC) -Wno-div-by-zero $(GBFLAGS) libsfc.c $(SRCS) $(WASMFLAGS)
	 $(EMCC) $(CFLAGS) $(GBAFLAGS) $(WASMFLAGS) -o libsfc.js libsfc.c $(SRCS)
	# $(CC) $(CFLAGS)  -c neslib.c -o libnes_wasm.o ${WARN}
	# $(CC) libnes_wasm.o --no-entry $(LIBFLAGS) -o $@
