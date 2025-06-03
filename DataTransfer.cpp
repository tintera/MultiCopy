#include "DataTransfer.h"
#include "Utils.h"
#include <windows.h>
#include <stdexcept>
#include <vector>
#include <string>
#include <spdlog/spdlog.h>

using namespace std::literals::string_literals;

std::string EmptyBlocksSemaphoreName = "MultiCopyEmptyBlocksSemaphore"s;
std::string BlocsToWriteSemaphoreName = "MultiCopyBlocksToWriteSemaphore"s;

DataTransfer::DataTransfer(DataTransfer&& other) noexcept: sharedMemory_{other.sharedMemory_},
                                                           emptyBlocksSemaphore_{std::move(other.emptyBlocksSemaphore_)},
                                                           blocksToWriteSemaphore_{std::move(other.blocksToWriteSemaphore_)},
                                                           hMapping_{other.hMapping_}
{
}

void DataTransfer::InitReading()
{
	for (int i = 0; i < BLOCK_NUM; ++i)
	{
		sharedMemory_->blocks[i].size = 0; // Initialize all blocks to empty.
		sharedMemory_->blocks[i].id = i; // Assign unique ID to each block.
		emptyBlocksSemaphore_.Signal();
	}
}

DataTransfer::DataTransfer(const char* sharedMemoryOSName)
{
	const std::string memName{ sharedMemoryOSName };
	auto sharedMemoryOsName = StringToWChar(memName);

	hMapping_ = CreateFileMapping(
		INVALID_HANDLE_VALUE,    // use paging file
		nullptr,                 // default security
		PAGE_READWRITE,          // read/write access
		0,                       // maximum object size (high-order DWORD)
		sizeof(SharedMemory),    // maximum object size (low-order DWORD)
		sharedMemoryOsName.data());     // name of mapping object
	if (hMapping_ == nullptr)
	{

		const DWORD errCode = GetLastError();
		const std::string systemMessage = GetLastErrorMessage(errCode);
		const std::string errorMessage = std::format("Failed to create file mapping: {} - {}.", std::to_string(errCode), systemMessage);
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

DataTransfer::DataTransferInterface::DataTransferInterface(BlocksArray* shared, int* counterIn, Semaphore* semaphoreIn,
                                                           Semaphore* semaphoreOut): shared_(shared), counterIn_(counterIn), semaphoreIn_(semaphoreIn), semaphoreOut_(semaphoreOut)
{}

// ReSharper disable once CppMemberFunctionMayBeConst // This function modifies the semaphore state. And that changes behavior of the object.
Block& DataTransfer::DataTransferInterface::GetBlock()
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
void DataTransfer::DataTransferInterface::SignalBlock()
{
	semaphoreOut_->Signal();
}

DataTransfer::DataTransferInterface DataTransfer::GetReaderInterface()
{
	return DataTransferInterface(&sharedMemory_->blocks, &sharedMemory_->NextReadBlock, &emptyBlocksSemaphore_, &blocksToWriteSemaphore_);
}

DataTransfer::DataTransferInterface DataTransfer::GetWriterInterface()
{
	return DataTransferInterface(&sharedMemory_->blocks, &sharedMemory_->NextWriteBlock, &blocksToWriteSemaphore_, &emptyBlocksSemaphore_);
}
