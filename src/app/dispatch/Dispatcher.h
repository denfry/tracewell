#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace tw::app {

// Пул фоновых воркеров + потокобезопасная доставка результатов на UI-поток.
// notify() вызывается на воркер-потоке после того, как результат положен в
// очередь; продакшен-код передаёт сюда PostMessage(hwnd, WM_APP_DISPATCH, 0, 0).
// onComplete никогда не вызывается иначе как изнутри DrainUiQueue().
class Dispatcher {
public:
    using Notify = std::function<void()>;

    explicit Dispatcher(Notify notify, int workerCount = 2);
    ~Dispatcher();

    Dispatcher(const Dispatcher&) = delete;
    Dispatcher& operator=(const Dispatcher&) = delete;

    template <typename T>
    void Submit(std::function<T()> task, std::function<void(T)> onComplete) {
        EnqueueJob([this, task = std::move(task), onComplete = std::move(onComplete)]() mutable {
            T result = task();
            PostToUiThread([onComplete = std::move(onComplete), result = std::move(result)]() mutable {
                onComplete(std::move(result));
            });
        });
    }

    // Вызывается на UI-потоке (например, из обработчика WM_APP_DISPATCH).
    void DrainUiQueue();

private:
    void EnqueueJob(std::function<void()> job);
    void PostToUiThread(std::function<void()> completion);
    void WorkerLoop();

    Notify notify_;
    std::vector<std::thread> workers_;

    std::mutex jobMutex_;
    std::condition_variable jobCv_;
    std::queue<std::function<void()>> jobs_;
    bool stopping_ = false;

    std::mutex uiMutex_;
    std::queue<std::function<void()>> uiQueue_;
};

}  // namespace tw::app
