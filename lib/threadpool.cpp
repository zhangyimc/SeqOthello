#include "threadpool.h"

// the constructor just launches some amount of workers
inline ThreadPool::ThreadPool(size_t threads, size_t _resource)
    :   resource(_resource), stop(false)
{
    for(size_t i = 0;i<threads;++i)
        workers.emplace_back(
            [this]
            {
                for(;;)
                {
                    std::function<void()> task;
                    int pri;
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        this->condition.wait(lock,
                            [this]{ return this->stop || !this->tasks.empty(); });
                        if(this->stop && this->tasks.empty())
                            return;
                        task = std::move(this->tasks.top().second);
                        pri = this->tasks.top().first;
                        this->tasks.pop();
                    }
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        this->condition_resource.wait(lock,
                            [this]{ return this->resource > 0; });
                        this->resource-=pri;
                    }
                    task();
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        this->resource+=pri;
                    }
                    condition_resource.notify_all();
                }
            }
        );
}

// add new work item to the pool
template<class F, class... Args>
auto ThreadPool::enqueue(int x, F&& f, Args&&... args) 
    -> std::future<typename std::result_of<F(Args...)>::type>
{
    using return_type = typename std::result_of<F(Args...)>::type;

    auto task = std::make_shared< std::packaged_task<return_type()> >(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
    std::future<return_type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(queue_mutex);

        // don't allow enqueueing after stopping the pool
        if(stop)
            throw std::runtime_error("enqueue on stopped ThreadPool");

        tasks.emplace(std::make_pair(x, [task](){ (*task)(); }));
    }
    condition.notify_one();
    return res;
}

// the destructor joins all threads
inline ThreadPool::~ThreadPool()
{
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true;
    }
    condition.notify_all();
    for(std::thread &worker: workers)
        worker.join();
}


