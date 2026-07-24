#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

#include <catch2/catch_test_macros.hpp>

#include "app/dispatch/Dispatcher.h"

using tw::app::Dispatcher;

TEST_CASE("Dispatcher delivers a result only after DrainUiQueue is called") {
    std::mutex m;
    std::condition_variable cv;
    bool notified = false;
    Dispatcher dispatcher(
        [&] {
            std::lock_guard<std::mutex> lock(m);
            notified = true;
            cv.notify_one();
        },
        2);

    int received = -1;
    dispatcher.Submit<int>([] { return 42; }, [&](int v) { received = v; });

    std::unique_lock<std::mutex> lock(m);
    REQUIRE(cv.wait_for(lock, std::chrono::seconds(2), [&] { return notified; }));
    lock.unlock();

    REQUIRE(received == -1);  // ещё не применено — ждёт вычитывания на "UI-потоке"
    dispatcher.DrainUiQueue();
    REQUIRE(received == 42);
}

TEST_CASE("Dispatcher processes many tasks with a bounded worker pool") {
    std::mutex m;
    std::condition_variable cv;
    int notifyCount = 0;
    constexpr int kTasks = 20;
    Dispatcher dispatcher(
        [&] {
            std::lock_guard<std::mutex> lock(m);
            ++notifyCount;
            cv.notify_one();
        },
        2);

    std::atomic<int> sum{0};
    for (int i = 0; i < kTasks; ++i) {
        dispatcher.Submit<int>([i] { return i; }, [&](int v) { sum.fetch_add(v); });
    }

    std::unique_lock<std::mutex> lock(m);
    REQUIRE(cv.wait_for(lock, std::chrono::seconds(5), [&] { return notifyCount == kTasks; }));
    lock.unlock();

    dispatcher.DrainUiQueue();
    REQUIRE(sum.load() == (kTasks * (kTasks - 1)) / 2);
}
