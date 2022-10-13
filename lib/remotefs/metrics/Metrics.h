#ifndef REMOTE_FS_METRICS_H
#define REMOTE_FS_METRICS_H

#include <chrono>
#include <forward_list>
#include <ratio>
#include <string>
#include <variant>

// Nothing in there is thread safe. Create a MetricRegistry per thread.

namespace remotefs {
template <bool disabled>
class MetricRegistry;

template <bool disabled>
std::ostream& operator<<(std::ostream& output, const MetricRegistry<disabled>& registry);

template <bool disabled = false>
class MetricRegistry {
   public:
    class Metric {
       public:
        explicit Metric(std::string&& name);
        virtual ~Metric() = default;
        Metric(const Metric&) = delete;
        Metric& operator=(const Metric&) = delete;
        Metric(const Metric&&) = delete;
        Metric& operator=(const Metric&&) = delete;
        virtual std::ostream& print(std::ostream& output) const = 0;

        template <bool d>
        friend std::ostream& operator<<(std::ostream& output, const typename MetricRegistry<d>::Metric& metric);

       protected:
        std::string _name;
    };

    class Counter final : public Metric {
       public:
        using Metric::Metric;
        Counter(const Counter&) = delete;
        Counter& operator=(const Counter&) = delete;

        std::ostream& print(std::ostream& output) const final;
        void increment(long inc = 1);
        Counter& operator+=(long arg) {
            increment(arg);
            return *this;
        }
        long get() {
            return value;
        }

       private:
        long value = 0;
    };

    template <typename T = long long>
    class Histogram : public Metric {
       public:
        using Type = T;
        using Diff = decltype(std::declval<Type>() - std::declval<Type>());

        using Metric::Metric;
        Histogram(const Histogram&) = delete;
        Histogram& operator=(const Histogram&) = delete;

        std::ostream& print(std::ostream& output) const final;
        Histogram& operator+=(const Diff& diff);

       private:
        struct Stats {
            long long samples = 0;
            Diff mean{};
            Diff min = std::numeric_limits<Diff>::max();
            Diff max = std::numeric_limits<Diff>::min();
            Diff total_mod{};
        } stats;
    };

    class Timer final : public Histogram<std::chrono::high_resolution_clock::time_point> {
       public:
        using Clock = std::chrono::high_resolution_clock;
        using Timestamp = typename Timer::Type;

        static_assert(std::is_same_v<Timestamp, Clock::time_point>);
        using Histogram<std::chrono::high_resolution_clock::time_point>::Histogram;

        class ScopeTracker {
           public:
            explicit ScopeTracker(Timer* hist);
            ~ScopeTracker();

           private:
            Timestamp start;
            Timer* histogram{};
        };

        [[nodiscard]] Timestamp measure_start() const;
        void measure_stop(const Timestamp& start);
        ScopeTracker track_scope();
    };

    Counter& create_counter(std::string&& name);
    Histogram<>& create_histogram(std::string&& name);
    Timer& create_timer(std::string&& name);

    template <bool d>
    friend std::ostream& operator<<(std::ostream& output, const MetricRegistry<d>& registry);

   private:
    // forward_list doesn't invalidate references upon inserting.
    std::forward_list<std::variant<Counter, Histogram<>, Timer>> metrics;
};

}  // namespace remotefs

#endif  // REMOTE_FS_METRICS_H
