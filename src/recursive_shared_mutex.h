/*
 * Copyright (c) 2017 Toni Neubert, all rights reserved. This file is under the Creative Commons license.
 * Take from https://codereview.stackexchange.com/questions/161735/recursive-shared-mutex
 */
#pragma once

#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_map>

class RecursiveSharedMutex {
public:
    /**
     * @brief Constructs the mutex.
     */
    RecursiveSharedMutex();

    /**
     * @brief Locks the mutex for exclusive write access for this thread.
     *              Blocks execution as long as write access is not available:
     *              * other thread has write access
     *              * other threads try to get write access
     *              * other threads have read access
     *
     *              A thread may call lock repeatedly.
     *              Ownership will only be released after the thread makes a matching number of calls to unlock.
     */
    void lock();
    bool try_lock();

    /**
     * @brief Locks the mutex for sharable read access.
     *              Blocks execution as long as read access is not available:
     *              * other thread has write access
     *              * other threads try to get write access
     *
     *              A thread may call lock repeatedly.
     *              Ownership will only be released after the thread makes a matching number of calls to unlock_shared.
     */
    void lock_shared();

    /**
     * @brief Unlocks the mutex for this thread if its level of ownership is 1. Otherwise reduces the level of ownership
     *              by 1.
     */
    void unlock();

    /**
     * @brief Unlocks the mutex for this thread if its level of ownership is 1. Otherwise reduces the level of ownership
     *              by 1.
     */
    void unlock_shared();

private:
    /// Protects data access of owners.
    std::mutex _mtx;
    /// Number of threads waiting for exclusive write access.
    uint32_t _waitingWriters;
    /// Thread id of writer.
    std::atomic<std::thread::id> _writerThreadId;
    /// Level of ownership of writer thread.
    uint32_t _writersOwnership;
    /// Level of ownership of reader threads.
    std::unordered_map<std::thread::id, uint32_t> _readersOwnership;
};