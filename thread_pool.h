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
        {
            jobsCount = t.jobsCount.load();
        }
    };

    std::vector<Thread> m_threads;
    std::atomic<size_t> m_lastActiveThread {0};
    std::atomic<char>   m_state {0};

public:
    ThreadPool() : ThreadPool(0)
    {
    }

    ThreadPool(size_t threadsCount)
    {
        if (!threadsCount)
            threadsCount = std::thread::hardware_concurrency() + 1;

        m_threads.reserve(threadsCount);

        // N threads for main tasks
        size_t counter = 0;
        std::generate_n(std::back_inserter(m_threads), threadsCount, [this, &counter] {
            return std::thread {&ThreadPool::worker, this, counter++};
        });

        m_lastActiveThread = threadsCount;
        setInitialized();
    }

    ~ThreadPool()
    {
        interrupt();
        for (auto& thread : m_threads)
        {
            thread.thread.join();
        }
    }

    void execute(const std::string& key, Task task)
    {
        // Consistent key
        execute(std::hash<std::string>()(key), std::move(task));
    }

    void execute(size_t key, Task task)
    {
        // Consistent key
        auto const threadIndex = key % m_threads.size();
        addJob(std::move(task), threadIndex);
        m_lastActiveThread = threadIndex;
    }

    void execute(Task task)
    {
        // Round robin
        auto const threadIndex = m_lastActiveThread % m_threads.size();
        addJob(std::move(task), threadIndex);
        m_lastActiveThread = threadIndex;
    }

private:
    bool isInitialized() const noexcept
    {
        return m_state != 0;
    }

    bool isInterrupted() const noexcept
    {
        return m_state < 0;
    }

    bool isAlive() const noexcept
    {
        return m_state > 0;
    }

    void setInitialized() noexcept
    {
        m_state = 1;
    }

    void interrupt() noexcept
    {
        m_state = -1;
    }

    void addJob(Task task, size_t index)
    {
        auto& thread = m_threads[index];

        std::unique_lock<std::mutex> lock(thread.mutex);
        thread.jobs.push_back(std::move(task));
        ++thread.jobsCount;
        lock.unlock();

        thread.cv.notify_one();
    }

    void worker(size_t threadIndex)
    {
        while (!isInitialized())
        {
            // Wait until m_threads will be filled
            std::this_thread::yield();
        }

        auto& thread = m_threads[threadIndex];

        while (isAlive())
        {
            // Wait for condition_variable
            std::unique_lock<std::mutex> lock(thread.mutex);
            thread.cv.wait(lock, [this, &thread] {
                return thread.jobsCount > 0 || isInterrupted();
            });
            lock.unlock();

            int triesCount = 0; // After 3 tries stop recheduling the thread and wait for condition_variable
            while (isAlive() && ++triesCount < 4)
            {
                if (thread.jobsCount < 1)
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
                --thread.jobsCount;
                lock.unlock();

                triesCount = 0;
                if (isInterrupted())
                    break;

                // Execute the task
                task();
            }
        }
    }

}; // end of class ThreadPool.
