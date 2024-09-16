package main

import (
	"encoding/binary"
	"flag"
	"fmt"
	"io"
	"os"
	"path/filepath"
)

// Version of the extractor
const Version = "1.0.0"

// PacHeader is the structure for the PAC file header
type PacHeader struct {
	SomeField           [24]int16
	SomeInt             int32
	ProductName         [256]int16
	FirmwareName        [256]int16
	PartitionCount      int32
	PartitionsListStart int32
	SomeIntFields1      [5]int32
	ProductName2        [50]int16
	SomeIntFields2      [6]int16
	SomeIntFields3      [2]int16
}

// PartitionHeader is the structure for each partition in the PAC file
type PartitionHeader struct {
	Length             uint32
	PartitionName      [256]int16
	FileName           [512]int16
	PartitionSize      uint32
	SomeFields1        [2]int32
	PartitionAddrInPac uint32
	SomeFields2        [3]int32
}

// getString converts an int16 array to a UTF-8 string
func getString(baseString []int16) string {
	var resString string
	for _, ch := range baseString {
		if ch == 0 {
			break
		}
		resString += string(rune(ch))
	}
	return resString
}

// printUsage prints the usage information
func printUsage() {
	fmt.Println("Usage: pacextractor <firmware name>.pac <output path>")
	fmt.Println("Options:")
	fmt.Println("  -h               Show this help message and exit")
	fmt.Println("  -v               Show version information and exit")
}

// openFirmwareFile opens the firmware file
func openFirmwareFile(filePath string) (*os.File, error) {
	fd, err := os.Open(filePath)
	if err != nil {
		return nil, fmt.Errorf("Error opening file %s: %w", filePath, err)
	}
	return fd, nil
}

// createOutputDirectory creates the output directory if it doesn't exist
func createOutputDirectory(path string) error {
	if _, err := os.Stat(path); os.IsNotExist(err) {
		err = os.MkdirAll(path, 0777)
		if err != nil {
			return fmt.Errorf("Failed to create output directory: %w", err)
		}
		fmt.Printf("Created output directory: %s\n", path)
	}
	return nil
}

// readPacHeader reads the PAC file header
func readPacHeader(fd *os.File) (PacHeader, error) {
	var header PacHeader
	err := binary.Read(fd, binary.LittleEndian, &header)
	if err != nil {
		return header, fmt.Errorf("Error while reading PAC header: %w", err)
	}
	return header, nil
}

// readPartitionHeader reads the partition header at the current file offset
func readPartitionHeader(fd *os.File) (PartitionHeader, error) {
	var header PartitionHeader
	err := binary.Read(fd, binary.LittleEndian, &header)
	if err != nil {
		return header, fmt.Errorf("Error while reading partition header: %w", err)
	}
	return header, nil
}

// printProgressBar prints a progress bar
func printProgressBar(completed, total uint32) {
	const barWidth = 50
	progress := float64(completed) / float64(total)
	pos := int(barWidth * progress)

	fmt.Print("\r[")
	for i := 0; i < barWidth; i++ {
		if i < pos {
			fmt.Print("=")
		} else if i == pos {
			fmt.Print(">")
		} else {
			fmt.Print(" ")
		}
	}
	fmt.Printf("] %.2f%%", progress*100)
}

// extractPartition extracts a partition to a file
func extractPartition(fd *os.File, partHeader PartitionHeader, outputPath string) error {
	if partHeader.PartitionSize == 0 {
		return nil
	}

	_, err := fd.Seek(int64(partHeader.PartitionAddrInPac), io.SeekStart)
	if err != nil {
		return fmt.Errorf("Error seeking partition data: %w", err)
	}

	// Increase buffer size for faster I/O operations
	const bufferSize = 256 * 1024 // 256 KB
	buffer := make([]byte, bufferSize)

	fileName := getString(partHeader.FileName[:])
	outputFilePath := filepath.Join(outputPath, fileName)

	// Remove existing file if it exists
	err = os.Remove(outputFilePath)
	if err != nil && !os.IsNotExist(err) {
		return fmt.Errorf("Error removing existing output file: %w", err)
	}

	fdNew, err := os.OpenFile(outputFilePath, os.O_WRONLY|os.O_CREATE|os.O_TRUNC, 0666)
	if err != nil {
		return fmt.Errorf("Error creating output file: %w", err)
	}
	defer fdNew.Close()

	fmt.Printf("Extracting to %s\n", outputFilePath)

	dataSizeLeft := partHeader.PartitionSize
	var dataSizeRead uint32

	for dataSizeLeft > 0 {
		copyLength := bufferSize
		if int(dataSizeLeft) < bufferSize {
			copyLength = int(dataSizeLeft)
		}

		n, err := io.ReadFull(fd, buffer[:copyLength])
		if err != nil {
			return fmt.Errorf("Error while reading partition data: %w", err)
		}

		_, err = fdNew.Write(buffer[:n])
		if err != nil {
			return fmt.Errorf("Error while writing partition data: %w", err)
		}

		dataSizeLeft -= uint32(n)
		dataSizeRead += uint32(n)
		printProgressBar(dataSizeRead, partHeader.PartitionSize)
	}

	fmt.Println()
	return nil
}

func main() {
	flag.Usage = printUsage
	flagVersion := flag.Bool("v", false, "Show version information and exit")
	flagHelp := flag.Bool("h", false, "Show help message and exit")
	flag.Parse()

	if *flagHelp {
		printUsage()
		os.Exit(0)
	}

	if *flagVersion {
		fmt.Printf("pacextractor version %s\n", Version)
		os.Exit(0)
	}

	args := flag.Args()
	if len(args) < 2 {
		printUsage()
		os.Exit(1)
	}

	firmwarePath := args[0]
	outputPath := args[1]

	fd, err := openFirmwareFile(firmwarePath)
	if err != nil {
		fmt.Println(err)
		os.Exit(1)
	}
	defer fd.Close()

	fileInfo, err := fd.Stat()
	if err != nil {
		fmt.Printf("Error getting file stats: %v\n", err)
		os.Exit(1)
	}

	firmwareSize := fileInfo.Size()
	if firmwareSize < int64(binary.Size(PacHeader{})) {
		fmt.Printf("file %s is not a valid firmware\n", firmwarePath)
		os.Exit(1)
	}

	err = createOutputDirectory(outputPath)
	if err != nil {
		fmt.Println(err)
		os.Exit(1)
	}

	pacHeader, err := readPacHeader(fd)
	if err != nil {
		fmt.Println(err)
		os.Exit(1)
	}

	firmwareName := getString(pacHeader.FirmwareName[:])
	fmt.Printf("Firmware name: %s\n", firmwareName)

	curPos := int64(pacHeader.PartitionsListStart)
	partHeaders := make([]PartitionHeader, pacHeader.PartitionCount)

	for i := 0; i < int(pacHeader.PartitionCount); i++ {
		_, err := fd.Seek(curPos, io.SeekStart)
		if err != nil {
			fmt.Printf("Error seeking to partition %d: %v\n", i, err)
			os.Exit(1)
		}

		partHeader, err := readPartitionHeader(fd)
		if err != nil {
			fmt.Printf("Error reading partition %d: %v\n", i, err)
			os.Exit(1)
		}

		partHeaders[i] = partHeader

		partitionName := getString(partHeader.PartitionName[:])
		fileName := getString(partHeader.FileName[:])
		fmt.Printf("Partition name: %s\n\twith file name: %s\n\twith size %d\n", partitionName, fileName, partHeader.PartitionSize)

		curPos += int64(partHeader.Length)
	}

	for i := 0; i < int(pacHeader.PartitionCount); i++ {
		err = extractPartition(fd, partHeaders[i], outputPath)
		if err != nil {
			fmt.Printf("Error extracting partition %d: %v\n", i, err)
			os.Exit(1)
		}
	}
}
