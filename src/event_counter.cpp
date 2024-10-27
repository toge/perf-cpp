#include <algorithm>
#include <numeric>
#include <perfcpp/perf.h>
#include <stdexcept>
bool
perf::EventCounter::add(std::string&& counter_name)
{
  /// If the counter has no name, we interpret this as the user wants to "close" the group and add further counters to another one.
  if (counter_name.empty()) {
    if (this->_groups.empty() || this->_groups.back().empty()) {
      return true;
    }

    if (this->_groups.size() < this->_config.max_groups()) {
      this->_groups.emplace_back();
      return true;
    }

    throw std::runtime_error{std::string{"Cannot add another group – number of groups: "}.append(std::to_string(this->_groups.size())).append(", maximal number of groups: ").append(std::to_string(std::size_t{this->_config.max_groups()}))};
  }

  /// If the given name references an existing counter, add it.
  if (auto counter_config = this->_counter_definitions.counter(counter_name); counter_config.has_value()) {
    this->add(std::get<0>(counter_config.value()), std::get<1>(counter_config.value()), false);
    return true;
  }

  /// If the given name references an existing metric, add the metric and all its required counters.
  if (auto metric = this->_counter_definitions.metric(counter_name); metric.has_value()) {
    /// Add all required counters.
    for (auto&& dependent_counter_name : std::get<1>(metric.value()).required_counter_names()) {
      if (auto dependent_counter_config = this->_counter_definitions.counter(dependent_counter_name); dependent_counter_config.has_value()) {
        this->add(std::get<0>(dependent_counter_config.value()), std::get<1>(dependent_counter_config.value()), true);
      } else {
        throw std::runtime_error{ std::string{ "Cannot find counter '" }
                                    .append(dependent_counter_name)
                                    .append("' for metric '")
                                    .append(counter_name)
                                    .append("'.") };
      }
    }

    /// If all of the metric's counters could be added (i.e., no exception was thrown), add the metric itself.
    this->_counters.emplace_back(std::get<0>(metric.value()));
    return true;
  }

  throw std::runtime_error{
    std::string{ "Cannot find counter or metric with name '" }.append(counter_name).append("'.")
  };
}

void
perf::EventCounter::add(std::string_view counter_name, perf::CounterConfig counter, const bool is_hidden)
{
  /// If the counter is already added,
  if (auto iterator = std::find_if(this->_counters.begin(),
                                   this->_counters.end(),
                                   [&counter_name](const auto& counter) { return counter.name() == counter_name; });
      iterator != this->_counters.end()) {
    iterator->is_hidden(iterator->is_hidden() && is_hidden);
    return;
  }

  /// Check if space for more counters left: If the latest group is "full", check, if there is space for another group.
  if (this->_groups.size() == this->_config.max_groups() &&
      this->_groups.back().size() >= this->_config.max_counters_per_group()) {
    throw std::runtime_error{ "Cannot add more counters: Reached maximum number of groups and maximum number of counters in the latest group." };
  }

  /// If the latest group is "full", add a new group. We already verified that there will be enough space.
  if (this->_groups.empty() || this->_groups.back().size() >= this->_config.max_counters_per_group()) {
    this->_groups.emplace_back();
  }

  const auto group_id = std::uint8_t(this->_groups.size()) - 1U;
  const auto in_group_id = std::uint8_t(this->_groups.back().size());
  this->_counters.emplace_back(counter_name, is_hidden, group_id, in_group_id);
  this->_groups.back().add(counter);
}

bool
perf::EventCounter::add(std::vector<std::string>&& counter_names)
{
  /// Add all counter names. If one of them fails, add() will throw an exception.
  for (auto& name : counter_names) {
    this->add(std::move(name));
  }

  /// If no exception was thrown, we are good to go.
  return true;
}

bool
perf::EventCounter::add(const std::vector<std::string>& counter_names)
{
  return this->add(std::vector<std::string>(counter_names));
}

bool
perf::EventCounter::start()
{
  /// Open all counters. If one of them fails, group.open() will throw an exception.
  for (auto& group : this->_groups) {
    group.open(this->_config);
  }

  /// Start all counters. If one of them fails, group.start() will throw an exception.
  for (auto& group : this->_groups) {
    group.start();
  }

  /// If no exception was thrown, we are good to go.
  return true;
}

void
perf::EventCounter::stop()
{
  /// Stop all counters. If one of them fails, group.stop() will throw an exception.
  for (auto& group : this->_groups) {
    group.stop();
  }

  /// Close all counters. If one of them fails, group.close() will throw an exception.
  for (auto& group : this->_groups) {
    group.close();
  }
}

perf::CounterResult
perf::EventCounter::result(std::uint64_t normalization) const
{
  /// Build result with all counters, including hidden ones.
  auto hardware_event_values = std::vector<std::pair<std::string_view, double>>{};
  hardware_event_values.reserve(this->_counters.size());

  /// Copy only the hardware-event values.
  for (const auto& event : this->_counters) {
    if (event.is_counter()) {
      const auto value = this->_groups[event.group_id()].get(event.in_group_id()) / double(normalization);
      hardware_event_values.emplace_back(event.name(), value);
    }
  }

  /// This result only contains hardware-event values to either copy the value (if the event is requested) or use the value for calculating a metric.
  auto hardware_events_result = CounterResult{ std::move(hardware_event_values) };

  /// List of all requested values (hardware-events and metrics)
  auto result = std::vector<std::pair<std::string_view, double>>{};
  result.reserve(this->_counters.size());

  for (const auto& event : this->_counters) {
    /// First, add all hardware events that were requested to be shown: event.is_hidden() indicates that the event was only required by a metric but not requested by the user.
    if (event.is_counter() && !event.is_hidden()) {
      if (const auto value = hardware_events_result.get(event.name()); value.has_value()) {
        result.emplace_back(event.name(), value.value());
      }
    }

    /// If the event is a metric (not a hardware event), calculate the value of the metric and add it to the result.
    else {
      if (auto metric = this->_counter_definitions.metric(event.name()); metric.has_value()) {
        if (const auto value = std::get<1>(metric.value()).calculate(hardware_events_result); value.has_value()) {
          result.emplace_back(std::get<0>(metric.value()), value.value());
        }
      }
    }
  }

  return CounterResult{ std::move(result) };
}

bool
perf::MultiEventCounterBase::add(std::vector<EventCounter>& event_counter, std::string&& counter_name)
{
  for (auto i = 0U; i < event_counter.size() - 1U; ++i) {
    event_counter[i].add(std::string{ counter_name });
  }

  return event_counter.back().add(std::move(counter_name));
}

bool
perf::MultiEventCounterBase::add(std::vector<EventCounter>& event_counter, std::vector<std::string>&& counter_names)
{
  for (auto i = 0U; i < event_counter.size() - 1U; ++i) {
    event_counter[i].add(std::vector<std::string>{ counter_names });
  }

  return event_counter.back().add(std::move(counter_names));
}

bool
perf::MultiEventCounterBase::add(std::vector<EventCounter>& event_counter,
                                 const std::vector<std::string>& counter_names)
{
  for (auto& thread_local_counter : event_counter) {
    thread_local_counter.add(counter_names);
  }

  return true;
}

perf::CounterResult
perf::MultiEventCounterBase::result(const std::vector<perf::EventCounter>& event_counters,
                                    const std::uint64_t normalization)
{
  /// The reference_event_counter is used to access counters (all EventCounters from the list are required to have the same counters but different values).
  const auto& reference_event_counter = event_counters.front();

  /// Build one result of only hardware-event values over all EventCounters by aggregating their values.
  auto aggregated_hardware_event_values = std::vector<std::pair<std::string_view, double>>{};
  aggregated_hardware_event_values.reserve(reference_event_counter._counters.size());

  /// For every (hardware) event, accumulate the results for every EventCounter from event_counters.
  for (const auto& event : reference_event_counter._counters) {
    if (event.is_counter()) {

      /// Add up the values from all individual EventCounters in event_counters.
      const auto value = std::accumulate(
        event_counters.begin(),
        event_counters.end(),
        .0,
        [id = event.group_id(), in_group_id = event.in_group_id()](const double sum, const auto& event_counter) {
          return sum + event_counter._groups[id].get(in_group_id);
      });

      /// Normalize the value (by the given normalization parameter) and add to the aggregated results.
      aggregated_hardware_event_values.emplace_back(event.name(), value / double(normalization));
    }
  }

  /// This result only contains hardware-event values to either copy the value (if the event is requested) or use the value for calculating a metric.
  auto hardware_event_results = CounterResult{ std::move(aggregated_hardware_event_values) };

  /// List of all requested values (hardware-events and metrics)
  auto result = std::vector<std::pair<std::string_view, double>>{};
  result.reserve(reference_event_counter._counters.size());

  for (const auto& event : reference_event_counter._counters) {
    /// First, add all hardware events that were requested to be shown: event.is_hidden() indicates that the event was only required by a metric but not requested by the user.
    if (event.is_counter() && !event.is_hidden()) {
      if (const auto value = hardware_event_results.get(event.name()); value.has_value()) {
        result.emplace_back(event.name(), value.value());
      }
    }

    /// If the event is a metric (not a hardware event), calculate the value of the metric and add it to the result.
    else {
      if (auto metric = reference_event_counter._counter_definitions.metric(event.name()); metric.has_value()) {
        if (auto value = std::get<1>(metric.value()).calculate(hardware_event_results); value.has_value()) {
          result.emplace_back(std::get<0>(metric.value()), value.value());
        }
      }
    }
  }

  return CounterResult{ std::move(result) };
}

perf::MultiThreadEventCounter::MultiThreadEventCounter(const perf::CounterDefinition& counter_list,
                                                       const std::uint16_t num_threads,
                                                       const perf::Config config)
{
  this->_thread_local_counter.reserve(num_threads);
  for (auto i = 0U; i < num_threads; ++i) {
    this->_thread_local_counter.emplace_back(counter_list, config);
  }
}

perf::MultiThreadEventCounter::MultiThreadEventCounter(perf::EventCounter&& event_counter,
                                                       const std::uint16_t num_threads)
{
  this->_thread_local_counter.reserve(num_threads);
  for (auto i = 0U; i < num_threads - 1U; ++i) {
    this->_thread_local_counter.push_back(event_counter);
  }
  this->_thread_local_counter.emplace_back(std::move(event_counter));
}

perf::MultiProcessEventCounter::MultiProcessEventCounter(const perf::CounterDefinition& counter_list,
                                                         std::vector<pid_t>&& process_ids,
                                                         perf::Config config)
{
  this->_process_local_counter.reserve(process_ids.size());

  for (const auto process_id : process_ids) {
    config.process_id(process_id);
    this->_process_local_counter.emplace_back(counter_list, config);
  }
}

perf::MultiProcessEventCounter::MultiProcessEventCounter(perf::EventCounter&& event_counter,
                                                         std::vector<pid_t>&& process_ids)
{
  this->_process_local_counter.reserve(process_ids.size());
  auto config = event_counter.config();

  for (auto i = 0U; i < process_ids.size() - 1U; ++i) {
    config.process_id(process_ids[i]);
    auto process_local_counter = perf::EventCounter{ event_counter };
    process_local_counter.config(config);

    this->_process_local_counter.emplace_back(std::move(process_local_counter));
  }

  config.process_id(process_ids.back());
  event_counter.config(config);
  this->_process_local_counter.emplace_back(std::move(event_counter));
}

bool
perf::MultiProcessEventCounter::start()
{
  for (auto& event_counter : this->_process_local_counter) {
    event_counter.start();
  }

  return true;
}

void
perf::MultiProcessEventCounter::stop()
{
  for (auto& event_counter : this->_process_local_counter) {
    event_counter.stop();
  }
}

perf::MultiCoreEventCounter::MultiCoreEventCounter(const perf::CounterDefinition& counter_list,
                                                   std::vector<std::uint16_t>&& cpu_ids,
                                                   perf::Config config)
{
  config.process_id(-1); /// Record every thread/process on the given CPUs.

  this->_cpu_local_counter.reserve(cpu_ids.size());

  for (const auto cpu_id : cpu_ids) {
    config.cpu_id(cpu_id);
    this->_cpu_local_counter.emplace_back(counter_list, config);
  }
}

perf::MultiCoreEventCounter::MultiCoreEventCounter(perf::EventCounter&& event_counter,
                                                   std::vector<std::uint16_t>&& cpu_ids)
{
  this->_cpu_local_counter.reserve(cpu_ids.size());
  auto config = event_counter.config();
  config.process_id(-1); /// Record every thread/process on the given CPUs.

  for (auto i = 0U; i < cpu_ids.size() - 1U; ++i) {
    config.cpu_id(cpu_ids[i]);
    auto process_local_counter = perf::EventCounter{ event_counter };
    process_local_counter.config(config);

    this->_cpu_local_counter.push_back(std::move(process_local_counter));
  }

  config.cpu_id(cpu_ids.back());
  event_counter.config(config);
  this->_cpu_local_counter.push_back(std::move(event_counter));
}

bool
perf::MultiCoreEventCounter::start()
{
  for (auto& event_counter : this->_cpu_local_counter) {
    event_counter.start();
  }

  return true;
}

void
perf::MultiCoreEventCounter::stop()
{
  for (auto& event_counter : this->_cpu_local_counter) {
    event_counter.stop();
  }
}