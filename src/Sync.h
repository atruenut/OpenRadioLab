// SPDX-License-Identifier: GPL-2.0-only
// OpenRadio Lab - Copyright (C) 2026 The OpenRadio Lab authors.
// This program is free software; you can redistribute it and/or modify it under the terms of the
// GNU General Public License version 2 as published by the Free Software Foundation. See LICENSE.

// Minimal threading helpers.
//
// Deliberately avoids <thread>/<mutex>: MinGW-w64 builds that use the "win32"
// thread model (common on Windows toolchains) do not provide std::thread or
// std::mutex. Raw Win32 primitives work with every MinGW flavour; a pthread
// fallback keeps the portable core testable on Linux.
#pragma once

#include <functional>
#include <cstdint>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <pthread.h>
#  include <time.h>
#  include <unistd.h>
#endif

namespace orl {

class Mutex {
public:
#ifdef _WIN32
    Mutex()  { InitializeCriticalSection(&m_); }
    ~Mutex() { DeleteCriticalSection(&m_); }
    void lock()   { EnterCriticalSection(&m_); }
    void unlock() { LeaveCriticalSection(&m_); }
private:
    CRITICAL_SECTION m_;
#else
    Mutex()  { pthread_mutex_init(&m_, nullptr); }
    ~Mutex() { pthread_mutex_destroy(&m_); }
    void lock()   { pthread_mutex_lock(&m_); }
    void unlock() { pthread_mutex_unlock(&m_); }
private:
    pthread_mutex_t m_;
#endif
public:
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;
};

class Lock {
public:
    explicit Lock(Mutex& m) : m_(m) { m_.lock(); }
    ~Lock() { m_.unlock(); }
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
private:
    Mutex& m_;
};

class Thread {
public:
    Thread() = default;
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;
    ~Thread() { join(); }

    void start(std::function<void()> fn) {
        join();
        auto* p = new std::function<void()>(std::move(fn));
#ifdef _WIN32
        h_ = CreateThread(nullptr, 0, &Thread::tramp, p, 0, nullptr);
        if (!h_) delete p;
#else
        if (pthread_create(&t_, nullptr, &Thread::tramp, p) == 0) started_ = true;
        else delete p;
#endif
    }

    void join() {
#ifdef _WIN32
        if (h_) { WaitForSingleObject(h_, INFINITE); CloseHandle(h_); h_ = nullptr; }
#else
        if (started_) { pthread_join(t_, nullptr); started_ = false; }
#endif
    }

private:
#ifdef _WIN32
    static DWORD WINAPI tramp(LPVOID p) {
        auto* f = static_cast<std::function<void()>*>(p);
        (*f)();
        delete f;
        return 0;
    }
    HANDLE h_ = nullptr;
#else
    static void* tramp(void* p) {
        auto* f = static_cast<std::function<void()>*>(p);
        (*f)();
        delete f;
        return nullptr;
    }
    pthread_t t_{};
    bool started_ = false;
#endif
};

// Auto-reset event: set() wakes one waiter; wait() returns true if it was signalled.
class Event {
public:
#ifdef _WIN32
    Event()  { h_ = CreateEventA(nullptr, FALSE, FALSE, nullptr); }
    ~Event() { CloseHandle(h_); }
    void set() { SetEvent(h_); }
    bool wait(double seconds) { return WaitForSingleObject(h_, (DWORD)(seconds * 1000.0)) == WAIT_OBJECT_0; }
private:
    HANDLE h_;
#else
    Event()  { pthread_mutex_init(&m_, nullptr); pthread_cond_init(&c_, nullptr); }
    ~Event() { pthread_cond_destroy(&c_); pthread_mutex_destroy(&m_); }
    void set() { pthread_mutex_lock(&m_); sig_ = true; pthread_cond_signal(&c_); pthread_mutex_unlock(&m_); }
    bool wait(double seconds) {
        timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
        const long long ns = (long long)ts.tv_nsec + (long long)(seconds * 1e9);
        ts.tv_sec += ns / 1000000000LL; ts.tv_nsec = ns % 1000000000LL;
        pthread_mutex_lock(&m_);
        while (!sig_) if (pthread_cond_timedwait(&c_, &m_, &ts) != 0) break;
        const bool got = sig_; sig_ = false;
        pthread_mutex_unlock(&m_);
        return got;
    }
private:
    pthread_mutex_t m_; pthread_cond_t c_; bool sig_ = false;
#endif
public:
    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;
};

inline double nowSeconds() {
#ifdef _WIN32
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER c; QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)freq.QuadPart;
#else
    timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#endif
}

inline void sleepSeconds(double s) {
    if (s <= 0) return;
#ifdef _WIN32
    DWORD ms = (DWORD)(s * 1000.0);
    Sleep(ms ? ms : 1);
#else
    usleep((useconds_t)(s * 1e6));
#endif
}

} // namespace orl
