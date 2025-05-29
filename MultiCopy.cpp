// MultiCopy.cpp : This file contains the 'main' function. Program execution begins and ends there.
#include <iostream>
#include <array>
#include <condition_variable>
#include <chrono>

class SharedMemory;

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
	Semaphore();
	void Signal();
	WaitResult Wait(std::chrono::duration<std::chrono::milliseconds> timeout);
};

// TODO: Move to a separate file and include platform specific things only in its implementation.
class RoleCheck
{
public:
	friend SharedMemory;
	enum class Role : uint8_t
	{
		Reader,
		Writer,
		Exit // This process should exit
	};
	RoleCheck();
	std::pair<Role, SharedMemory> GetRole();
private:
	Semaphore GetSemaphore();
};

static constexpr std::streamsize BLOCK_SIZE = 1'000'000;
static constexpr std::streamsize BLOCK_NUM = 3;

struct Block {
	char data[BLOCK_SIZE];
	std::streamsize size;
};

class SharedMemory
{
public:
	explicit SharedMemory(RoleCheck::Role role);

	class DataTransfer {
	public:
		Block& GetBlock();
		void PutBlock(Block& );
	};

	DataTransfer GetWriterInterface();
	DataTransfer GetReaderInterface();

private:
	//struct GuardedQueue
	//{
	//	std::queue<Block> queue;
	//	std::mutex mutex;
	//	std::condition_variable cv;
	//};
	//
	//GuardedQueue GetEmptyBlocks();
	//GuardedQueue GetBlocksToWrite();

	//GuardedQueue emptyBlocks_;
	//GuardedQueue blocksToWrite_;
	//std::array<Block, BLOCK_NUM> blocks_; // used to fill EmptyBlocks at the program beginning.
};

int main()
{
	RoleCheck roleCheck; // Lifespan of this object is the whole process runtime.
	auto [role, sharedMemory] = roleCheck.GetRole();
	if (role == RoleCheck::Role::Exit)
	{
		std::cout << "Maximum of two processes is allowed at same time. This one was decided to be third or more.\n";
		return 0;
	}
	// Read command-line parameters here.

	if (role == RoleCheck::Role::Reader)
	{
		// Reader code here.
		std::cout << "Reader process started.\n";
		// open input file
		auto rdMem = sharedMemory.GetReaderInterface();
		while (true /*read not finished*/)
		{
			auto& block = rdMem.GetBlock();
			// Read from file into emptyBlock.
			rdMem.PutBlock(block);
		}
	}
	else if (role == RoleCheck::Role::Writer)
	{
		// Writer code here.
		std::cout << "Writer process started.\n";
		auto wrMem = sharedMemory.GetWriterInterface();
		bool finished = false;
		do // last block has full size
		{
			auto& block = wrMem.GetBlock();
			// Write to file from block.
			wrMem.PutBlock(block);
			finished = (block.size < BLOCK_SIZE);
		} while (!finished /*write not finished*/);
	}
}
