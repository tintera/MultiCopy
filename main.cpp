#include "RoleCheck.h"
#include "DataTransfer.h"
#include "Utils.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <fstream>
#include <iostream>

//TODO: When the file is very small both processes become readers.

namespace 
{
	// Reads maximum BLOCK_SIZE bytes from the inputFile into block.
	// inputFileName is used only for error messages.
	std::streamsize ReadFromFile(const char* inputFileName, std::ifstream& inputFile, Block& block)
	{
		spdlog::info("Reading a block.");
		inputFile.read(block.data, BLOCK_SIZE);
		if (inputFile.eof())
		{
			spdlog::warn("End of file reached.");
		}
		else
		{
			if (inputFile.fail()) {
				const std::string errorMessage = std::format("Failed to read from file: {}: {}", inputFileName, GetLastErrorMessage(errno));
				inputFile.close();
				throw std::ios_base::failure(errorMessage);
			}
		}
		block.size = inputFile.gcount();
		spdlog::info("Loaded {} bytes.", block.size);
		return block.size;
	}

int DoReader(const char* inputFileName, DataTransfer dataTransfer)
{
	// open input file
	std::ifstream inputFile(inputFileName, std::ios::binary);
	if (!inputFile.is_open()) {
		spdlog::error("Error: Could not open input file {}.", inputFileName);
		return EXIT_FAILURE;
	}
    inputFile.exceptions( std::ifstream::badbit);
	spdlog::debug("Input file opened: {}", inputFileName);
	dataTransfer.InitReading();
	auto rdMem = dataTransfer.GetReaderInterface();
	// ReSharper disable once CppInitializedValueIsAlwaysRewritten // safety default value
	try
	{
		bool finished = false;
		do
		{
			Block& block = rdMem.GetBlock();
			finished = (BLOCK_SIZE != ReadFromFile(inputFileName, inputFile, block));
			rdMem.SignalBlock();  // Allow processing of the loaded block
		} while (!finished);
	}
	catch (const std::ios_base::failure& e) {
		spdlog::error("I/O error while reading from file: {}: {}", inputFileName, e.what());
		inputFile.close();
		return EXIT_FAILURE;
	}
	inputFile.close();
	spdlog::warn("File reading finished.");
	return EXIT_SUCCESS;
}

int DoWrite(const char* outputFileName, DataTransfer dataTransfer)
{
	// open output file
	std::ofstream outputFile(outputFileName, std::ios::binary);
	if (!outputFile.is_open()) {
		spdlog::error("Error: Could not open output file {}.", outputFileName);
		return EXIT_FAILURE;
	}
	outputFile.exceptions(std::ifstream::failbit);
	spdlog::debug("Output file opened: {}", outputFileName);
	DataTransfer::DataTransferInterface wrMem = dataTransfer.GetWriterInterface();
	// ReSharper disable once CppInitializedValueIsAlwaysRewritten // safety default value
	bool finished = false;
	do // last block has full size
	{
		Block& block = wrMem.GetBlock();

		// Write to file from block.
		spdlog::warn("Writing a block.");
		outputFile.write(block.data, block.size);
		spdlog::warn("Wrote {} bytes.", block.size);

		wrMem.SignalBlock();
		finished = (block.size < BLOCK_SIZE);
	} while (!finished);
	outputFile.close();
	spdlog::warn("File saved");
	return EXIT_SUCCESS;
}

}

int main(const int argc, const char* const argv[]) {
	try
	{
		const auto logger = spdlog::basic_logger_mt("file_logger", "shared_log.txt");
		spdlog::set_default_logger(logger);
		spdlog::set_level(spdlog::level::info);
		const RoleCheck roleCheck{};
		const auto role = roleCheck.GetRole();
		if (role == RoleCheck::Role::Exit)
		{
			spdlog::info("Maximum of two processes is allowed at same time. This one was decided to be third or more.");
			return EXIT_FAILURE;
		}
		spdlog::info("Role: {}", (role == RoleCheck::Role::Reader) ? "Reader" : "Writer");

		// Read command-line parameters here.
		// Simplified parameter checking so we do not need to add dependencies.
		spdlog::info("Command-line parameters: argc = {}, argv[0] = {}", argc, argv[0]);
		spdlog::info(argv[1]);
		spdlog::info(argv[2]);
		spdlog::info(argv[3]);
		if (argc != 4) {
			spdlog::error("Usage: {} <input_file> <output_file> <shared_mem_name>", argv[0]);
			return EXIT_FAILURE;
		}

		const char* inputFileName = argv[1];
		const char* outputFileName = argv[2];
		const char* sharedMemoryNameArg = argv[3];

		std::ios::sync_with_stdio(false);

		DataTransfer dataTransfer{ sharedMemoryNameArg }; // Create shared memory and semaphores.

		if (role == RoleCheck::Role::Reader)
		{
			spdlog::info("Reader process started.");
			return DoReader(inputFileName, std::move(dataTransfer));
		}
		if (role == RoleCheck::Role::Writer)
		{
			spdlog::warn("Writer process started.");
			return DoWrite(outputFileName, std::move(dataTransfer));
		}
		return EXIT_FAILURE;
	}
	catch (std::exception& e)
	{
		spdlog::error("Exception: {}", e.what());
		return EXIT_FAILURE;
	}
	catch (...)
	{
		return EXIT_FAILURE;
	}
}
