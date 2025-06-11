#include "RoleCheck.h"
#include "DataTransfer.h"
#include "Utils.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <fstream>
#include <iostream>

#include "LoggingIfStream.h"
#include "LoggingOfstream.h"

using namespace std::string_literals;

namespace
{
	// Reads maximum BLOCK_SIZE bytes from the inputFile into block.
	// inputFileName is used only for error messages.
	std::streamsize ReadFromFile(const std::string& inputFileName, std::ifstream& inputFile, Block& block)
	{
		spdlog::info("Reading a block.");
		inputFile.read(block.data, BLOCK_SIZE);
		if (inputFile.eof())
		{
			spdlog::info("End of file reached.");
		}
		else
		{
			if (inputFile.fail()) {
				const std::string errorMessage = std::format("Failed to read from file: {}: {}", inputFileName, GetLastErrorMessage(errno));
				inputFile.close();
				throw std::runtime_error(errorMessage);
			}
		}
		block.size = inputFile.gcount();
		block.error = false;
		spdlog::info("Loaded {} bytes to block {}.", block.size, block.id);
		return block.size;
	}

	std::string getFinisherName(const std::string& dataTransferName)
	{
		return "finisher_"s + dataTransferName;
	}

	void ReaderFinish(DataTransfer dataTransfer)
	{
		Semaphore finisher{ getFinisherName(dataTransfer.GetName()) };
		auto res = finisher.Wait(static_cast<std::chrono::milliseconds>(0));
		if (res != Semaphore::WaitResult::Signaled)
		{
			spdlog::info("Waiting for Writer to start and get role.");
			finisher.Wait();
		}else
		{
			spdlog::info("Reader detected it's Writer process.");
		}
		spdlog::info("Reader process finishing");
	}

	int DoRead(const std::string& inputFileName, DataTransfer dataTransfer)
	{
		spdlog::info("Reader process started.");
		dataTransfer.InitReading();
		auto rdMem = dataTransfer.GetReaderInterface();
		// open input file
		LoggingIfstream inputFile(inputFileName, std::ios::binary);
		if (!inputFile.get().is_open()) {
			spdlog::error("Error: Could not open input file {}.", inputFileName);
			Block& block = rdMem.GetBlock();
			block.error = true;
			rdMem.SignalBlock();  // Allow writer to get it and see error.
			ReaderFinish(std::move(dataTransfer));
			return EXIT_FAILURE;
		}
		inputFile.get().exceptions(std::ifstream::badbit);
		spdlog::debug("Input file opened: {}", inputFileName);
		try
		{
			// ReSharper disable once CppInitializedValueIsAlwaysRewritten // safety default value
			bool finished = false;
			do
			{
				Block& block = rdMem.GetBlock();
				finished = (BLOCK_SIZE != ReadFromFile(inputFileName, inputFile.get(), block));
				rdMem.SignalBlock();  // Allow processing of the loaded block
			} while (!finished);
		}
		catch (const std::ios_base::failure& e) {
			spdlog::error("I/O error while reading from file: {}: {}", inputFileName, e.what());
			rdMem.SignalBlock(); //TODO: can this be part of a destructor?
			return EXIT_FAILURE;
		}
		spdlog::info("File reading finished.");

		ReaderFinish(std::move(dataTransfer));
		return EXIT_SUCCESS;
	}

	int DoWrite(const std::string& outputFileName, DataTransfer dataTransfer)
	{
		spdlog::info("Writer process started.");
		Semaphore finisher{ (getFinisherName(dataTransfer.GetName())) };
		finisher.Signal();
		DataTransfer::DataTransferInterface wrMem = dataTransfer.GetWriterInterface();

		// open output file
		// TODO: do not open if source file does not exist or is not readable
		LoggingOfstream outputFile(outputFileName, std::ios::binary);
		if (!outputFile.get().is_open()) {
			spdlog::error("Error: Could not open output file {}.", outputFileName);
			Block& block = wrMem.GetBlock();
			block.error = true;
			wrMem.SignalBlock();
			return EXIT_FAILURE;
		}
		outputFile.get().exceptions(std::ifstream::failbit | std::ifstream::badbit);
		spdlog::debug("Output file opened: {}", outputFileName);
		// ReSharper disable once CppInitializedValueIsAlwaysRewritten // safety default value
		bool finished = false;
		do // last block has full size
		{
			Block& block = wrMem.GetBlock();
			// Write to file from block.
			spdlog::info("Writing a block #{}.", block.id);
			outputFile.get().write(block.data, block.size);
			block.error = false;
			spdlog::info("Wrote {} bytes.", block.size);

			wrMem.SignalBlock();
			finished = (block.size < BLOCK_SIZE);
		} while (!finished);
		spdlog::info("File saved");
		return EXIT_SUCCESS;
	}

	void ConfigureLogger()
	{
		std::vector<spdlog::sink_ptr> sinks;
		const auto wincolor_sink = std::make_shared<spdlog::sinks::stdout_color_sink_st>();
		wincolor_sink->set_level(spdlog::level::info);
		sinks.push_back(wincolor_sink);
		const auto rotating_file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_st>(std::format("multiCopy_{}_log.txt", spdlog::details::os::pid()), 1024 * 1024, 5);
		rotating_file_sink->set_level(spdlog::level::debug);
		sinks.push_back(rotating_file_sink);
		const auto logger = std::make_shared<spdlog::logger>("file_logger", begin(sinks), end(sinks));
		logger->flush_on(spdlog::level::err);
		spdlog::set_default_logger(logger);
		spdlog::set_level(spdlog::level::debug);
	}

	struct Config
	{
		std::string inputFileName;
		std::string outputFileName;
		std::string sharedMemoryName;
	};

	Config ReadCommandlineParameters(const int argc, const char* const* argv)
	{
		// Read command-line parameters here.
		// Simplified parameter checking so we do not need to add dependencies.
		spdlog::info("Command-line parameters: argc = {}, argv[0] = {}", argc, argv[0]);
		spdlog::info(argv[1]);
		spdlog::info(argv[2]);
		spdlog::info(argv[3]);
		if (argc != 4) {
			throw std::runtime_error(std::format("Usage: {} <input_file> <output_file> <shared_mem_name>", argv[0]));
		}
		Config config;
		config.inputFileName = argv[1];
		config.outputFileName = argv[2];
		config.sharedMemoryName = argv[3];
		return config;
	}
}

int main(const int argc, const char* const argv[]) {
	try
	{
		int resultCode = EXIT_FAILURE;
		ConfigureLogger();
		{
			const RoleCheck roleCheck{};
			const RoleCheck::Role role = roleCheck.GetRole();
			const Config config = ReadCommandlineParameters(argc, argv);
			std::ios::sync_with_stdio(false);
			DataTransfer dataTransfer{ config.sharedMemoryName, role }; // Create shared memory and semaphores.

			switch (role) {
			case RoleCheck::Role::Reader: resultCode = DoRead(config.inputFileName, std::move(dataTransfer)); break;
			case RoleCheck::Role::Writer: resultCode = DoWrite(config.outputFileName, std::move(dataTransfer)); break;
			case RoleCheck::Role::Exit: resultCode = EXIT_FAILURE; break;
			}
		}
		spdlog::shutdown();
		return resultCode;
	}
	catch (std::runtime_error& e)
	{
		spdlog::error("Exception: {}", e.what());
		spdlog::shutdown();
		return EXIT_FAILURE;
	}
	catch (...)
	{
		spdlog::shutdown();
		return EXIT_FAILURE;
	}
}
