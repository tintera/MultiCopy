// MultiCopy.cpp : This file contains the 'main' function. Program execution begins and ends there.
#include <iostream>
#include <array>
#include <condition_variable>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <windows.h>
#include <tchar.h> // For TCHAR and TEXT macro

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

using namespace std::literals::string_literals;

namespace 
{
constexpr std::streamsize BLOCK_SIZE = 1'000'000; //1'000'000;
constexpr std::streamsize BLOCK_NUM = 3;
wchar_t sharedMemoryOsName[] = L"MultiCopySharedMemory\0";
auto EmptyBlocksSemaphoreName = "MultiCopyEmptyBlocksSemaphore"s;
auto BlocsToWriteSemaphoreName = "MultiCopyBlocksToWriteSemaphore"s;

class DataTransfer;

std::string GetLastErrorMessage(const DWORD err)
{
	LPWSTR msgBuf = nullptr;
	FormatMessageW(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr,
		err,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPWSTR)&msgBuf,
		0, nullptr);

	std::wstring errorMsg = L"Failed to signal semaphore: " + std::to_wstring(err);
	if (msgBuf) {
		errorMsg += L" - ";
		errorMsg += msgBuf;
		LocalFree(msgBuf);
	}
	// Convert to UTF-8 for std::runtime_error
	int size_needed = WideCharToMultiByte(CP_UTF8, 0, errorMsg.c_str(), -1, nullptr, 0, nullptr, nullptr);
	std::string errorMsgUtf8(size_needed, 0);
	WideCharToMultiByte(CP_UTF8, 0, errorMsg.c_str(), -1, &errorMsgUtf8[0], size_needed, nullptr, nullptr);
	return errorMsgUtf8;
}

// OS-agnostic semaphore abstraction
class Semaphore
{
public:
	enum class WaitResult : uint8_t
	{
		Signaled,
		WaitAbandoned,
		Timeout,
		WaitFailed
	};

	explicit Semaphore(const std::string& name) : name_(name)
	{
		const auto temp = std::wstring(name.begin(), name.end());
		const LPCWSTR sw = temp.c_str();
		hSemaphore_ = CreateSemaphore(
			nullptr,               // default security attributes
			0,                     // initial count
			BLOCK_NUM,             // maximum count
			sw);                   // name of the semaphore
		if (hSemaphore_ == nullptr)
		{
			spdlog::error("Failed to create semaphore {}: {}", name_, GetLastErrorMessage(GetLastError()));
			throw std::runtime_error("Failed to create semaphore: " + std::to_string(GetLastError()));
		}
		creationResult_ = (GetLastError() == ERROR_ALREADY_EXISTS) ? CreationResult::Joined : CreationResult::Created;
		spdlog::debug("Created semaphore: {}, creation result: {}", name_, (creationResult_ == CreationResult::Created) ? "Created" : "Joined");
	}
	enum class CreationResult : uint8_t
	{
		Unknown,
		Created, // Semaphore was created by this process
		Joined, // Semaphore was already created by another process
	};
	~Semaphore()
	{
		spdlog::debug("Destroying semaphore: {}", name_);
		if (hSemaphore_ != nullptr)
		{
			CloseHandle(hSemaphore_);
		}
	}
	// Deleted copy constructor and assignment operator to prevent copying.
	Semaphore(const Semaphore&) = delete;
	Semaphore& operator=(const Semaphore&) = delete;

	[[nodiscard]] CreationResult Created() const
	{
		return creationResult_;
	}

	// ReSharper disable once CppMemberFunctionMayBeConst // This function modifies underlying OS semaphore state.
    void Signal()
	{
		spdlog::debug("Signaling semaphore: {}", name_);
        if (!ReleaseSemaphore(hSemaphore_, 1, nullptr))
        {
            const DWORD err = GetLastError();
			spdlog::error("Failed to signal semaphore {}: {}", name_, GetLastErrorMessage(err));
            throw std::runtime_error(GetLastErrorMessage(err));
        }
    }

	// ReSharper disable once CppMemberFunctionMayBeConst // This function modifies underlying OS semaphore state.
	WaitResult Wait(const std::chrono::milliseconds timeout)
	{
		spdlog::debug("Waiting for semaphore: {}", name_);
		switch (WaitForSingleObject(hSemaphore_, static_cast<DWORD>(timeout.count())))
		{
		case WAIT_OBJECT_0:
			return WaitResult::Signaled;
		case WAIT_ABANDONED:
			return WaitResult::WaitAbandoned;
		case WAIT_TIMEOUT:
			return WaitResult::Timeout;
		default:
			return WaitResult::WaitFailed;
		}
	}

	std::string GetName() const
	{
		return name_;
	}
private:
	HANDLE hSemaphore_;
	CreationResult creationResult_ = CreationResult::Unknown;
	std::string name_;
};

struct Block {
	char data[BLOCK_SIZE];
	std::streamsize size;
};

using BlocksArray = std::array<Block, BLOCK_NUM>;
struct SharedMemory
{
	int NextReadBlock = 0;
	int NextWriteBlock = 0;
	BlocksArray blocks;
};

// TODO: Move to a separate file and include platform specific things only in its implementation.
class RoleCheck
{
public:
	friend DataTransfer;
	enum class Role : uint8_t
	{
		Reader,
		Writer,
		Exit // This process should exit
	};

	RoleCheck()
	{
		if (roleSemaphore_.Created() == Semaphore::CreationResult::Created)
		{
			// This is the first process, it will be the reader
			role_ = Role::Reader;
			roleSemaphore_.Signal(); // Enable one writer
		}
		else
		{
			// This is a second or other process, it will be the writer or terminated.
			if (const auto res = roleSemaphore_.Wait(std::chrono::milliseconds(500)); res == Semaphore::WaitResult::Signaled)
			{
				role_ = Role::Writer;
			}
			else if (res == Semaphore::WaitResult::Timeout || res == Semaphore::WaitResult::WaitFailed)
			{
				role_ = Role::Exit; // No reader available, exit.
			}
			else
			{
				throw std::runtime_error("Unexpected semaphore wait result: " + std::to_string(static_cast<int>(res)));
			}
		}
	}

	[[nodiscard]] Role GetRole() const
	{
		return role_;
	}
private:
	Semaphore roleSemaphore_{"MultiCopyRoleSemaphore"s};
	Role role_;
};

// Converts a UTF-8 std::string to a null-terminated std::vector<wchar_t>
std::vector<wchar_t> StringToWChar(const std::string& str) {
	int wlen = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
	if (wlen == 0) {
		throw std::runtime_error("StringToWChar: MultiByteToWideChar failed");
	}
	std::vector<wchar_t> wstr(wlen);
	if (MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, wstr.data(), wlen) == 0) {
		throw std::runtime_error("StringToWChar: MultiByteToWideChar failed");
	}
	return wstr;
}

class DataTransfer
{
private:
	// consider using a message queue
	SharedMemory *sharedMemory_ = nullptr;
	Semaphore emptyBlocksSemaphore_{ EmptyBlocksSemaphoreName };
	Semaphore blocksToWriteSemaphore_{ BlocsToWriteSemaphoreName };
public:
	// Call only one time in the beginning of the reading process.
	void InitReading()
	{
		for (int i = 0; i < BLOCK_NUM; ++i)
		{
			emptyBlocksSemaphore_.Signal();
		}
	}

	explicit DataTransfer(const char* sharedMemoryOSName)
	{
		const std::string memName{ sharedMemoryOSName };
		auto wSharedMemoryOsName = StringToWChar(memName);

		hMapping_ = CreateFileMapping(
			INVALID_HANDLE_VALUE,    // use paging file
			nullptr,                 // default security
			PAGE_READWRITE,          // read/write access
			0,                       // maximum object size (high-order DWORD)
			sizeof(SharedMemory),    // maximum object size (low-order DWORD)
			sharedMemoryOsName);     // name of mapping object
		if (hMapping_ == nullptr)
		{

			LPVOID lpMsgBuf;
			DWORD errCode = GetLastError();

			if (FormatMessage(
				FORMAT_MESSAGE_ALLOCATE_BUFFER |
				FORMAT_MESSAGE_FROM_SYSTEM |
				FORMAT_MESSAGE_IGNORE_INSERTS,
				NULL,
				errCode,
				MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
				(LPTSTR)&lpMsgBuf,
				0, NULL) == 0) 
			{
				throw std::runtime_error("FormatMessage failed: " + std::to_string(GetLastError()));
			}
			MessageBox(NULL, (LPCTSTR)lpMsgBuf, TEXT("Error"), MB_OK);
			std::string errorMessage = "Failed to create file mapping: " + std::to_string(errCode) + " - " + std::string(static_cast<char*>(lpMsgBuf));
			throw std::runtime_error(errorMessage);
		}
		sharedMemory_ = static_cast<SharedMemory*>(MapViewOfFile(
			hMapping_,
			FILE_MAP_ALL_ACCESS, // read/write access
			0,
			0,
			sizeof(SharedMemory)));
		if (sharedMemory_ == nullptr)
		{
			CloseHandle(hMapping_);
			throw std::runtime_error("Failed to map view of file: " + std::to_string(GetLastError()));
		}
	}

	DataTransfer(const DataTransfer&) = delete; // No copying allowed.
	DataTransfer& operator=(const DataTransfer&) = delete; // No assignment allowed.

	class DataTransferInterface {
	public:
		explicit DataTransferInterface(BlocksArray* shared, int* counterIn, Semaphore* semaphoreIn, Semaphore* semaphoreOut) : shared_(shared), counterIn_(counterIn), semaphoreIn_(semaphoreIn), semaphoreOut_(semaphoreOut) {}
		// ReSharper disable once CppMemberFunctionMayBeConst // This function modifies the semaphore state. And that changes behavior of the object.
		Block& GetBlock()
		{
			auto res = semaphoreIn_->Wait(std::chrono::milliseconds(500)); // No block released in timeout is considered as other side not working or broken pipeline.
			if (res == Semaphore::WaitResult::Signaled)
			{
				auto& shared = *shared_;
				auto& counterIn = *counterIn_;
				counterIn = (++counterIn) % BLOCK_NUM;
				return shared[counterIn];
			}
			throw std::runtime_error("Failed to get block: " + std::to_string(static_cast<int>(res)));
		}

		// ReSharper disable once CppMemberFunctionMayBeConst // This function modifies the semaphore state. And that changes behavior of the object.
		void SignalBlock()
		{
			semaphoreOut_->Signal();
		}
	private:
		BlocksArray* shared_;
		int* counterIn_;
		Semaphore* semaphoreIn_;
		Semaphore* semaphoreOut_;
	};
	
	DataTransferInterface GetReaderInterface()
	{
		return DataTransferInterface(&sharedMemory_->blocks, &sharedMemory_->NextReadBlock, &emptyBlocksSemaphore_, &blocksToWriteSemaphore_);
	}
	DataTransferInterface GetWriterInterface()
	{
		return DataTransferInterface(&sharedMemory_->blocks, &sharedMemory_->NextWriteBlock, &blocksToWriteSemaphore_, &emptyBlocksSemaphore_ );
	}

private:
	HANDLE hMapping_;
};

// What is there is no writer (or it failed)
// Separate synchronization.
struct SharedMemoryII
{
	struct infoCommandFromReaderToWriter;
	struct infoCommandFromWriterToReader;
	Block blocks[BLOCK_NUM];
};
}

int main(const int argc, const char* const argv[])
{
	try
	{
		auto logger = spdlog::basic_logger_mt("file_logger", "shared_log.txt");
		spdlog::set_default_logger(logger);
		spdlog::set_level(spdlog::level::info);
		const RoleCheck roleCheck{};
		const auto role = roleCheck.GetRole();
		if (role == RoleCheck::Role::Exit)
		{
			spdlog::info("Maximum of two processes is allowed at same time. This one was decided to be third or more.");
			return 0;
		}
		spdlog::info("Role: {}", (role == RoleCheck::Role::Reader) ? "Reader" : "Writer");

		// Read command-line parameters here.
		// Simplified parameter checking so we do not need to add dependencies.
		spdlog::info("Command-line parameters: argc = {}, argv[0] = {}", argc, argv[0]);
		if (argc != 4) {
			spdlog::error("Usage: {} <input_file> <output_file> <shared_mem_name>", argv[0]);
			return 1;
		}

		const char* inputFileName = argv[1];
		const char* outputFileName = argv[2];
		const char* sharedMemoryNameArg = argv[3];

		std::ios::sync_with_stdio(false);

		DataTransfer dataTransfer{ sharedMemoryNameArg }; // Create shared memory and semaphores.

		if (role == RoleCheck::Role::Reader)
		{
			// Reader code here.
			spdlog::info("Reader process started.");

			// open input file
			std::ifstream inputFile(inputFileName, std::ios::binary);
			if (!inputFile.is_open()) {
				spdlog::error("Error: Could not open input file {}.",  inputFileName);
				return 1;
			}
			dataTransfer.InitReading();
			auto rdMem = dataTransfer.GetReaderInterface();
			bool finished = false;
			do
			{
				auto& [data, size] = rdMem.GetBlock();

				// Read from file into emptyBlock.
				spdlog::info("Reading a block.");
				inputFile.read(data, BLOCK_SIZE);
				size = inputFile.gcount();
				spdlog::info("Loaded {} bytes.", size);
				finished = (size != BLOCK_SIZE);

				rdMem.SignalBlock();  // Allow processing of the loaded block
			} while (!finished);
			inputFile.close();
			spdlog::warn("File reading finished.");
		}
		if (role == RoleCheck::Role::Writer)
		{
			// Writer code here.
			spdlog::warn("Writer process started.");

			// open output file
			std::ofstream outputFile(outputFileName, std::ios::binary);
			if (!outputFile.is_open()) {
				spdlog::error("Error: Could not open output file {}.", outputFileName);
				return 1;
			}

			DataTransfer::DataTransferInterface wrMem = dataTransfer.GetWriterInterface();
			bool finished = false;
			do // last block has full size
			{
				auto& [data, size] = wrMem.GetBlock();

				// Write to file from block.
				spdlog::warn("Writing a block.");
				outputFile.write(data, size);
				spdlog::warn("Wrote {} bytes.", size);
				finished = (size != BLOCK_SIZE);

				wrMem.SignalBlock();
				finished = (size < BLOCK_SIZE);
			} while (!finished /*write not finished*/);
			outputFile.close();
			spdlog::warn("File saved");
		}
		return EXIT_SUCCESS;
	}
	catch (std::exception& e)
	{
		spdlog::error("Exception: {}", e.what());
		return EXIT_FAILURE;
	}
	catch (...)
	{
		return 2; // Unknown exception
	}
}

// Note: This code is a simplified example and may require additional error handling and cleanup in a production environment.
// It is designed to demonstrate the use of shared memory and semaphores for inter-process communication in a Windows environment.

// The code assumes that the reader and writer processes will be run separately and that they will communicate through the shared memory.
// The shared memory is created with a fixed size and the blocks are used to transfer data between the reader and writer.
// Note: This code is specific to Windows due to the use of Windows API for shared memory and semaphores.
// To compile this code, you need to link against the Windows libraries.
// But it's used the way it allows OS-agnostic code to be written, so it can be used in a cross-platform project.
// The BLOCK_SIZE and BLOCK_NUM constants define the size of each block and the number of blocks used for data transfer.

// Possible optimizations:
// For large files test using larger BLOCK_SIZE, e.g. 10 MB or more.
// Role uses Semaphore to determine if it is Reader or Writer. It can be reused later for the price of less clean interfaces or Role and DataTransfer classes.
// Maybe they can be merged in such a case.
// Consider using a message queue for more complex communication patterns.
