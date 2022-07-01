#include "remotefs/metrics/Metrics.h"

#include <ostream>

namespace remotefs {

template <>
MetricRegistry<false>::Metric::Metric(std::string&& name)
    : _name{std::move(name)} {}

template <>
MetricRegistry<true>::Metric::Metric(std::string&&) {}

std::ostream& operator<<(std::ostream& output, const MetricRegistry<false>::Metric& metric) {
    return metric.print(output);
}

std::ostream& operator<<(std::ostream& output, const MetricRegistry<true>::Metric&) {
    return output;
}
template <>
std::ostream& MetricRegistry<false>::Counter::print(std::ostream& output) const {
    output << _name << ":count:" << value;
    return output;
}

template <>
std::ostream& MetricRegistry<true>::Counter::print(std::ostream& output) const {
    return output;
}

template <>
void MetricRegistry<false>::Counter::increment(long inc) {
    value += inc;
}

template <>
void MetricRegistry<true>::Counter::increment(long) {}

template <>
MetricRegistry<false>::Counter& MetricRegistry<false>::create_counter(std::string&& name) {
    return std::get<Counter>(metrics.emplace_front(std::in_place_type<Counter>, std::move(name)));
}

template <>
MetricRegistry<true>::Counter& MetricRegistry<true>::create_counter(std::string&&) {
    static auto default_counter = Counter("");
    return default_counter;
}

template <>
MetricRegistry<false>::Histogram& MetricRegistry<false>::create_histogram(std::string&& name) {
    return std::get<Histogram>(metrics.emplace_front(std::in_place_type<Histogram>, std::move(name)));
}

template <>
MetricRegistry<true>::Histogram& MetricRegistry<true>::create_histogram(std::string&&) {
    static auto default_histogram = Histogram("");
    return default_histogram;
}

template <>
std::ostream& operator<<(std::ostream& output, const MetricRegistry<false>& registry) {
    for (const auto& metric : registry.metrics) {
        std::visit([&](auto&& arg) { output << arg; }, metric);
    }
    return output;
}

template <>
std::ostream& operator<<(std::ostream& output, const MetricRegistry<true>&) {
    return output;
}

template <>
std::ostream& MetricRegistry<false>::Histogram::print(std::ostream& output) const {
    output << _name << ":samples:" << stats.samples << "\n";
    output << _name << ":mean:" << stats.mean << "\n";
    output << _name << ":min:" << stats.min << "\n";
    output << _name << ":max:" << stats.max << "\n";
    output << _name << ":total:"
           << std::chrono::duration_cast<std::chrono::milliseconds>(
                  (std::chrono::duration_cast<std::chrono::duration<double, std::nano>>(stats.mean) * stats.samples +
                   stats.total_mod))
           << "\n";
    return output;
}

template <>
std::ostream& MetricRegistry<true>::Histogram::print(std::ostream& output) const {
    return output;
}

template <>
void MetricRegistry<false>::Histogram::measure_stop(const MetricRegistry::Histogram::Timestamp& start) {
    auto diff = Clock::now() - start;
    stats.samples++;
    stats.mean += (diff - stats.mean) / stats.samples;
    stats.min = std::min(stats.min, diff);
    stats.max = std::max(stats.min, diff);
    stats.total_mod = diff % stats.mean;
}

template <>
void MetricRegistry<true>::Histogram::measure_stop(const MetricRegistry::Histogram::Timestamp&) {}

template <>
MetricRegistry<false>::Histogram::Timestamp MetricRegistry<false>::Histogram::measure_start() const {
    return Clock::now();
}

template <>
MetricRegistry<true>::Histogram::Timestamp MetricRegistry<true>::Histogram::measure_start() const {
    return {};
}

template <>
MetricRegistry<false>::Histogram::ScopeTracker::~ScopeTracker() {
    histogram->measure_stop(start);
}

#pragma clang diagnostic push
#pragma ide diagnostic ignored "performance-trivially-destructible"
template <>
MetricRegistry<true>::Histogram::ScopeTracker::~ScopeTracker() = default;
#pragma clang diagnostic pop

template <>
MetricRegistry<false>::Histogram::ScopeTracker::ScopeTracker(MetricRegistry::Histogram* hist)
    : start{Clock::now()},
      histogram{hist} {}

#pragma clang diagnostic push
#pragma ide diagnostic ignored "cppcoreguidelines-pro-type-member-init"
template <>
MetricRegistry<true>::Histogram::ScopeTracker::ScopeTracker(MetricRegistry::Histogram*) {}
#pragma clang diagnostic pop

template <>
MetricRegistry<false>::Histogram::ScopeTracker MetricRegistry<false>::Histogram::track_scope() {
    return ScopeTracker{this};
}

template <>
MetricRegistry<true>::Histogram::ScopeTracker MetricRegistry<true>::Histogram::track_scope() {
    return ScopeTracker{nullptr};
}
}  // namespace remotefs
