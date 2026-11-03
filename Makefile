# ShadowAsset - compilación en Linux/WSL/macOS y en Windows con make (MSYS2).
#   make          compila build/shadowasset (y build/shadowasset-gui.exe en Windows)
#   make test     compila y ejecuta las pruebas unitarias
#   make asan     pruebas con AddressSanitizer + UndefinedBehaviorSanitizer
#   make clean

CC      ?= gcc
WINDRES ?= windres
CFLAGS  ?= -std=c17 -Wall -Wextra -Wpedantic -Werror -Wshadow -Wconversion -Wformat=2 -O2 -g
SAN     := -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer

# Módulos compartidos: todo menos los dos puntos de entrada.
LIB_SRC := $(filter-out src/main.c src/gui.c,$(wildcard src/*.c))
HEADERS := $(wildcard src/*.h)

BIN    := build/shadowasset
TESTS  := build/pruebas
TARGETS := $(BIN)

ifeq ($(OS),Windows_NT)
  EXE     := .exe
  LDLIBS  := -liphlpapi
  BIN     := $(BIN)$(EXE)
  TESTS   := $(TESTS)$(EXE)
  GUI     := build/shadowasset-gui.exe
  TARGETS := $(BIN) $(GUI)
endif

.PHONY: all test asan clean

all: $(TARGETS)

build:
	mkdir -p build

$(BIN): src/main.c $(LIB_SRC) $(HEADERS) | build
	$(CC) $(CFLAGS) src/main.c $(LIB_SRC) $(LDLIBS) -o $@

build/gui_res.o: res/gui.rc res/shadowasset.manifest | build
	$(WINDRES) res/gui.rc -O coff -o $@

$(GUI): src/gui.c build/gui_res.o $(LIB_SRC) $(HEADERS) | build
	$(CC) $(CFLAGS) -municode -mwindows src/gui.c $(LIB_SRC) build/gui_res.o \
		-liphlpapi -lcomctl32 -lcomdlg32 -lshell32 -lole32 -o $@

$(TESTS): tests/pruebas.c $(LIB_SRC) $(HEADERS) | build
	$(CC) $(CFLAGS) tests/pruebas.c $(LIB_SRC) $(LDLIBS) -o $@

test: $(TESTS)
	./$(TESTS)

asan: | build
	$(CC) $(CFLAGS) $(SAN) tests/pruebas.c $(LIB_SRC) $(LDLIBS) -o build/pruebas_asan$(EXE)
	./build/pruebas_asan$(EXE)

clean:
	rm -rf build
