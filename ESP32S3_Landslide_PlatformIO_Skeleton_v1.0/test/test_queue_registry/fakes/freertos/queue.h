#pragma once

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <new>
#include <utility>
#include <vector>

#include <freertos/FreeRTOS.h>

struct FakeQueueDefinition {
    explicit FakeQueueDefinition(UBaseType_t queueLength,
                                 UBaseType_t queueItemSize)
        : capacity(queueLength), itemSize(queueItemSize) {}

    UBaseType_t capacity;
    UBaseType_t itemSize;
    std::deque<std::vector<std::uint8_t>> items;
    std::mutex mutex;
    std::condition_variable readable;
    std::condition_variable writable;
};

using QueueHandle_t = FakeQueueDefinition*;

inline QueueHandle_t xQueueCreate(UBaseType_t length,
                                  UBaseType_t itemSize) {
    if (length == 0U || itemSize == 0U) {
        return nullptr;
    }
    return new (std::nothrow) FakeQueueDefinition(length, itemSize);
}

inline void vQueueDelete(QueueHandle_t queue) {
    delete queue;
}

inline BaseType_t xQueueSend(QueueHandle_t queue, const void* item,
                             TickType_t waitTicks) {
    if (queue == nullptr || item == nullptr) {
        return pdFALSE;
    }

    std::unique_lock<std::mutex> lock(queue->mutex);
    const auto hasCapacity = [queue]() {
        return queue->items.size() < queue->capacity;
    };
    bool ready = hasCapacity();
    if (!ready && waitTicks == portMAX_DELAY) {
        queue->writable.wait(lock, hasCapacity);
        ready = true;
    } else if (!ready && waitTicks != 0U) {
        ready = queue->writable.wait_for(
            lock, std::chrono::milliseconds(waitTicks), hasCapacity);
    }
    if (!ready) {
        return pdFALSE;
    }

    std::vector<std::uint8_t> copy(queue->itemSize);
    std::memcpy(copy.data(), item, queue->itemSize);
    queue->items.push_back(std::move(copy));
    lock.unlock();
    queue->readable.notify_one();
    return pdPASS;
}

inline BaseType_t xQueueReceive(QueueHandle_t queue, void* item,
                                TickType_t waitTicks) {
    if (queue == nullptr || item == nullptr) {
        return pdFALSE;
    }

    std::unique_lock<std::mutex> lock(queue->mutex);
    const auto hasItem = [queue]() { return !queue->items.empty(); };
    bool ready = hasItem();
    if (!ready && waitTicks == portMAX_DELAY) {
        queue->readable.wait(lock, hasItem);
        ready = true;
    } else if (!ready && waitTicks != 0U) {
        ready = queue->readable.wait_for(
            lock, std::chrono::milliseconds(waitTicks), hasItem);
    }
    if (!ready) {
        return pdFALSE;
    }

    const std::vector<std::uint8_t>& copy = queue->items.front();
    std::memcpy(item, copy.data(), queue->itemSize);
    queue->items.pop_front();
    lock.unlock();
    queue->writable.notify_one();
    return pdPASS;
}

inline UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue) {
    if (queue == nullptr) {
        return 0U;
    }
    std::lock_guard<std::mutex> lock(queue->mutex);
    return static_cast<UBaseType_t>(queue->items.size());
}
