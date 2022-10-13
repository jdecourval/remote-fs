#include "remotefs/metrics/Metrics.h"

namespace remotefs {

template <>
MetricRegistry<true>::Metric::Metric(std::string&&) {}

std::ostream& operator<<(std::ostream& output, const MetricRegistry<true>::Metric&) {
    return output;
}

template <>
std::ostream& MetricRegistry<true>::Counter::print(std::ostream& output) const {
    return output;
}

template <>
void MetricRegistry<true>::Counter::increment(long) {}

template <>
MetricRegistry<true>::Counter& MetricRegistry<true>::create_counter(std::string&&) {
    static auto default_counter = Counter("");
    return default_counter;
}

template <>
MetricRegistry<true>::Histogram<>& MetricRegistry<true>::create_histogram(std::string&&) {
    static auto default_histogram = Histogram<>("");
    return default_histogram;
}

template <>
MetricRegistry<true>::Timer& MetricRegistry<true>::create_timer(std::string&&) {
    static auto default_timer = Timer("");
    return default_timer;
}

template <>
std::ostream& operator<<(std::ostream& output, const MetricRegistry<true>&) {
    return output;
}

template <>
template <>
std::ostream& MetricRegistry<true>::Histogram<>::print(std::ostream& output) const {
    return output;
}

template <>
template <>
std::ostream& MetricRegistry<true>::Histogram<std::chrono::high_resolution_clock::time_point>::print(
    std::ostream& output) const {
    return output;
}

template <>
template <>
MetricRegistry<true>::Histogram<>& MetricRegistry<true>::Histogram<>::operator+=(const Diff&) {
    static auto default_histogram = Histogram<>("");
    return default_histogram;
}

template <>
void MetricRegistry<true>::Timer::measure_stop(const MetricRegistry::Timer::Timestamp&) {}

template <>
MetricRegistry<true>::Timer::Timestamp MetricRegistry<true>::Timer::measure_start() const {
    return {};
}

#pragma clang diagnostic push
#pragma ide diagnostic ignored "performance-trivially-destructible"
template <>
MetricRegistry<true>::Timer::ScopeTracker::~ScopeTracker() = default;
#pragma clang diagnostic pop

#pragma clang diagnostic push
#pragma ide diagnostic ignored "cppcoreguidelines-pro-type-member-init"
template <>
MetricRegistry<true>::Timer::ScopeTracker::ScopeTracker(MetricRegistry::Timer*) {}
#pragma clang diagnostic pop

template <>
MetricRegistry<true>::Timer::ScopeTracker MetricRegistry<true>::Timer::track_scope() {
    return ScopeTracker{nullptr};
}
}  // namespace remotefs
