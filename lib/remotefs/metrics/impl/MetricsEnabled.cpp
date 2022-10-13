#include <ostream>

#include "remotefs/metrics/Metrics.h"

namespace remotefs {

template <>
MetricRegistry<false>::Metric::Metric(std::string&& name)
    : _name{std::move(name)} {}

std::ostream& operator<<(std::ostream& output, const MetricRegistry<false>::Metric& metric) {
    return metric.print(output);
}

template <>
std::ostream& MetricRegistry<false>::Counter::print(std::ostream& output) const {
    output << _name << ":count:" << value;
    return output;
}

template <>
void MetricRegistry<false>::Counter::increment(long inc) {
    value += inc;
}

template <>
MetricRegistry<false>::Counter& MetricRegistry<false>::create_counter(std::string&& name) {
    return std::get<Counter>(metrics.emplace_front(std::in_place_type<Counter>, std::move(name)));
}

template <>
MetricRegistry<false>::Histogram<>& MetricRegistry<false>::create_histogram(std::string&& name) {
    return std::get<Histogram<>>(metrics.emplace_front(std::in_place_type<Histogram<>>, std::move(name)));
}

template <>
MetricRegistry<false>::Timer& MetricRegistry<false>::create_timer(std::string&& name) {
    return std::get<Timer>(metrics.emplace_front(std::in_place_type<Timer>, std::move(name)));
}

template <>
std::ostream& operator<<(std::ostream& output, const MetricRegistry<false>& registry) {
    for (const auto& metric : registry.metrics) {
        std::visit([&](auto&& arg) { output << arg; }, metric);
    }
    return output;
}

template <>
template <>
std::ostream& MetricRegistry<false>::Histogram<>::print(std::ostream& output) const {
    output << _name << ":samples:" << stats.samples << "\n";
    output << _name << ":mean:" << stats.mean << "\n";
    output << _name << ":min:" << stats.min << "\n";
    output << _name << ":max:" << stats.max << "\n";
    output << _name << ":total:" << stats.mean * stats.samples + stats.total_mod << "\n";
    return output;
}

template <>
template <>
std::ostream& MetricRegistry<false>::Histogram<std::chrono::high_resolution_clock::time_point>::print(
    std::ostream& output) const {
    output << _name << ":samples:" << stats.samples << "\n";
    output << _name << ":mean:" << stats.mean << "\n";
    output << _name << ":min:" << stats.min << "\n";
    output << _name << ":max:" << stats.max << "\n";
    output << _name << ":total:" << stats.mean * stats.samples + stats.total_mod << "\n";
    return output;
}

template <>
template <>
MetricRegistry<false>::Histogram<>& MetricRegistry<false>::Histogram<>::operator+=(const Diff& duration) {
    stats.samples++;
    stats.mean += (duration - stats.mean) / stats.samples;
    stats.min = std::min(stats.min, duration);
    stats.max = std::max(stats.min, duration);
    stats.total_mod = duration % stats.mean;
    return *this;
}

template <>
template <>
MetricRegistry<false>::Histogram<std::chrono::high_resolution_clock::time_point>&
MetricRegistry<false>::Histogram<std::chrono::high_resolution_clock::time_point>::operator+=(const Diff& duration) {
    stats.samples++;
    stats.mean += (duration - stats.mean) / stats.samples;
    stats.min = std::min(stats.min, duration);
    stats.max = std::max(stats.min, duration);
    stats.total_mod = duration % stats.mean;
    return *this;
}

template <>
void MetricRegistry<false>::Timer::measure_stop(const MetricRegistry::Timer::Timestamp& start) {
    auto diff = Clock::now() - start;
    *this += diff;
}

template <>
MetricRegistry<false>::Timer::Timestamp MetricRegistry<false>::Timer::measure_start() const {
    return Clock::now();
}

template <>
MetricRegistry<false>::Timer::ScopeTracker::~ScopeTracker() {
    histogram->measure_stop(start);
}

template <>
MetricRegistry<false>::Timer::ScopeTracker::ScopeTracker(MetricRegistry::Timer* hist)
    : start{Clock::now()},
      histogram{hist} {}

template <>
MetricRegistry<false>::Timer::ScopeTracker MetricRegistry<false>::Timer::track_scope() {
    return ScopeTracker{this};
}

}  // namespace remotefs
