#pragma once
#include <fstream>
#include <string>

class LoggingIfstream {
public:
	explicit LoggingIfstream(const std::string& filename, int mode);

    ~LoggingIfstream();

	std::ifstream& get() {
        return file_;
    }

private:
    std::ifstream file_;
    std::string name_;
};
//        LoggingIfstream myFile("example.txt");
//        if (myFile.get().is_open()) {
