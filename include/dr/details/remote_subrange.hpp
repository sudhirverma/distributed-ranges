// SPDX-FileCopyrightText: Intel Corporation
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <dr/concepts/concepts.hpp>
#include <dr/details/ranges_shim.hpp>
#include <iterator>

namespace lib {

template <std::forward_iterator I>
class remote_subrange : public rng::subrange<I, I> {
  using base = rng::subrange<I, I>;

public:
  remote_subrange()
    requires std::default_initializable<I>
  = default;

  constexpr remote_subrange(I first, I last, std::size_t rank)
      : base(first, last), rank_(rank) {}

  template <rng::forward_range R>
  constexpr remote_subrange(R &&r, std::size_t rank)
      : base(rng::begin(r), rng::end(r)), rank_(rank) {}

  template <lib::remote_range R>
  constexpr remote_subrange(R &&r)
      : base(rng::begin(r), rng::end(r)), rank_(lib::ranges::rank(r)) {}

  constexpr std::size_t rank() const noexcept { return rank_; }

private:
  std::size_t rank_;
};

template <rng::forward_range R>
remote_subrange(R &&, std::size_t) -> remote_subrange<rng::iterator_t<R>>;

template <lib::remote_range R>
remote_subrange(R &&) -> remote_subrange<rng::iterator_t<R>>;

} // namespace lib
