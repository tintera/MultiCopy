#pragma once
#include <array>
#include "Semaphore.h"

constexpr std::streamsize BLOCK_SIZE = 1'000'000;
constexpr std::streamsize BLOCK_NUM = 3;

struct Block {
    char data[BLOCK_SIZE];
    std::streamsize size;
};

using BlocksArray = std::array<Block, BLOCK_NUM>;

struct SharedMemory {
    int NextReadBlock = 0;
    int NextWriteBlock = 0;
    BlocksArray blocks;
};

class DataTransfer {
private:
    SharedMemory* sharedMemory_ = nullptr;
    Semaphore emptyBlocksSemaphore_;
    Semaphore blocksToWriteSemaphore_;
    HANDLE hMapping_;
public:
    void InitReading();
    explicit DataTransfer(const char* sharedMemoryOSName);
    DataTransfer(const DataTransfer&) = delete;
    DataTransfer& operator=(const DataTransfer&) = delete;

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
};
