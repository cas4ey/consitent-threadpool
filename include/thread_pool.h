#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <stdint.h>
#include <string>
#include <thread>
#include <vector>

#ifdef _MSC_VER
// std::back_inserter is defined in <iterator> for Visual C++ ...
#include <iterator>
#endif

using Task = std::function<void()>;

class ThreadPool final
{
    struct Thread
    {
        std::deque<Task>        jobs;
        std::mutex              mutex;
        std::condition_variable cv;
        std::thread             thread;
        std::atomic<int64_t>    jobsCount {0};

        Thread(std::thread t) noexcept
            : thread(std::move(t))
        {
        }

        Thread(Thread&& t) noexcept
            : jobs(std::move(t.jobs))
            , thread(std::move(t.thread))
            , jobsCount {t.jobsCount.load(std::memory_order_acquire)}
        {
        }
    };

    std::vector<Thread> m_threads;
    std::atomic<size_t> m_lastActiveThread {0};
    std::atomic<char>   m_state {0};

public:
    ThreadPool() : ThreadPool(0)
    {
    }

    explicit ThreadPool(size_t threadsCount)
    {
        if (threadsCount == 0)
            threadsCount = std::thread::hardware_concurrency() + 1;

        m_threads.reserve(threadsCount);

        // N threads for main tasks
        size_t counter = 0;
        std::generate_n(std::back_inserter(m_threads), threadsCount, [this, &counter] {
            return std::thread {&ThreadPool::worker, this, counter++};
        });

        m_lastActiveThread.store(threadsCount, std::memory_order_release);
        set_initialized();
    }

    ~ThreadPool()
    {
        interrupt();
        for (auto& thread : m_threads)
        {
            thread.cv.notify_one();
            thread.thread.join();
        }
    }

    size_t push(const std::string& key, Task task)
    {
        // Consistent key
        return push(consistent_key(key), std::move(task));
    }

    size_t push(const char* key, Task task)
    {
        return push(std::string {key}, std::move(task));
    }

    size_t push(size_t key, Task task)
    {
        // Consistent key
        auto const threadIndex = thread_index(key);
        enqueue(std::move(task), threadIndex);
        m_lastActiveThread.store(threadIndex, std::memory_order_release);
        return threadIndex;
    }

    size_t push(Task task)
    {
        // Round robin
        auto const threadIndex = round_robin_thread_index();
        enqueue(std::move(task), threadIndex);
        m_lastActiveThread.store(threadIndex, std::memory_order_release);
        return threadIndex;
    }

    template <class TFunc, class ... TArgs>
    size_t push(const std::string& key, TFunc func, TArgs&& ... args)
    {
        Task task = std::bind(std::forward<TFunc>(func), std::forward<TArgs>(args)...);
        return push(consistent_key(key), std::move(task));
    }

    template <class TFunc, class ... TArgs>
    size_t push(const char* key, TFunc func, TArgs&& ... args)
    {
        return push(std::string {key}, std::move(func), std::forward<TArgs>(args)...);
    }

    template <class TFunc, class ... TArgs>
    size_t push(size_t key, TFunc func, TArgs&& ... args)
    {
        Task task = std::bind(std::forward<TFunc>(func), std::forward<TArgs>(args)...);
        return push(key, std::move(task));
    }

    template <class TFunc, class ... TArgs>
    size_t push(TFunc func, TArgs&& ... args)
    {
        Task task = std::bind(std::forward<TFunc>(func), std::forward<TArgs>(args)...);
        return push(std::move(task));
    }

private:
    size_t consistent_key(const std::string& key) const noexcept
    {
        return std::hash<std::string>()(key);
    }

    size_t thread_index(size_t key) const noexcept
    {
        return key % m_threads.size();
    }

    size_t round_robin_thread_index() const noexcept
    {
        return thread_index(m_lastActiveThread.load(std::memory_order_acquire));
    }

    char state() const noexcept
    {
        return m_state.load(std::memory_order_release);
    }

    bool is_initialized() const noexcept
    {
        return state() != 0;
    }

    bool is_interrupted() const noexcept
    {
        return state() < 0;
    }

    bool is_alive() const noexcept
    {
        return state() > 0;
    }

    void set_initialized() noexcept
    {
        m_state.store(1, std::memory_order_release);
    }

    void interrupt() noexcept
    {
        m_state.store(-1, std::memory_order_release);
    }

    void enqueue(Task task, size_t index)
    {
        auto& thread = m_threads[index];

        std::unique_lock<std::mutex> lock(thread.mutex);
        thread.jobs.push_back(std::move(task));
        thread.jobsCount.fetch_add(1, std::memory_order_release);
        lock.unlock();

        thread.cv.notify_one();
    }

    void worker(size_t threadIndex)
    {
        while (!is_initialized())
        {
            // Wait until m_threads will be filled
            std::this_thread::yield();
        }

        auto& thread = m_threads[threadIndex];

        while (is_alive())
        {
            // Wait for condition_variable
            std::unique_lock<std::mutex> lock(thread.mutex);
            thread.cv.wait(lock, [this, &thread] {
                return thread.jobsCount.load(std::memory_order_acquire) > 0 || is_interrupted();
            });
            lock.unlock();

            int triesCount = 0; // After 10 tries stop recheduling the thread and wait for condition_variable
            while (is_alive() && ++triesCount < 10)
            {
                if (thread.jobsCount.load(std::memory_order_acquire) < 1)
                {
                    // Reschedule thread execution to wait for a job
                    std::this_thread::yield();
                    continue;
                }

                lock.lock();

                if (thread.jobs.empty())
                {
                    // Reschedule thread execution to wait for a job
                    lock.unlock();
                    std::this_thread::yield();
                    continue;
                }

                // Pop task from the queue
                auto task = std::move(thread.jobs.front());
                thread.jobs.pop_front();
                thread.jobsCount.fetch_sub(1, std::memory_order_release);
                lock.unlock();

                triesCount = 0;
                if (is_interrupted())
                    break;

                // Execute the task
                task();
            }
        }
    }

}; // end of class ThreadPool.
