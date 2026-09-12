#include <chrono>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <iterator>
#include <optional>
#include <queue>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

// A synchronous, lazy, single-pass generator. It intentionally stores yielded
// values in the coroutine frame so that dereferencing the iterator is safe
// until the iterator is incremented again.
template <typename T>
class Generator {
public:
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    class Iterator {
    public:
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::input_iterator_tag;

        Iterator() = default;
        explicit Iterator(handle_type coroutine) : coroutine_{coroutine} {}

        Iterator& operator++()
        {
            coroutine_.resume();
            Generator::rethrow_if_failed(coroutine_);
            return *this;
        }

        void operator++(int) { ++(*this); }

        const T& operator*() const noexcept
        {
            return *coroutine_.promise().current_value;
        }

        const T* operator->() const noexcept { return &**this; }

        bool operator==(std::default_sentinel_t) const noexcept
        {
            return !coroutine_ || coroutine_.done();
        }

    private:
        handle_type coroutine_{};
    };

    struct promise_type {
        std::optional<T> current_value;
        std::exception_ptr failure;

        Generator get_return_object() noexcept
        {
            return Generator{handle_type::from_promise(*this)};
        }

        std::suspend_always initial_suspend() const noexcept { return {}; }
        std::suspend_always final_suspend() const noexcept { return {}; }

        std::suspend_always yield_value(T value)
        {
            current_value.emplace(std::move(value));
            return {};
        }

        void return_void() const noexcept {}

        void unhandled_exception() noexcept
        {
            failure = std::current_exception();
        }
    };

    Generator() = default;

    Generator(Generator&& other) noexcept
        : coroutine_{std::exchange(other.coroutine_, {})}
    {
    }

    Generator& operator=(Generator&& other) noexcept
    {
        if (this != &other) {
            if (coroutine_) {
                coroutine_.destroy();
            }
            coroutine_ = std::exchange(other.coroutine_, {});
        }
        return *this;
    }

    Generator(const Generator&) = delete;
    Generator& operator=(const Generator&) = delete;

    ~Generator()
    {
        if (coroutine_) {
            coroutine_.destroy();
        }
    }

    Iterator begin()
    {
        if (!coroutine_) {
            return {};
        }
        coroutine_.resume();
        rethrow_if_failed(coroutine_);
        return Iterator{coroutine_};
    }

    std::default_sentinel_t end() const noexcept { return {}; }

private:
    explicit Generator(handle_type coroutine) : coroutine_{coroutine} {}

    static void rethrow_if_failed(handle_type coroutine)
    {
        if (coroutine.done() && coroutine.promise().failure) {
            std::rethrow_exception(coroutine.promise().failure);
        }
    }

    handle_type coroutine_{};
};

// A deliberately small, single-threaded scheduler. Handles in its queues are
// non-owning; the Task/awaiter that created a coroutine owns its frame.
class EventLoop {
public:
    using clock = std::chrono::steady_clock;

    class ScheduleAwaiter {
    public:
        explicit ScheduleAwaiter(EventLoop& loop) : loop_{loop} {}

        bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> coroutine) const
        {
            loop_.post(coroutine);
        }
        void await_resume() const noexcept {}

    private:
        EventLoop& loop_;
    };

    class SleepAwaiter {
    public:
        SleepAwaiter(EventLoop& loop, std::chrono::milliseconds delay)
            : loop_{loop}, delay_{delay}
        {
        }

        bool await_ready() const noexcept { return delay_ <= 0ms; }
        void await_suspend(std::coroutine_handle<> coroutine) const
        {
            loop_.post_after(delay_, coroutine);
        }
        void await_resume() const noexcept {}

    private:
        EventLoop& loop_;
        std::chrono::milliseconds delay_;
    };

    ScheduleAwaiter schedule() noexcept { return ScheduleAwaiter{*this}; }

    SleepAwaiter sleep_for(std::chrono::milliseconds delay) noexcept
    {
        return SleepAwaiter{*this, delay};
    }

    void post(std::coroutine_handle<> coroutine)
    {
        ready_.push(coroutine);
    }

    void post_after(
        std::chrono::milliseconds delay,
        std::coroutine_handle<> coroutine)
    {
        timers_.push(Timer{clock::now() + delay, sequence_++, coroutine});
    }

    void run()
    {
        while (!ready_.empty() || !timers_.empty()) {
            if (!ready_.empty()) {
                auto coroutine = ready_.front();
                ready_.pop();
                if (!coroutine.done()) {
                    coroutine.resume();
                }
                continue;
            }

            const auto wake_at = timers_.top().wake_at;
            if (wake_at > clock::now()) {
                std::this_thread::sleep_until(wake_at);
            }

            const auto now = clock::now();
            while (!timers_.empty() && timers_.top().wake_at <= now) {
                ready_.push(timers_.top().coroutine);
                timers_.pop();
            }
        }
    }

private:
    struct Timer {
        clock::time_point wake_at;
        std::size_t sequence;
        std::coroutine_handle<> coroutine;
    };

    struct LaterFirst {
        bool operator()(const Timer& left, const Timer& right) const noexcept
        {
            if (left.wake_at != right.wake_at) {
                return left.wake_at > right.wake_at;
            }
            return left.sequence > right.sequence;
        }
    };

    std::queue<std::coroutine_handle<>> ready_;
    std::priority_queue<Timer, std::vector<Timer>, LaterFirst> timers_;
    std::size_t sequence_{};
};

// A lazy, single-consumer Task<T>. This teaching implementation supports a
// non-void result, exception propagation and nested co_await composition.
template <typename T>
class Task {
public:
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    struct promise_type {
        std::optional<T> value;
        std::exception_ptr failure;
        std::coroutine_handle<> continuation = std::noop_coroutine();

        Task get_return_object() noexcept
        {
            return Task{handle_type::from_promise(*this)};
        }

        std::suspend_always initial_suspend() const noexcept { return {}; }

        struct FinalAwaiter {
            bool await_ready() const noexcept { return false; }

            std::coroutine_handle<> await_suspend(
                handle_type completed) const noexcept
            {
                return completed.promise().continuation;
            }

            void await_resume() const noexcept {}
        };

        FinalAwaiter final_suspend() const noexcept { return {}; }

        template <typename U>
        void return_value(U&& result)
        {
            value.emplace(std::forward<U>(result));
        }

        void unhandled_exception() noexcept
        {
            failure = std::current_exception();
        }
    };

    class Awaiter {
    public:
        explicit Awaiter(handle_type coroutine) : coroutine_{coroutine} {}

        Awaiter(Awaiter&& other) noexcept
            : coroutine_{std::exchange(other.coroutine_, {})}
        {
        }

        Awaiter(const Awaiter&) = delete;
        Awaiter& operator=(const Awaiter&) = delete;

        ~Awaiter()
        {
            if (coroutine_) {
                coroutine_.destroy();
            }
        }

        bool await_ready() const noexcept
        {
            return !coroutine_ || coroutine_.done();
        }

        std::coroutine_handle<> await_suspend(
            std::coroutine_handle<> awaiting) const noexcept
        {
            coroutine_.promise().continuation = awaiting;
            return coroutine_;
        }

        T await_resume()
        {
            if (!coroutine_) {
                throw std::logic_error{"awaited an empty task"};
            }

            auto coroutine = std::exchange(coroutine_, {});
            struct FrameGuard {
                handle_type coroutine;
                ~FrameGuard() { coroutine.destroy(); }
            } guard{coroutine};

            if (coroutine.promise().failure) {
                std::rethrow_exception(coroutine.promise().failure);
            }
            return std::move(*coroutine.promise().value);
        }

    private:
        handle_type coroutine_{};
    };

    Task() = default;

    Task(Task&& other) noexcept
        : coroutine_{std::exchange(other.coroutine_, {})},
          started_{std::exchange(other.started_, false)}
    {
    }

    Task& operator=(Task&& other) noexcept
    {
        if (this != &other) {
            destroy_frame();
            coroutine_ = std::exchange(other.coroutine_, {});
            started_ = std::exchange(other.started_, false);
        }
        return *this;
    }

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    ~Task() { destroy_frame(); }

    Awaiter operator co_await() &&
    {
        if (started_) {
            throw std::logic_error{"a started root task cannot be co_awaited"};
        }
        return Awaiter{std::exchange(coroutine_, {})};
    }

    void start_on(EventLoop& loop)
    {
        if (!coroutine_ || started_) {
            throw std::logic_error{"task can only be started once"};
        }
        started_ = true;
        loop.post(coroutine_);
    }

    T result()
    {
        if (!coroutine_ || !coroutine_.done()) {
            throw std::logic_error{"task result is not ready"};
        }
        if (coroutine_.promise().failure) {
            std::rethrow_exception(coroutine_.promise().failure);
        }
        return std::move(*coroutine_.promise().value);
    }

private:
    explicit Task(handle_type coroutine) : coroutine_{coroutine} {}

    void destroy_frame() noexcept
    {
        if (!coroutine_) {
            return;
        }

        // The scheduler stores non-owning handles. Destroying a started but
        // incomplete root would leave a dangling handle in its queues.
        if (started_ && !coroutine_.done()) {
            std::terminate();
        }
        coroutine_.destroy();
        coroutine_ = {};
    }

    handle_type coroutine_{};
    bool started_{};
};

Generator<std::uint64_t> fibonacci(std::size_t count)
{
    std::uint64_t current = 0;
    std::uint64_t next = 1;

    for (std::size_t index = 0; index < count; ++index) {
        co_yield current;
        const auto following = current + next;
        current = next;
        next = following;
    }
}

Task<int> delayed_square(
    EventLoop& loop,
    int value,
    std::chrono::milliseconds delay)
{
    co_await loop.sleep_for(delay);
    if (value < 0) {
        throw std::invalid_argument{"value must not be negative"};
    }
    co_return value * value;
}

Task<int> sum_of_squares(EventLoop& loop)
{
    const int first = co_await delayed_square(loop, 3, 20ms);

    // Explicitly yield to the event loop once. schedule() is an awaitable,
    // not a thread switch: this demo remains single-threaded.
    co_await loop.schedule();

    const int second = co_await delayed_square(loop, 4, 10ms);
    co_return first + second;
}

int main()
{
    std::cout << "fibonacci:";
    std::vector<std::uint64_t> generated;
    for (const auto value : fibonacci(8)) {
        generated.push_back(value);
        std::cout << ' ' << value;
    }
    std::cout << '\n';
    if (generated != std::vector<std::uint64_t>{0, 1, 1, 2, 3, 5, 8, 13}) {
        throw std::runtime_error{"generator produced an unexpected sequence"};
    }

    EventLoop loop;
    auto pipeline = sum_of_squares(loop);
    auto independent = delayed_square(loop, 5, 15ms);

    pipeline.start_on(loop);
    independent.start_on(loop);
    loop.run();

    const int pipeline_result = pipeline.result();
    const int independent_result = independent.result();
    if (pipeline_result != 25 || independent_result != 25) {
        throw std::runtime_error{"task produced an unexpected result"};
    }
    std::cout << "pipeline result: " << pipeline_result << '\n';
    std::cout << "independent result: " << independent_result << '\n';

    auto failure = delayed_square(loop, -1, 0ms);
    failure.start_on(loop);
    loop.run();

    bool caught_expected_error = false;
    try {
        static_cast<void>(failure.result());
    } catch (const std::invalid_argument& error) {
        caught_expected_error = true;
        std::cout << "task error: " << error.what() << '\n';
    }
    if (!caught_expected_error) {
        throw std::runtime_error{"task did not propagate its exception"};
    }
}
