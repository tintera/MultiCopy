#include "RoleCheck.h"
#include <chrono>
#include <stdexcept>
#include <string>

RoleCheck::RoleCheck() : roleSemaphore_("MultiCopyRoleSemaphore") {
    if (roleSemaphore_.Created() == Semaphore::CreationResult::Created) {
        role_ = Role::Reader;
        roleSemaphore_.Signal();
    } else {
        if (const auto res = roleSemaphore_.Wait(std::chrono::milliseconds(500)); res == Semaphore::WaitResult::Signaled) {
            role_ = Role::Writer;
        } else if (res == Semaphore::WaitResult::Timeout || res == Semaphore::WaitResult::WaitFailed) {
            role_ = Role::Exit;
        } else {
            throw std::runtime_error("Unexpected semaphore wait result: " + std::to_string(static_cast<int>(res)));
        }
    }
}

RoleCheck::Role RoleCheck::GetRole() const {
    return role_;
}
