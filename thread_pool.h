/**
*     Copyright (C) 2022 Mason Soroka-Gill
* 
*     This program is free software: you can redistribute it and/or modify
*     it under the terms of the GNU General Public License as published by
*     the Free Software Foundation, either version 3 of the License, or
*     (at your option) any later version.
* 
*     This program is distributed in the hope that it will be useful,
*     but WITHOUT ANY WARRANTY; without even the implied warranty of
*     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*     GNU General Public License for more details.
* 
*     You should have received a copy of the GNU General Public License
*     along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <condition_variable>
#include <functional>
#include <iterator>
#include <thread>
#include <future>
#include <vector>
#include <atomic>
#include <queue>
#include <mutex>

class ThreadPool {
    private:
        std::atomic<bool> thread_terminate;
        unsigned int threadcount;
        std::vector<std::thread> threads;
        std::queue<std::function<void()>> task_queue;
        std::mutex thread_queue_mtx;
        std::condition_variable thread_conditional;

        void init_threadpool()
        {
            this->threads.reserve(this->threadcount);
            for (size_t i = 0; i < this->threadcount; ++i)
                this->threads.push_back(std::thread(worker, this));
            std::cout << this->threadcount << " threads created for dispatch\n";
        }

        static void worker(ThreadPool* threadpool)
        {
            for (;;) {
                if (threadpool->thread_terminate) return;

                std::unique_lock<std::mutex> thread_queue_lock(threadpool->thread_queue_mtx);

                if (threadpool->task_queue.empty()) {
                    threadpool->thread_conditional.wait(thread_queue_lock, [&]() {
                            return threadpool->thread_terminate || !threadpool->task_queue.empty();
                    });
                }

                if (threadpool->thread_terminate) return;

                auto work = std::move(threadpool->task_queue.front());
                threadpool->task_queue.pop();

                thread_queue_lock.unlock();

                work();
            }
        }

    public:
        ThreadPool()
            : thread_terminate(false), threadcount(std::thread::hardware_concurrency())
        {
            this->init_threadpool();
        }

        ThreadPool(unsigned int threadcount)
            : thread_terminate(false), threadcount(threadcount)
        {
            if (this->threadcount == 0)
                this->threadcount = std::thread::hardware_concurrency();

            this->init_threadpool();
        }

        ~ThreadPool()
        {
            std::unique_lock<std::mutex> thread_queue_lock(this->thread_queue_mtx);
                for (;!this->task_queue.empty(); this->task_queue.pop())
                    ;
            thread_queue_lock.unlock();

            this->thread_terminate = true;
            this->thread_conditional.notify_all();

            for (std::thread& t : this->threads)
                t.join();
        }

        bool empty()
        {
            std::unique_lock<std::mutex> thread_queue_lock(this->thread_queue_mtx);
            bool isEmpty = this->task_queue.empty();
            thread_queue_lock.unlock();
            return isEmpty;
        }

        template<typename Function, typename... Args>
        void dispatch(Function&& func, Args&&... args)
        {
            auto task = std::make_shared<std::packaged_task<typename std::result_of<Function(Args...)>::type()>>(
                        std::bind(std::forward<Function>(func), std::forward<Args>(args)...));

            std::unique_lock<std::mutex> thread_queue_lock(this->thread_queue_mtx);
                this->task_queue.emplace([task]() { (*task)(); });
            thread_queue_lock.unlock();

            this->thread_conditional.notify_one();
        }
};

#endif
