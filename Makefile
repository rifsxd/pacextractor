# Define compiler and flags
CC = gcc

# Target executable
TARGET = pacextractor

# Source files
SRC = pacextractor.c

# Rule to build the target
$(TARGET): $(SRC)
	$(CC) $(SRC) -o $(TARGET)

# Clean up build artifacts
clean:
	rm -f $(TARGET)
