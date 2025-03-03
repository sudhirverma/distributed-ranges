// SPDX-FileCopyrightText: Intel Corporation
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <CL/sycl.hpp>
#include <type_traits>

namespace shp {

template <typename T>
  requires(std::is_trivially_copyable_v<T> || std::is_void_v<T>)
class device_ref {
public:
  device_ref() = delete;
  ~device_ref() = default;
  device_ref(const device_ref &) = default;
  device_ref &operator=(const device_ref &) = default;

  device_ref(T *pointer) : pointer_(pointer) {}

  operator T() const {
#ifdef __SYCL_DEVICE_ONLY__
    return *pointer_;
#else
    // cl::sycl::queue q(shp::context(), sycl::gpu_selector());
    cl::sycl::queue q;
    char buffer[sizeof(T)] __attribute__((aligned(sizeof(T))));
    q.memcpy(reinterpret_cast<T *>(buffer), pointer_, sizeof(T)).wait();
    return *reinterpret_cast<T *>(buffer);
#endif
  }

  device_ref operator=(const T &value)
    requires(!std::is_const_v<T>)
  {
#ifdef __SYCL_DEVICE_ONLY__
    *pointer_ = value;
#else
    // cl::sycl::queue q(shp::context(), sycl::gpu_selector());
    cl::sycl::queue q;
    q.memcpy(pointer_, &value, sizeof(T)).wait();
#endif
    return *this;
  }

private:
  T *pointer_;
};

} // namespace shp
