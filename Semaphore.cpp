#include "Semaphore.h"
#include <spdlog/spdlog.h>
#include "Utils.h"

constexpr std::streamsize BLOCK_NUM = 3;

Semaphore::Semaphore(const std::string& name) : name_(name) {
    const auto temp = std::wstring(name.begin(), name.end());
    const LPCWSTR sw = temp.c_str();
    hSemaphore_ = CreateSemaphore(
        nullptr, 0, BLOCK_NUM, sw);
    if (hSemaphore_ == nullptr) {
        spdlog::error("Failed to create semaphore {}: {}", name_, GetLastErrorMessage(GetLastError()));
        throw std::runtime_error("Failed to create semaphore: " + std::to_string(GetLastError()));
    }
    creationResult_ = (GetLastError() == ERROR_ALREADY_EXISTS) ? CreationResult::Joined : CreationResult::Created;
    spdlog::debug("Created semaphore: {}, creation result: {}", name_, (creationResult_ == CreationResult::Created) ? "Created" : "Joined");
}

Semaphore::~Semaphore() {
    spdlog::debug("Destroying semaphore: {}", name_);
    if (hSemaphore_ != nullptr) {
        CloseHandle(hSemaphore_);
    }
}

Semaphore::Semaphore(Semaphore&& other) noexcept: hSemaphore_{other.hSemaphore_},
                                                  creationResult_{other.creationResult_},
                                                  name_{std::move(other.name_)}
{
}

Semaphore::CreationResult Semaphore::Created() const {
    return creationResult_;
}

void Semaphore::Signal() {
    spdlog::debug("Signaling semaphore: {}", name_);
    if (!ReleaseSemaphore(hSemaphore_, 1, nullptr)) {
        const DWORD err = GetLastError();
        spdlog::error("Failed to signal semaphore {}: {}", name_, GetLastErrorMessage(err));
        throw std::runtime_error(GetLastErrorMessage(err));
    }
}

Semaphore::WaitResult Semaphore::Wait(const std::chrono::milliseconds timeout) {
    spdlog::debug("Waiting for semaphore: {}", name_);
    switch (WaitForSingleObject(hSemaphore_, static_cast<DWORD>(timeout.count()))) {
    case WAIT_OBJECT_0: return WaitResult::Signaled;
    case WAIT_ABANDONED: return WaitResult::WaitAbandoned;
    case WAIT_TIMEOUT: return WaitResult::Timeout;
    default: return WaitResult::WaitFailed;
    }
}

std::string Semaphore::GetName() const {
    return name_;
}
