#pragma once
#include <string>
#include <chrono>
#include <windows.h>

class Semaphore {
public:
    explicit Semaphore(const std::string& name);
    ~Semaphore();
    Semaphore(const Semaphore& other) = delete;
    Semaphore& operator=(const Semaphore&) = delete;
	Semaphore(Semaphore&& other) noexcept;

	enum class WaitResult : uint8_t {
        Signaled,
        WaitAbandoned,
        Timeout,
        WaitFailed
    };

    enum class CreationResult : uint8_t {
        Unknown,
        Created,
        Joined,
    };
    [[nodiscard]] CreationResult Created() const;
    void Signal();
    WaitResult Wait(std::chrono::milliseconds wait);
	WaitResult Wait();
    [[nodiscard]] std::string GetName() const;
private:
    HANDLE hSemaphore_;
    CreationResult creationResult_ = CreationResult::Unknown;
    std::string name_;
};
