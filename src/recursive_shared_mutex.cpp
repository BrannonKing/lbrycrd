/*
 * Copyright (c) 2017 Toni Neubert, all rights reserved. This file is under the Creative Commons license.
 * Take from https://codereview.stackexchange.com/questions/161735/recursive-shared-mutex, with modifications
 */
#include "recursive_shared_mutex.h"
#include "threadsafety.h"
#include <cassert>

RecursiveSharedMutex::RecursiveSharedMutex() :
        _waitingWriters(0),
        _writersOwnership(1) {
}

void RecursiveSharedMutex::lock() EXCLUSIVE_LOCK_FUNCTION() {
    // Case 1:
    // * Thread has no ownership.
    // * Zero readers, no writer.
    // -> The thread gets exclusive ownership as writer.

    // Case 2:
    // * Thread has no ownership.
    // * Many readers, no writer.
    // -> Gets exclusive ownership as writer and waits until last reader is unlocked.

    // Case 3:
    // * Thread has no ownership.
    // * Zero readers, one writer.
    // -> Gets exclusive ownership as writer after other writer thread is unlocked.

    // Case 4:
    // * Thread has no ownership.
    // * Zero readers, one writer.
    // * Many threads try to get exclusive ownership.
    // -> Various attempts until exclusive ownership as writer has been acquired. The acquisition order is arbitrarily.

    // Case 5:
    // * Thread has exclusive ownership.
    // * Zero readers, one writer.
    // -> Increases threads level of ownership.

    // Case 6:
    // * Thread has sharable ownership.
    // -> Deadlock.

    auto threadId = std::this_thread::get_id();
    {
        // Increase level of ownership if thread has already exclusive ownership.
        std::lock_guard<std::mutex> lock(_mtx);
        if (_writerThreadId == threadId) {
            ++_writersOwnership;
            return;
        }
        assert(_readersOwnership.count(threadId) == 0); // Upgrade to exclusive lock is not available!
        _waitingWriters++;
    }


    for (;;) {
        // Attempt to get exclusive ownership.
        std::thread::id emptyThreadId;
        if (_writerThreadId.compare_exchange_weak(emptyThreadId, threadId)) {
            for (;;) {
                // Wait until no readers exist.
                {
                    std::lock_guard<std::mutex> lock(_mtx);
                    if (_readersOwnership.size() == 0) {
                        // Notify a waiting writer is gone.
                        --_waitingWriters;
                        return;
                    }
                }
                std::this_thread::yield();
            }
        }
        std::this_thread::yield();
    }
}

bool RecursiveSharedMutex::try_lock() EXCLUSIVE_TRYLOCK_FUNCTION() {
    auto threadId = std::this_thread::get_id();
    std::thread::id emptyThreadId;
    std::lock_guard<std::mutex> lock(_mtx);

    if (_writerThreadId == threadId) {
        ++_writersOwnership;
        return true;
    }

    if (_readersOwnership.size() == 0 && _writerThreadId.compare_exchange_weak(emptyThreadId, threadId))
        return true;

    return false;
}

void RecursiveSharedMutex::lock_shared() SHARED_LOCK_FUNCTION() {
    // Case 1:
    // * Thread has/has no ownership.
    // * Zero/Many readers, no writer.
    // -> The thread gets shared ownership as reader.

    // Case 2:
    // * Thread has no ownership.
    // * Zero readers, one writer.
    // -> Waits until writer thread unlocked. The thread gets shared ownership as reader.

    // Case 3:
    // * Thread has sharable ownership.
    // * Many readers, no writer.
    // -> Increases threads level of ownership.

    // Case 4:
    // * Thread has exclusive ownership.
    // * Zero readers, one writer.
    // -> Increases threads level of ownership.

    // Case 5:
    // * Thread has no ownership.
    // * Zero/Many readers, one/no writer.
    // * Many threads try to get exclusive ownership.
    // -> Waits until all exclusive ownership requests are handled. The thread gets shared ownership as reader.

    auto threadId = std::this_thread::get_id();
    {
        // Increase level of ownership if thread has already exclusive ownership.
        std::lock_guard<std::mutex> lock(_mtx);

        // As writer.
        if (_writerThreadId == threadId) {
            ++_writersOwnership;
            return;
        }

        // As an additional reader.
        if (_readersOwnership.count(threadId) != 0) {
            ++_readersOwnership[threadId];
            return;
        }
    }

    for (;;) {
        {
            std::lock_guard <std::mutex> lock(_mtx);
            bool wait_longer = _waitingWriters != 0 || _writerThreadId != std::thread::id();
            // Wait until no writer is waiting or writing.
            if (!wait_longer) {
                // Add new reader ownership.
                _readersOwnership.insert(std::make_pair(threadId, 1));
                return;
            }
        }
        std::this_thread::yield();
    }
}

void RecursiveSharedMutex::unlock() UNLOCK_FUNCTION() {
    // Case 1:
    // * Thread has exclusive ownership.
    // -> If threads level of ownership is 0, releases exclusive ownership otherwise decrements threads level of
    //      ownership.

    // Case 2:
    // * Thread has no/has sharable ownership.
    // -> In debug mode: Assert will terminate program.
    // -> In release mode: Undefined behaviour! Case 1 will occur.

    assert(std::this_thread::get_id() == _writerThreadId);

    std::lock_guard<std::mutex> lock(_mtx);
    {
        // Decrease writer threads level of ownership if not 1.
        if (_writersOwnership != 1) {
            --_writersOwnership;
            return;
        }
    }

    // Reset threads ownership.
    _writerThreadId = std::thread::id();
}

void RecursiveSharedMutex::unlock_shared() UNLOCK_FUNCTION() {
    // Case 1:
    // * Thread has sharable ownership.
    // -> If reader threads level of ownership is 0, releases sharable ownership otherwise decrements reader threads
    //    level of ownership.

    // Case 2:
    // * Thread has exclusive ownership.
    // -> Decrements threads level of ownership.

    // Case 3:
    // * Thread has no ownership.
    // -> In debug mode: Assert will terminate program.
    // -> In release mode: Undefined behaviour!

    // Reduce readers recursive depth.
    // Remove reader from map if depth == 0.
    auto threadId = std::this_thread::get_id();

    std::lock_guard<std::mutex> lock(_mtx);

    // Decrease writer threads level of ownership if not 1.
    if (_writerThreadId == threadId) {
        --_writersOwnership;
        return;
    }

    assert(_readersOwnership.count(threadId) == 1);

    // Decrease threads level of ownership if not 1.
    if (_readersOwnership[threadId] != 1) {
        --_readersOwnership[threadId];
        return;
    }

    // Remove readers ownership.
    _readersOwnership.erase(threadId);
}