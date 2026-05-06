# 1. Variables - Makes the script easy to update
CC = gcc
CFLAGS = -Wall -Wextra -g -Iinclude
TARGET = bins/program

# 2. Path definitions
SRCDIR = src
OBJDIR = obj
INCDIR = include

# 3. List your source files and object files
# This automatically finds all .c files in src/ and maps them to obj/
SRCS = $(wildcard $(SRCDIR)/*.c)
OBJS = $(patsubst $(SRCDIR)/%.c, $(OBJDIR)/%.o, $(SRCS))

# 4. The default rule (what happens when you just type 'make')
all: $(OBJDIR) $(TARGET)

# 5. Linking the final executable
$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET)

# 6. Compiling individual source files into object files
$(OBJDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# 7. Create the object directory if it doesn't exist
$(OBJDIR):
	mkdir -p $(OBJDIR)

# 8. Clean rule for a fresh build
clean:
	rm -rf $(OBJDIR) $(TARGET)

.PHONY: all clean
