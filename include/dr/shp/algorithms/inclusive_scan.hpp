// SPDX-FileCopyrightText: Intel Corporation
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <CL/sycl.hpp>

#include <oneapi/dpl/execution>

#include <dr/shp/algorithms/execution_policy.hpp>
#include <dr/shp/allocators.hpp>
#include <dr/shp/init.hpp>
#include <dr/shp/vector.hpp>
#include <oneapi/dpl/async>
#include <oneapi/dpl/numeric>

#include <dr/concepts/concepts.hpp>

namespace shp {

template <typename ExecutionPolicy, lib::distributed_contiguous_range R,
          lib::distributed_contiguous_range O, typename BinaryOp,
          typename U = rng::range_value_t<R>>
void inclusive_scan_impl_(ExecutionPolicy &&policy, R &&r, O &&o,
                          BinaryOp &&binary_op, std::optional<U> init = {}) {
  namespace sycl = cl::sycl;

  using T = rng::range_value_t<O>;

  static_assert(
      std::is_same_v<std::remove_cvref_t<ExecutionPolicy>, device_policy>);

  auto zipped_view = shp::views::zip(r, o);
  auto zipped_segments = zipped_view.zipped_segments();

  if constexpr (std::is_same_v<std::remove_cvref_t<ExecutionPolicy>,
                               device_policy>) {

    auto &&devices = std::forward<ExecutionPolicy>(policy).get_devices();

    std::vector<sycl::event> events;

    auto root = devices[0];
    shp::device_allocator<T> allocator(shp::context(), root);
    shp::vector<T, shp::device_allocator<T>> partial_sums(
        std::size_t(zipped_segments.size()), allocator);

    std::size_t segment_id = 0;
    for (auto &&segs : zipped_segments) {
      auto &&[in_segment, out_segment] = segs;

      auto device = devices[lib::ranges::rank(in_segment)];

      sycl::queue q(shp::context(), device);
      oneapi::dpl::execution::device_policy local_policy(q);

      auto dist = std::distance(rng::begin(in_segment), rng::end(in_segment));
      assert(dist > 0);

      auto first = rng::begin(in_segment);
      auto last = rng::end(in_segment);
      auto d_first = rng::begin(out_segment);

      // NOTE: some very odd runtime behavior is happening here.
      // `inclusive_scan_async`
      //       is failing with raw pointers and *succeeding with iterators*,
      //       while everything else (`dpl::for_each`, `single_task`,
      //       `parallel_for`) works with raw pointers but *fails with
      //       iterators*.
      //
      // More investigation (and some bug reports) are likely necessary, but
      // this works for now.

      sycl::event event;

      if (segment_id == 0 && init.has_value()) {
        event = oneapi::dpl::experimental::inclusive_scan_async(
            local_policy, first, last, d_first, binary_op, init.value());
      } else {
        event = oneapi::dpl::experimental::inclusive_scan_async(
            local_policy, first, last, d_first, binary_op);
      }

      auto dst_iter = lib::ranges::local(partial_sums).data() + segment_id;

      auto src_iter = lib::ranges::local(out_segment).data();
      std::advance(src_iter, dist - 1);

      auto e = q.submit([&](auto &&h) {
        h.depends_on(event);
        h.single_task([=]() {
          rng::range_value_t<O> value = *src_iter;
          *dst_iter = value;
        });
      });

      events.push_back(e);

      segment_id++;
    }

    for (auto &&e : events) {
      e.wait();
    }
    events.clear();

    sycl::queue q(shp::context(), root);
    oneapi::dpl::execution::device_policy local_policy(q);

    auto first = lib::ranges::local(partial_sums).data();
    auto last = first + partial_sums.size();

    oneapi::dpl::experimental::inclusive_scan_async(local_policy, first, last,
                                                    first, binary_op)
        .wait();

    std::size_t idx = 0;
    for (auto &&segs : zipped_segments) {
      auto &&[in_segment, out_segment] = segs;
      auto device = devices[lib::ranges::rank(out_segment)];

      sycl::queue q(shp::context(), device);
      oneapi::dpl::execution::device_policy local_policy(q);

      if (idx > 0) {
        T sum = partial_sums[idx - 1];

        auto first = rng::begin(out_segment);
        auto last = rng::end(out_segment);

        sycl::event e = oneapi::dpl::experimental::for_each_async(
            local_policy, first, last,
            [=](auto &&x) { x = binary_op(x, sum); });

        events.push_back(e);
      }
      idx++;
    }

    for (auto &&e : events) {
      e.wait();
    }

  } else {
    assert(false);
  }
}

template <typename ExecutionPolicy, lib::distributed_contiguous_range R,
          lib::distributed_contiguous_range O, typename BinaryOp, typename T>
void inclusive_scan(ExecutionPolicy &&policy, R &&r, O &&o,
                    BinaryOp &&binary_op, T init) {
  inclusive_scan_impl_(std::forward<ExecutionPolicy>(policy),
                       std::forward<R>(r), std::forward<O>(o),
                       std::forward<BinaryOp>(binary_op), std::optional(init));
}

template <typename ExecutionPolicy, lib::distributed_contiguous_range R,
          lib::distributed_contiguous_range O, typename BinaryOp>
void inclusive_scan(ExecutionPolicy &&policy, R &&r, O &&o,
                    BinaryOp &&binary_op) {
  inclusive_scan_impl_(std::forward<ExecutionPolicy>(policy),
                       std::forward<R>(r), std::forward<O>(o),
                       std::forward<BinaryOp>(binary_op));
}

template <typename ExecutionPolicy, lib::distributed_contiguous_range R,
          lib::distributed_contiguous_range O>
void inclusive_scan(ExecutionPolicy &&policy, R &&r, O &&o) {
  inclusive_scan(std::forward<ExecutionPolicy>(policy), std::forward<R>(r),
                 std::forward<O>(o), std::plus<>());
}

// Distributed iterator versions

template <typename ExecutionPolicy, lib::distributed_iterator Iter,
          lib::distributed_iterator OutputIter, typename BinaryOp, typename T>
OutputIter inclusive_scan(ExecutionPolicy &&policy, Iter first, Iter last,
                          OutputIter d_first, BinaryOp &&binary_op, T init) {

  auto dist = std::distance(first, last);
  auto d_last = d_first;
  std::advance(d_last, dist);
  inclusive_scan(std::forward<ExecutionPolicy>(policy),
                 rng::subrange(first, last), rng::subrange(d_first, d_last),
                 std::forward<BinaryOp>(binary_op), init);

  return d_last;
}

template <typename ExecutionPolicy, lib::distributed_iterator Iter,
          lib::distributed_iterator OutputIter, typename BinaryOp>
OutputIter inclusive_scan(ExecutionPolicy &&policy, Iter first, Iter last,
                          OutputIter d_first, BinaryOp &&binary_op) {

  auto dist = std::distance(first, last);
  auto d_last = d_first;
  std::advance(d_last, dist);
  inclusive_scan(std::forward<ExecutionPolicy>(policy),
                 rng::subrange(first, last), rng::subrange(d_first, d_last),
                 std::forward<BinaryOp>(binary_op));

  return d_last;
}

template <typename ExecutionPolicy, lib::distributed_iterator Iter,
          lib::distributed_iterator OutputIter>
OutputIter inclusive_scan(ExecutionPolicy &&policy, Iter first, Iter last,
                          OutputIter d_first) {
  auto dist = std::distance(first, last);
  auto d_last = d_first;
  std::advance(d_last, dist);
  inclusive_scan(std::forward<ExecutionPolicy>(policy),
                 rng::subrange(first, last), rng::subrange(d_first, d_last));

  return d_last;
}

} // namespace shp
