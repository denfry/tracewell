#include "Dispatcher.h"

namespace tw::app {

Dispatcher::Dispatcher(Notify notify, int workerCount) : notify_(std::move(notify)) {
    workers_.reserve(workerCount);
    for (int i = 0; i < workerCount; ++i) {
        workers_.emplace_back([this] { WorkerLoop(); });
    }
}

Dispatcher::~Dispatcher() {
    {
        std::lock_guard<std::mutex> lock(jobMutex_);
        stopping_ = true;
    }
    jobCv_.notify_all();
    for (auto& worker : workers_) {
        worker.join();
    }
}

void Dispatcher::EnqueueJob(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> lock(jobMutex_);
        jobs_.push(std::move(job));
    }
    jobCv_.notify_one();
}

void Dispatcher::PostToUiThread(std::function<void()> completion) {
    {
        std::lock_guard<std::mutex> lock(uiMutex_);
        uiQueue_.push(std::move(completion));
    }
    notify_();
}

void Dispatcher::WorkerLoop() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(jobMutex_);
            jobCv_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
            if (stopping_ && jobs_.empty()) {
                return;
            }
            job = std::move(jobs_.front());
            jobs_.pop();
        }
        job();
    }
}

void Dispatcher::DrainUiQueue() {
    std::queue<std::function<void()>> local;
    {
        std::lock_guard<std::mutex> lock(uiMutex_);
        std::swap(local, uiQueue_);
    }
    while (!local.empty()) {
        local.front()();
        local.pop();
    }
}

}  // namespace tw::app
