#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <windows.h>
#include <avrt.h>

#pragma comment(lib, "avrt.lib")

class MMCSSScopedTask {
public:
    explicit MMCSSScopedTask(const wchar_t* taskName) {
        taskIndex_ = 0;
        handle_ = AvSetMmThreadCharacteristicsW(const_cast<LPWSTR>(taskName), &taskIndex_);
    }

    ~MMCSSScopedTask() {
        if (handle_) {
            AvRevertMmThreadCharacteristics(handle_);
        }
    }

    MMCSSScopedTask(const MMCSSScopedTask&) = delete;
    MMCSSScopedTask& operator=(const MMCSSScopedTask&) = delete;

    bool IsValid() const { return handle_ != nullptr; }

private:
    HANDLE handle_{nullptr};
    DWORD taskIndex_{0};
};