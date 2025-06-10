#include "RoleCheck.h"
#include <chrono>
#include <stdexcept>
#include <string>
#include <spdlog/spdlog.h>

using namespace std::string_view_literals;

RoleCheck::RoleCheck() : roleSemaphore_("MultiCopyRoleSemaphore") {
    if (roleSemaphore_.Created() == Semaphore::CreationResult::Created) {
        role_ = Role::Reader;
        roleSemaphore_.Signal();
    } else {
        if (const auto res = roleSemaphore_.Wait(); res == Semaphore::WaitResult::Signaled) {
            role_ = Role::Writer;
        } else if (res == Semaphore::WaitResult::Timeout || res == Semaphore::WaitResult::WaitFailed) {
            role_ = Role::Exit;
        } else {
            throw std::runtime_error("Unexpected semaphore wait result: " + std::to_string(static_cast<int>(res)));
        }
    }
    std::string_view roleHumanReadableName = (role_ == Role::Reader)
	                                             ? "Reader"sv
	                                             : (role_ == Role::Writer)
	                                             ? "Writer"sv
	                                             : "Exit"sv;
    spdlog::info("Role: {}", roleHumanReadableName);
    if (role_ == Role::Exit)
    {
        throw std::runtime_error("Maximum of two processes is allowed at same time. This one was decided to be third or more.");
    }
}

RoleCheck::Role RoleCheck::GetRole() const {
    return role_;
}
