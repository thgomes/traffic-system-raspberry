CFLAGS = -Wunused-variable -I./libs -Wno-implicit-function-declaration
LDFLAGS = -lm

SRC_DIR = src/
LIBS_DIR = libs/

# Source files
SRC_FILES = $(wildcard $(SRC_DIR)/*.c)
LIBS_FILES = $(wildcard $(LIBS_DIR)/*.c)

# Object files
OBJ_SRC_FILES = $(SRC_FILES:.c=.o)
OBJ_LIBS_FILES = $(LIBS_FILES:.c=.o)

EXECUTABLE = central_server

# Default target
all: $(EXECUTABLE)

$(EXECUTABLE): $(OBJ_SRC_FILES) $(OBJ_LIBS_FILES)
	gcc $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Compile source files into object files
%.o: %.c
	gcc $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ_SRC_FILES) $(OBJ_LIBS_FILES)

all:clean