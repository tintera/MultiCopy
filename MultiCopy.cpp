// MultiCopy.cpp : This file contains the 'main' function. Program execution begins and ends there.
#include <iostream>
#include <array>
#include <condition_variable>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include <windows.h>
#include <tchar.h> // For TCHAR and TEXT macro

using namespace std::literals::string_literals;

namespace 
{
constexpr std::streamsize BLOCK_SIZE = 100; //1'000'000;
constexpr std::streamsize BLOCK_NUM = 3;
wchar_t sharedMemoryOsName[] = L"MultiCopySharedMemory\0";
auto EmptyBlocksSemaphoreName = "MultiCopyEmptyBlocksSemaphore"s;
auto BlocsToWriteSemaphoreName = "MultiCopyBlocksToWriteSemaphore"s;

class DataTransfer;

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

	explicit Semaphore(const std::string& name)
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
			throw std::runtime_error("Failed to create semaphore: " + std::to_string(GetLastError()));
		}
		creationResult_ = (GetLastError() == ERROR_ALREADY_EXISTS) ? CreationResult::Joined : CreationResult::Created;
	}
	enum class CreationResult : uint8_t
	{
		Unknown,
		Created, // Semaphore was created by this process
		Joined, // Semaphore was already created by another process
	};
	~Semaphore()
	{
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
		if (!ReleaseSemaphore(hSemaphore_, 1, nullptr))
		{
			throw std::runtime_error("Failed to signal semaphore: " + std::to_string(GetLastError()));
		}
	}

	// ReSharper disable once CppMemberFunctionMayBeConst // This function modifies underlying OS semaphore state.
	WaitResult Wait(const std::chrono::milliseconds timeout)
	{
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
private:
	HANDLE hSemaphore_;
	CreationResult creationResult_ = CreationResult::Unknown;
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

class DataTransfer
{
private:
	// consider using a message queue
	SharedMemory *sharedMemory_ = nullptr;
	Semaphore emptyBlocksSemaphore_{ EmptyBlocksSemaphoreName };
	Semaphore blocksToWriteSemaphore_{ BlocsToWriteSemaphoreName };
public:
	explicit DataTransfer()
	{
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
		for (int i = 0; i < BLOCK_NUM; ++i)
		{
			emptyBlocksSemaphore_.Signal();
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
			auto res = semaphoreIn_->Wait(std::chrono::milliseconds(500)); // No block released in 1/10th of second is considered as other side not working or broken pipeline.
			if (res == Semaphore::WaitResult::Signaled)
			{
				auto& shared = *shared_;
				auto& counterIn = *counterIn_;
				counterIn = (++counterIn) % 3;
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
		const RoleCheck roleCheck{};
		const auto role = roleCheck.GetRole();
		if (role == RoleCheck::Role::Exit)
		{
			std::cout << "Maximum of two processes is allowed at same time. This one was decided to be third or more.\n";
			return 0;
		}
		std::cout << "Role: " << (role == RoleCheck::Role::Reader ? "Reader" : "Writer") << '\n';

		// Read command-line parameters here.
		// Simplified parameter checking so we do not need to add dependencies.
		if (argc < 3) {
			std::cerr << "Usage: " << argv[0] << " <input_file> <output_file>\n";
			return 1;
		}
		const char* inputFileName = argv[1];
		const char* outputFileName = argv[2];

		std::ios::sync_with_stdio(false);


		DataTransfer sharedMemory{}; // Create shared memory and semaphores.

		if (role == RoleCheck::Role::Reader)
		{
			// Reader code here.
			std::cout << "Reader process started.\n";

			// open input file
			std::ifstream inputFile(inputFileName, std::ios::binary);
			if (!inputFile.is_open()) {
				std::cerr << "Error: Could not open input file " << inputFileName << "\n";
				return 1;
			}

			auto rdMem = sharedMemory.GetReaderInterface();
			bool finished = false;
			do
			{
				auto& [data, size] = rdMem.GetBlock();

				// Read from file into emptyBlock.
				std::cout << "Loading a block.\n";
				inputFile.read(data, BLOCK_SIZE);
				size = inputFile.gcount();
				std::cout << "Loaded " << size << " bytes.\n";
				finished = (size != BLOCK_SIZE);

				rdMem.SignalBlock();  // Allow processing of the loaded block
			} while (!finished);
			inputFile.close();
		}
		if (role == RoleCheck::Role::Writer)
		{
			// Writer code here.
			std::cout << "Writer process started.\n";

			// open output file
			std::ofstream outputFile(outputFileName, std::ios::binary);
			if (!outputFile.is_open()) {
				std::cerr << "Error: Could not open output file " << outputFileName << "\n";
				return 1;
			}

			auto wrMem = sharedMemory.GetWriterInterface();
			bool finished = false;
			do // last block has full size
			{
				auto& [data, size] = wrMem.GetBlock();

				// Write to file from block.
				std::cout << "Writing a block.\n";
				outputFile.write(data, size);
				std::cout << "Wrote " << size << " bytes.\n";
				finished = (size != BLOCK_SIZE);

				wrMem.SignalBlock();
				finished = (size < BLOCK_SIZE);
			} while (!finished /*write not finished*/);
			outputFile.close();
		}
		std::cout << "File copied.\n";
	}
	catch (std::exception& e)
	{
		std::cerr << "Exception: " << e.what() << '\n';
		return 1;
	}
	catch (...)
	{
		return 2; // Unknown exception
	}
}

// Note: This code is a simplified example and may require additional error handling and cleanup in a production environment.
// It is designed to demonstrate the use of shared memory and semaphores for inter-process communication in a Windows environment.
// For production also consider using a more sophisticated error handling mechanism, such as logging errors to a file or using a custom exception class.

// The code assumes that the reader and writer processes will be run separately and that they will communicate through the shared memory.
// The shared memory is created with a fixed size and the blocks are used to transfer data between the reader and writer.
// Note: This code is specific to Windows due to the use of Windows API for shared memory and semaphores.
// To compile this code, you need to link against the Windows libraries.
// But it's used the way it allows OS-agnostic code to be written, so it can be used in a cross-platform project.
// The code uses C++ standard library features for file I/O and exception handling.
// The BLOCK_SIZE and BLOCK_NUM constants define the size of each block and the number of blocks used for data transfer.

// Possible optimizations:
// For large files test using larger BLOCK_SIZE, e.g. 10 MB or more.
// Role uses Semaphore to determine if it is Reader or Writer. It can be reused later for the price of less clean interfaces or Role and DataTransfer classes.
// Maybe they can be merged in such a case.
// Consider using a message queue for more complex communication patterns.
