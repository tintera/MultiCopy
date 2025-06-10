#pragma once
#include <spdlog/spdlog.h>

constexpr std::streamsize BLOCK_SIZE = 1'000'000;
constexpr std::streamsize BLOCK_NUM = 3;

struct Block {
	explicit Block(int id_);
	~Block();
	char data[BLOCK_SIZE] {};
    std::streamsize size = 0;
    int id = -1; // Unique identifier for the block, can be used for debugging or tracking.
	bool error = true; // Set it to false when a block of data is successfully read.
};

inline Block::Block(const int id_) : id(id_)
{
	spdlog::debug("Constructing a Block ");
}

inline Block::~Block()
{
    spdlog::debug("Destructing block {}.", id);
}

using BlocksArray = std::array<Block, BLOCK_NUM>;
