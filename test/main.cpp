#include "../thread_pool.h"
#include <iostream>

int main()
{
    ThreadPool pool {2};

    std::mutex m;

    auto task1 = [&m] {
        std::lock_guard<std::mutex> lock(m);
        std::cout << "task 1 thread: " << std::this_thread::get_id() << std::endl;
    };

    auto task2 = [&m] {
        std::lock_guard<std::mutex> lock(m);
        std::cout << "task 2 thread: " << std::this_thread::get_id() << std::endl;
    };

    for (int i = 0; i < 5; ++i)
    {
        pool.execute("a", task1);
        pool.execute("b", task2);
    }

    std::getchar();

    return 0;
}

