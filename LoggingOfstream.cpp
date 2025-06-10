#include "LoggingOfstream.h"

#include <spdlog/spdlog.h>


LoggingOfstream::LoggingOfstream(const std::string& filename, const int mode) : file_(filename, mode), name_(filename)
{
	if (!file_.is_open())
	{
		spdlog::debug("Error opening output file: {}", filename);
	}
	spdlog::debug("Opening output file: {}", filename);
}

LoggingOfstream::~LoggingOfstream()
{
	if (file_.is_open()) {
		spdlog::debug("Output file {} closed.", name_);
		file_.close();
	}
}
