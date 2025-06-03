#pragma once
#include "Semaphore.h"

class DataTransfer;

class RoleCheck {
public:
    friend class DataTransfer;
    enum class Role : uint8_t {
        Reader,
        Writer,
        Exit
    };
    RoleCheck();
    [[nodiscard]] Role GetRole() const;
private:
    Semaphore roleSemaphore_;
    Role role_;
};
