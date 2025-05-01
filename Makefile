# Compiler to use
CC = gcc

# Compiler flags:
# -Wall: Enable common warnings
# -Wextra: Enable extra warnings (recommended)
# -pedantic: Issue all warnings demanded by strict ISO C
# -std=c11: Use the C11 standard (needed for static_assert)
# -g: Include debugging information
# -O2: Optimization level (optional, can be removed or changed, e.g., to -O0 for easier debugging)
CFLAGS = -Wall -Wextra -pedantic -std=c11 -g -O2

# Linker flags (if needed, e.g., -lm for math library if you use functions like ceil, sqrt)
# qsort (from search.h) doesn't typically require a separate link flag with modern gcc.
LDFLAGS =

# Target executable name
TARGET = exfs2

# Source files
SRCS = exfs2.c

# Object files (derived automatically from source files)
OBJS = $(SRCS:.c=.o)

# Header files (used as dependencies)
HDRS = exfs2.h

# Default target: build the executable
# This is the rule executed when you just type 'make'
all: $(TARGET)

# Rule to link the object file(s) into the final executable
$(TARGET): $(OBJS)
	@echo "Linking $(TARGET)..."
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET) $(LDFLAGS)
	@echo "$(TARGET) built successfully."

# Rule to compile a .c source file into a .o object file
# It depends on the corresponding .c file and any header files specified in HDRS
%.o: %.c $(HDRS)
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# Rule to clean up build artifacts
clean:
	@echo "Cleaning up..."
	rm -f $(TARGET) $(OBJS)
	@echo "Cleanup complete."

# Declare 'all' and 'clean' as phony targets, meaning they aren't actual files
.PHONY: all clean

