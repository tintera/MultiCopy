#include "Utils.h"

#include <stdexcept>

std::string GetLastErrorMessage(const DWORD err) {
    LPWSTR msgBuf = nullptr;
    FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        err,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&msgBuf),
        0, nullptr);

    std::wstring errorMsg;
    if (msgBuf) {
        errorMsg += msgBuf;
        LocalFree(msgBuf);
    }
    const int size_needed = WideCharToMultiByte(CP_UTF8, 0, errorMsg.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string errorMsgUtf8(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, errorMsg.c_str(), -1, &errorMsgUtf8[0], size_needed, nullptr, nullptr);
    return errorMsgUtf8;
}

std::vector<wchar_t> StringToWChar(const std::string& str) {
    const int wlen = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    if (wlen == 0) {
        throw std::runtime_error("StringToWChar: MultiByteToWideChar failed");
    }
    std::vector<wchar_t> wstr(wlen);
    if (MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, wstr.data(), wlen) == 0) {
        throw std::runtime_error("StringToWChar: MultiByteToWideChar failed");
    }
    return wstr;
}
