#include <cassert>
#include <coroutine>
#include <exception>
#include <iostream>
#include <utility>

class IntTask {
public:
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    struct promise_type {
        int value{};

        IntTask get_return_object() noexcept
        {
            return IntTask{handle_type::from_promise(*this)};
        }

        std::suspend_always initial_suspend() const noexcept { return {}; }
        std::suspend_always final_suspend() const noexcept { return {}; }
        void return_value(int result) noexcept { value = result; }
        void unhandled_exception() noexcept { std::terminate(); }
    };

    IntTask(IntTask&& other) noexcept
        : coroutine_{std::exchange(other.coroutine_, {})}
    {
    }

    IntTask(const IntTask&) = delete;
    IntTask& operator=(const IntTask&) = delete;

    ~IntTask()
    {
        if (coroutine_) {
            coroutine_.destroy();
        }
    }

    bool resume()
    {
        if (!coroutine_ || coroutine_.done()) {
            return false;
        }
        coroutine_.resume();
        return !coroutine_.done();
    }

    int result() const
    {
        assert(coroutine_ && coroutine_.done());
        return coroutine_.promise().value;
    }

private:
    explicit IntTask(handle_type coroutine) noexcept : coroutine_{coroutine} {}

    handle_type coroutine_{};
};

struct PauseOnce {
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    void await_resume() const noexcept {}
};

IntTask answer(int input)
{
    const int across_suspend = input + 1;
    co_await PauseOnce{};
    co_return across_suspend * 2;
}

int main()
{
    auto task = answer(20);
    assert(task.resume());
    assert(!task.resume());
    assert(task.result() == 42);
    std::cout << "result: " << task.result() << '\n';
}
