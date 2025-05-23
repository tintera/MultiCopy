// MultiCopy.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <array>
#include <condition_variable>
#include <queue>

// TODO: Move to a separate file and include platform specific things only in its implementation.
class RoleCheck
{
public:
	enum class Role : uint8_t
	{
		Reader,
		Writer,
		Exit // This process should exit
	};
	/// First creates an OS semaphore and check if this is first or second instance.
	/// Then creates second OS semaphore if this is first (Reader) or second (Writer) instance.
	RoleCheck();
	Role GetRole();
};

class SharedMemory
{
public:
	static constexpr std::streamsize BLOCK_SIZE = 1'000'000;
	static constexpr std::streamsize BLOCK_NUM = 3;
	struct Block {
		char data[BLOCK_SIZE];
		std::streamsize size;
	};

	struct GuardedQueue
	{
		std::queue<Block> queue;
		std::mutex mutex;
		std::condition_variable cv;
	};

	explicit SharedMemory(RoleCheck::Role role);
	std::array<Block, BLOCK_NUM> GetPtr();
	GuardedQueue GetEmptyBlocks();
	GuardedQueue GetBlocksToWrite();
};

int main()
{
	RoleCheck roleCheck; // Lifespan of this object is the whole process runtime.
	RoleCheck::Role role = roleCheck.GetRole();
	if (role == RoleCheck::Role::Exit)
	{
		std::cout << "maximum of two projects exist.\n";
		return 0;
	}

	// Read command line parameters here.

	SharedMemory sharedMemory(role);

	if (role == RoleCheck::Role::Reader)
	{
		// Reader code here.
		std::cout << "Reader process started.\n";
	}
	else if (role == RoleCheck::Role::Writer)
	{
		// Writer code here.
		std::cout << "Writer process started.\n";
	}
	// TODO: Remove following line.
	std::cout << "Hello World!\n";
}
