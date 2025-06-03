#pragma once
#include <array>
#include <string>

#include "Semaphore.h"
#include "Utils.h"

constexpr std::streamsize BLOCK_SIZE = 1'000'000;
constexpr std::streamsize BLOCK_NUM = 3;
extern std::string EmptyBlocksSemaphoreName;
extern std::string BlocsToWriteSemaphoreName;

struct Block {
    char data[BLOCK_SIZE];
    std::streamsize size;
	int id; // Unique identifier for the block, can be used for debugging or tracking.
};

using BlocksArray = std::array<Block, BLOCK_NUM>;

struct SharedMemory {
    int NextReadBlock = 0;
    int NextWriteBlock = 0;
    BlocksArray blocks;
};

class DataTransfer
{
public:
	DataTransfer(const DataTransfer& other) = delete;
	DataTransfer& operator=(const DataTransfer&) = delete; // No assignment allowed.
	DataTransfer(DataTransfer&& other) noexcept;

	// Call only one time in the beginning of the reading process.
	void InitReading();

	explicit DataTransfer(const char* sharedMemoryOSName);

	class DataTransferInterface {
	public:
		explicit DataTransferInterface(BlocksArray* shared, int* counterIn, Semaphore* semaphoreIn, Semaphore* semaphoreOut);
		Block& GetBlock();

		void SignalBlock();

	private:
		BlocksArray* shared_;
		int* counterIn_;
		Semaphore* semaphoreIn_;
		Semaphore* semaphoreOut_;
	};

	DataTransferInterface GetReaderInterface();
	DataTransferInterface GetWriterInterface();

private:
	// consider using a message queue
	SharedMemory* sharedMemory_ = nullptr;
	Semaphore emptyBlocksSemaphore_{ EmptyBlocksSemaphoreName };
	Semaphore blocksToWriteSemaphore_{ BlocsToWriteSemaphoreName };
	HANDLE hMapping_;
};
