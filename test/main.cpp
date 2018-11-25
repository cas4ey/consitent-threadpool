#include <iostream>
#include "thread_pool.h"

class A
{
    std::mutex m_mutex;
    std::atomic<int> m_sum {0};
    std::atomic<int> m_invokeCount {0};

public:

    void f(int i) noexcept
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::cout << "A::a(" << i << ") from thread " << std::this_thread::get_id() << std::endl;
        m_sum.fetch_add(i, std::memory_order_release);
        m_invokeCount.fetch_add(1, std::memory_order_release);
    }

    int sum() const noexcept
    {
        return m_sum.load(std::memory_order_acquire);
    }

    void wait(int count) const noexcept
    {
        while (m_invokeCount.load(std::memory_order_acquire) < count)
        {
            std::this_thread::yield();
        }
    }
};

void test_consistent_key()
{
    std::cout << "CONSISTENT KEY TEST -------------------------------\n";

    ThreadPool pool {2};

    std::mutex m;

    std::atomic<int> invokeCount {0};

    auto task1 = [&m, &invokeCount] {
        std::lock_guard<std::mutex> lock(m);
        std::cout << "task 1 thread: " << std::this_thread::get_id() << std::endl;
        invokeCount.fetch_add(1, std::memory_order_release);
    };

    auto task2 = [&m, &invokeCount] {
        std::lock_guard<std::mutex> lock(m);
        std::cout << "task 2 thread: " << std::this_thread::get_id() << std::endl;
        invokeCount.fetch_add(1, std::memory_order_release);
    };

    auto task3 = [&m, &invokeCount] {
        std::lock_guard<std::mutex> lock(m);
        std::cout << "task 3 thread: " << std::this_thread::get_id() << std::endl;
        invokeCount.fetch_add(1, std::memory_order_release);
    };

    for (int i = 0; i < 3; ++i)
    {
        const auto t1 = pool.push("first",  task1);
        const auto t2 = pool.push("second", task2);
        const auto t3 = pool.push("first",  task3);

        if (t1 != t3 || t1 == t2)
        {
            std::cout << "FAILURE\n";
            return;
        }
    }

    while (invokeCount.load(std::memory_order_acquire) < 3 * 3)
    {
        std::this_thread::yield();
    }

    std::cout << "SUCCESS\n";
}

void test_class_method_with_args()
{
    std::cout << "\nCLASS METHOD WITH ARGS TEST -----------------------\n";

    ThreadPool pool {2};

    A a;

    for (int i = 0; i < 3; ++i)
    {
        pool.push("foo", &A::f, &a, 10);
        pool.push("bar", &A::f, &a, 23);
    }

    a.wait(3 * 2);

    if (a.sum() == 3 * (10 + 23))
    {
        std::cout << "SUCCESS\n";
    }
    else
    {
        std::cout << "FAILURE\n";
    }
}

int main()
{
    test_consistent_key();
    test_class_method_with_args();
    std::getchar();
    return 0;
}

