#pragma once

#include <functional>
#include <mutex>
#include <unordered_map>

namespace tw::app {

// Простая observable-обёртка: явная подписка вместо XAML {Binding}.
// Set() уведомляет подписчиков синхронно, на потоке вызывающего.
template <typename T>
class Property {
public:
    using Callback = std::function<void(const T&)>;

    explicit Property(T initial = T{}) : value_(std::move(initial)) {}

    T Get() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return value_;
    }

    void Set(T value) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            value_ = std::move(value);
        }
        NotifySubscribers();
    }

    // Возвращает id подписки для последующей отписки через Unsubscribe.
    int Subscribe(Callback callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        int id = nextId_++;
        subscribers_.emplace(id, std::move(callback));
        return id;
    }

    void Unsubscribe(int id) {
        std::lock_guard<std::mutex> lock(mutex_);
        subscribers_.erase(id);
    }

private:
    void NotifySubscribers() {
        std::unordered_map<int, Callback> snapshot;
        T valueCopy;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            snapshot = subscribers_;
            valueCopy = value_;
        }
        for (auto& [id, callback] : snapshot) {
            callback(valueCopy);
        }
    }

    mutable std::mutex mutex_;
    T value_;
    std::unordered_map<int, Callback> subscribers_;
    int nextId_ = 0;
};

}  // namespace tw::app
