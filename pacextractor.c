#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/stat.h>

#define VERSION "1.1.0"

typedef struct {
    int16_t someField[24];
    int32_t someInt;
    int16_t productName[256];
    int16_t firmwareName[256];
    int32_t partitionCount;
    int32_t partitionsListStart;
    int32_t someIntFields1[5];
    int16_t productName2[50];
    int16_t someIntFields2[6];
    int16_t someIntFields3[2];
} PacHeader;

typedef struct {
    uint32_t length;
    int16_t partitionName[256];
    int16_t fileName[512];
    uint32_t partitionSize;
    int32_t someFields1[2];
    uint32_t partitionAddrInPac;
    int32_t someFields2[3];
    int32_t dataArray[];
} PartitionHeader;

static void getString(const int16_t* baseString, char* resString) {
    if (baseString == NULL || resString == NULL) {
        *resString = '\0';
        return;
    }

    while (*baseString) {
        *resString++ = (char)(*baseString & 0xFF);
        baseString++;
        if (resString - resString > 255) { // Prevent buffer overflow
            break;
        }
    }
    *resString = '\0'; // Null-terminate the result string
}

static void printUsage(void) {
    printf("Usage: pacextractor -e <firmware name>.pac -o <output path>\n");
    printf("Options:\n");
    printf("  -h               Show this help message and exit\n");
    printf("  -v               Show version information and exit\n");
}

static void printUsageAndExit(void) {
    printUsage();
    exit(EXIT_FAILURE);
}

static void handleOpenFileError(const char* fileName) {
    perror(fileName);
    exit(EXIT_FAILURE);
}

static int openFirmwareFile(const char* filePath) {
    int fd = open(filePath, O_RDONLY);
    if (fd == -1) {
        handleOpenFileError(filePath);
    }
    return fd;
}

static void createOutputDirectory(const char* path) {
    char temp[768];
    strcpy(temp, path);
    for (char *p = temp; *p; p++) {
        if (*p == '/') {
            *p = 0;  // Temporarily terminate the string
            if (access(temp, F_OK) == -1) {
                if (mkdir(temp, 0777) == -1) {
                    perror("Failed to create output directory");
                    exit(EXIT_FAILURE);
                }
            }
            *p = '/';  // Restore the string
        }
    }
    if (access(temp, F_OK) == -1) {
        if (mkdir(temp, 0777) == -1) {
            perror("Failed to create output directory");
            exit(EXIT_FAILURE);
        }
    }
}

static PacHeader readPacHeader(int fd) {
    PacHeader header;
    if (read(fd, &header, sizeof(PacHeader)) != sizeof(PacHeader)) {
        perror("Error while reading PAC header");
        exit(EXIT_FAILURE);
    }
    return header;
}

static PartitionHeader* readPartitionHeader(int fd, uint32_t* curPos) {
    lseek(fd, *curPos, SEEK_SET);
    uint32_t length;
    if (read(fd, &length, sizeof(length)) != sizeof(length)) {
        perror("Error while reading partition header length");
        exit(EXIT_FAILURE);
    }
    
    PartitionHeader* header = malloc(length);
    if (header == NULL) {
        perror("Memory allocation failed");
        exit(EXIT_FAILURE);
    }
    
    lseek(fd, *curPos, SEEK_SET);
    if (read(fd, header, length) != length) {
        perror("Error while reading partition header");
        free(header);
        exit(EXIT_FAILURE);
    }
    
    *curPos += length;
    return header;
}

static void printProgressBar(uint32_t completed, uint32_t total) {
    const int barWidth = 50;
    float progress = (float)completed / total;
    int pos = barWidth * progress;

    printf("\r[");
    for (int i = 0; i < barWidth; ++i) {
        if (i < pos) printf("=");
        else if (i == pos) printf(">");
        else printf(" ");
    }
    printf("] %.2f%%", progress * 100.0);
    fflush(stdout);
}

static void extractPartition(int fd, const PartitionHeader* partHeader, const char* outputPath) {
    if (partHeader->partitionSize == 0) {
        return;
    }

    lseek(fd, partHeader->partitionAddrInPac, SEEK_SET);

    // Increase buffer size for faster I/O operations
    const size_t BUFFER_SIZE = 256 * 1024; // 256 KB
    char* buffer = malloc(BUFFER_SIZE);
    if (buffer == NULL) {
        perror("Memory allocation failed");
        exit(EXIT_FAILURE);
    }

    char outputFilePath[768];
    char fileName[512];
    getString(partHeader->fileName, fileName);
    snprintf(outputFilePath, sizeof(outputFilePath), "%s/%s", outputPath, fileName);

    if (remove(outputFilePath) == -1 && errno != ENOENT) {
        perror("Error removing existing output file");
        free(buffer);
        exit(EXIT_FAILURE);
    }

    int fd_new = open(outputFilePath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd_new == -1) {
        perror("Error creating output file");
        free(buffer);
        exit(EXIT_FAILURE);
    }

    printf("Extracting to %s\n", outputFilePath);

    uint32_t dataSizeLeft = partHeader->partitionSize;
    uint32_t dataSizeRead = 0;

    while (dataSizeLeft > 0) {
        uint32_t copyLength = (dataSizeLeft > BUFFER_SIZE) ? BUFFER_SIZE : dataSizeLeft;
        ssize_t rb = read(fd, buffer, copyLength);
        if (rb != copyLength) {
            perror("Error while reading partition data");
            close(fd_new);
            free(buffer);
            exit(EXIT_FAILURE);
        }
        ssize_t wb = write(fd_new, buffer, copyLength);
        if (wb != copyLength) {
            perror("Error while writing partition data");
            close(fd_new);
            free(buffer);
            exit(EXIT_FAILURE);
        }
        dataSizeLeft -= copyLength;
        dataSizeRead += copyLength;
        printProgressBar(dataSizeRead, partHeader->partitionSize);
    }
    printf("\n");
    close(fd_new);
    free(buffer);
}

int main(int argc, char** argv) {
    if (argc < 5) {
        printUsageAndExit();
    }

    if (strcmp(argv[1], "-h") == 0) {
        printUsage();
        exit(EXIT_SUCCESS);
    } else if (strcmp(argv[1], "-v") == 0) {
        printf("pacextractor version %s\n", VERSION);
        exit(EXIT_SUCCESS);
    } else if (strcmp(argv[1], "-e") == 0 && strcmp(argv[3], "-o") == 0) {
        // Process the extraction
        int fd = openFirmwareFile(argv[2]);
        
        struct stat st;
        if (fstat(fd, &st) == -1) {
            perror("Error getting file stats");
            exit(EXIT_FAILURE);
        }
        int firmwareSize = st.st_size;
        if (firmwareSize < sizeof(PacHeader)) {
            fprintf(stderr, "File %s is not a valid firmware\n", argv[2]);
            close(fd);
            exit(EXIT_FAILURE);
        }

        char* outputPath = argv[4];
        createOutputDirectory(outputPath);

        PacHeader pacHeader = readPacHeader(fd);
        
        char firmwareName[256];
        getString(pacHeader.firmwareName, firmwareName);
        printf("Firmware name: %s\n", firmwareName);

        uint32_t curPos = pacHeader.partitionsListStart;
        PartitionHeader** partHeaders = malloc(pacHeader.partitionCount * sizeof(PartitionHeader*));
        if (partHeaders == NULL) {
            perror("Memory allocation failed for partition headers");
            close(fd);
            exit(EXIT_FAILURE);
        }

        for (int i = 0; i < pacHeader.partitionCount; i++) {
            partHeaders[i] = readPartitionHeader(fd, &curPos);
            
            char partitionName[256];
            char fileName[512];
            getString(partHeaders[i]->partitionName, partitionName);
            getString(partHeaders[i]->fileName, fileName);
            printf("Partition name: %s\n\twith file name: %s\n\twith size %u\n",
                   partitionName, fileName, partHeaders[i]->partitionSize);
        }

        for (int i = 0; i < pacHeader.partitionCount; i++) {
            extractPartition(fd, partHeaders[i], outputPath);
            free(partHeaders[i]);
        }
        free(partHeaders);
        close(fd);
        
        return EXIT_SUCCESS;
    } else {
        printUsageAndExit();
    }

    return EXIT_SUCCESS;
}
