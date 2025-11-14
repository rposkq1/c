# Compiler
CC = gcc

# Compiler flags
CFLAGS = -Wall -Wextra -g -pedantic -Wno-deprecated-declarations -Os
LDFLAGS = -lncurses

# Source files
SRC = fb.c

# Object files
OBJ = $(SRC:.c=.o)

# Executable name
TARGET = framebuffer_display

# Default target
all: $(TARGET)

# Linking the object files to create the executable
$(TARGET): $(OBJ)
	$(CC) $(OBJ) $(LDFLAGS) -o $(TARGET)

# Compiling the source files into object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean target to remove object files and executable
clean:
	rm -f $(OBJ) $(TARGET)

# Phony targets
.PHONY: all clean

