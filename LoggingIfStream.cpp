#include "LoggingIfStream.h"

#include <spdlog/spdlog.h>


LoggingIfstream::LoggingIfstream(const std::string& filename, const int mode): file_(filename, mode), name_(filename)
{
	if (!file_.is_open())
	{
		spdlog::debug("Error opening input file: {}", filename);
	}
	spdlog::debug("Opening input file: {}", filename);
}

LoggingIfstream::~LoggingIfstream()
{
	if (file_.is_open()) {
		spdlog::debug("Input file {} closed.", name_);
		file_.close();
	}
}
