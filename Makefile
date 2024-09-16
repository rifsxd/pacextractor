# Variables
BINARY_NAME = pacextractor
SRC_FILES = pacextractor.go

# Default target
all: build

# Build the binary
build:
	go build -o $(BINARY_NAME) $(SRC_FILES)

# Format the code
fmt:
	go fmt $(SRC_FILES)

# Run go vet for static analysis
vet:
	go vet $(SRC_FILES)

# Run the program with default arguments (modify as needed)
run: build
	./$(BINARY_NAME) firmware.pac output_directory

# Clean the build
clean:
	rm -f $(BINARY_NAME)

# Help message
help:
	@echo "Makefile for building and managing the Go project"
	@echo ""
	@echo "Usage:"
	@echo "  make           - Build the project"
	@echo "  make build     - Build the binary"
	@echo "  make fmt       - Format the source files"
	@echo "  make vet       - Run go vet for static analysis"
	@echo "  make run       - Run the program with default arguments"
	@echo "  make clean     - Remove the binary"
	@echo "  make help      - Show this help message"

.PHONY: all build fmt vet run clean help
