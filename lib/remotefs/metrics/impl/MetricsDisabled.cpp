#include "remotefs/metrics/Metrics.h"

namespace remotefs {
template <>
MetricRegistry<true>::Metric::Metric(std::string&&) {}

namespace {
auto default_histogram = MetricRegistry<true>::Histogram<>("");
auto default_double_histogram = MetricRegistry<true>::Histogram<double>("");
auto default_timer = MetricRegistry<true>::Timer("");
auto default_counter = MetricRegistry<true>::Counter("");
}  // namespace

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
    return default_counter;
}

template <>
MetricRegistry<true>::Histogram<>& MetricRegistry<true>::create_histogram(std::string&&) {
    return default_histogram;
}

template <>
MetricRegistry<true>::Histogram<double>& MetricRegistry<true>::create_histogram_double(std::string&&) {
    return default_double_histogram;
}

template <>
MetricRegistry<true>::Timer& MetricRegistry<true>::create_timer(std::string&&) {
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
std::ostream& MetricRegistry<true>::Histogram<double>::print(std::ostream& output) const {
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
    return default_histogram;
}

template <>
template <>
MetricRegistry<true>::Histogram<double>& MetricRegistry<true>::Histogram<double>::operator+=(const Diff&) {
    return default_double_histogram;
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
