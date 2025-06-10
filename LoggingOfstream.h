#pragma once
#include <fstream>
#include <string>

class LoggingOfstream {
public:
    explicit LoggingOfstream(const std::string& filename, int mode);

    ~LoggingOfstream();

    std::ofstream& get() {
        return file_;
    }

private:
    std::ofstream file_;
    std::string name_;
};
//        LoggingIfstream myFile("example.txt");
//        if (myFile.get().is_open()) {
