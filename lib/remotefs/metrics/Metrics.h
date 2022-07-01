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
        virtual std::ostream& print(std::ostream& output) const = 0;

        template <bool d>
        friend std::ostream& operator<<(std::ostream& output, const typename MetricRegistry<d>::Metric& metric);

       protected:
        std::string _name;
    };

    class Counter : public Metric {
       public:
        using Metric::Metric;
        Counter(const Counter&) = delete;
        Counter& operator=(const Counter&) = delete;

        std::ostream& print(std::ostream& output) const final;
        void increment(long inc = 1);

       private:
        long value = 0;
    };

    class Histogram : public Metric {
       public:
        using Clock = std::chrono::high_resolution_clock;
        using Duration = std::chrono::duration<int64_t, std::nano>;
        using Timestamp = std::chrono::time_point<Clock>;

        class ScopeTracker {
           public:
            explicit ScopeTracker(Histogram* hist);
            ~ScopeTracker();

           private:
            Timestamp start;
            Histogram* histogram{};
        };

        using Metric::Metric;
        Histogram(const Histogram&) = delete;
        Histogram& operator=(const Histogram&) = delete;

        std::ostream& print(std::ostream& output) const final;
        [[nodiscard]] Timestamp measure_start() const;
        void measure_stop(const Timestamp& start);
        ScopeTracker track_scope();

       private:
        struct Stats {
            long long samples = 0;
            Duration mean{};
            Duration min = Duration::max();
            Duration max = Duration::min();
            Duration total_mod = Duration::zero();
        } stats;
    };

    Counter& create_counter(std::string&& name);
    Histogram& create_histogram(std::string&& name);

    template <bool d>
    friend std::ostream& operator<<(std::ostream& output, const MetricRegistry<d>& registry);

   private:
    // forward_list doesn't invalidate references upon inserting.
    std::forward_list<std::variant<Counter, Histogram>> metrics;
};

}  // namespace remotefs

#endif  // REMOTE_FS_METRICS_H
