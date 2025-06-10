#pragma once
#include <array>
#include <string>

#include "Block.h"
#include "RoleCheck.h"
#include "Semaphore.h"

extern std::string EmptyBlocksSemaphoreName;
extern std::string BlocsToWriteSemaphoreName;


struct SharedMemory {
	int NextReadBlock = 0;
	int NextWriteBlock = 0;
	BlocksArray blocks = { Block{0}, Block{1}, Block{2} }; // Can we use https://en.cppreference.com/w/cpp/utility/integer_sequence.html
};

class DataTransfer
{
public:
	DataTransfer(const std::string& sharedMemoryOSName, RoleCheck::Role role);
	DataTransfer(const DataTransfer& other) = delete;
	DataTransfer& operator=(const DataTransfer&) = delete; // No assignment allowed.
	DataTransfer(DataTransfer&& other) noexcept;
	~DataTransfer();

	// Call only one time in the beginning of the reading process.
	void InitReading();
	std::string GetName();

	class DataTransferInterface {
	public:
		explicit DataTransferInterface(BlocksArray* blocks, int* counterIn, Semaphore* semaphoreIn, Semaphore* semaphoreOut);
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
	std::string name_;
	SharedMemory* sharedMemory_ = nullptr;
	Semaphore emptyBlocksSemaphore_{ EmptyBlocksSemaphoreName + name_ };
	Semaphore blocksToWriteSemaphore_{ BlocsToWriteSemaphoreName + name_ };
	HANDLE hMapping_;
};
