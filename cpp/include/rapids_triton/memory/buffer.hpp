/*
 * Copyright (c) 2021, NVIDIA CORPORATION.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once
#include <cstddef>
#include <memory>
#include <variant>

#include <cuda_runtime_api.h>

#include <rapids_triton/build_control.hpp>
#include <rapids_triton/exceptions.hpp>
#include <rapids_triton/memory/types.hpp>
#include <rapids_triton/memory/detail/allocate.hpp>
#include <rapids_triton/memory/detail/copy.hpp>
#include <rapids_triton/triton/device.hpp>

namespace triton { namespace backend { namespace rapids {
  template <typename T>
  struct Buffer {
    using size_type = std::size_t;
    using value_type = T;

    using h_ptr = T*;
    using d_ptr = T*;
    using owned_h_ptr = std::unique_ptr<T[]>;
    using owned_d_ptr = std::unique_ptr<T, detail::dev_deallocater<T>{}>;
    using data_ptr = std::variant<h_ptr, d_ptr, owned_h_ptr, owned_d_ptr>;

    Buffer() noexcept : device_{}, data_{std::in_place_index<0>, nullptr}, size_{}, stream_{} {}

    /**
     * @brief Construct buffer of given size in given memory location (either
     * on host or on device)
     * A buffer constructed in this way is owning and will release allocated
     * resources on deletion
     */
    Buffer(size_type size, MemoryType memory_type=DeviceMemory, device_id_t device=0, cudaStream_t
        stream=0) :
      device_{device}, data_{allocate(size, memory_type)}, size_{size}, stream_{stream} {}

    /**
     * @brief Construct buffer from given source in given memory location (either
     * on host or on device)
     * A buffer constructed in this way is non-owning; the caller is
     * responsible for freeing any resources associated with the input pointer
     */
    Buffer(T* input_data, size_type size, MemoryType memory_type=DeviceMemory,
        device_id_t device=0, cudaStream_t stream=0) :
      device_{device}, data_{input_data}, size_{size}, stream_{stream} {}

    /**
     * @brief Construct one buffer from another in the given memory location
     * (either on host or on device)
     * A buffer constructed in this way is owning and will copy the data from
     * the original location
     */
    Buffer(Buffer<T> const& other, MemoryType memory_type, device_id_t device=0) : device_{device}, data_([&other](){
        auto result = allocate(other.size_, memory_type);
        copy(result, other.data_, other.size_, other.stream_);
        return result;
      }()), size_{other.size_}, stream_{other.stream_} {}

    /**
     * @brief Create owning copy of existing buffer
     * The memory type of this new buffer will be the same as the original
     */
    Buffer(Buffer<T> const& other) : Buffer(other, other.mem_type(), other.device(), other.stream()) {}

    Buffer(Buffer<T>&& other, MemoryType memory_type) : device_{other.device()}, data_{[&other, memory_type](){
      data_ptr result;
      if(memory_type == other.mem_type()) {
        result = std::move(other.data_);
      } else {
        result = allocate(other.size_, memory_type);
        copy(result, other.data_, other.size_, other.stream_);
      }
      return result;
    }()}, size_{other.size_}, stream{other.stream_} {}

    Buffer(Buffer<T>&& other) noexcept : device_{other.device_}, data_{std::move{other.data_}}, size_{other.size_},
      stream_{other.stream_} {}

    ~Buffer() = default;

    /**
     * @brief Return where memory for this buffer is located (host or device)
     */
    auto mem_type() const noexcept {
      return data_ptr.index() % 2 == 0 ? HostMemory, DeviceMemory;
    }

    /**
     * @brief Return number of elements in buffer
     */
    auto size() const noexcept {
      return size_;
    }

    /**
     * @brief Return pointer to data stored in buffer
     */
    auto* data() noexcept {
      return get_raw_ptr(data_);
    }

    auto device() const noexcept {
      return device_;
    }

    /**
     * @brief Return CUDA stream associated with this buffer
     */
    auto stream() const noexcept {
      return stream_;
    }

    /**
     * @brief Set CUDA stream for this buffer to new value
     *
     * @warning This method calls cudaStreamSynchronize on the old stream
     * before updating. Be aware of performance implications and try to avoid
     * interactions between buffers on different streams where possible.
     */
    void set_stream(cudaStream_t new_stream) {
      if constexpr (IS_GPU_BUILD) {
        cuda_check(cudaStreamSynchronize(stream_));
      }
      stream_ = new_stream;
    }

   private:
    device_id_t device_;
    data_ptr data_;
    size_type size_;
    cudaStream_t stream_;

    // Helper function for accessing raw pointer to underlying data of data_ptr
    static auto get_raw_ptr(data_ptr ptr) noexcept {
      /* Switch statement is an optimization relative to std::visit to avoid
       * vtable overhead for a small number of alternatives */
      T* result;
      switch (ptr.index()) {
        case 0:
          result = std::get<0>(ptr);
          break;
        case 1:
          result = std::get<1>(ptr);
          break;
        case 2:
          result = std::get<2>(ptr).get();
          break;
        case 3:
          result = std::get<3>(ptr).get();
          break;
      }
      return result;
    }

    // Helper function for allocating memory in constructors
    static auto allocate(size_type size, MemoryType memory_type=DeviceMemory) {
      data_ptr result;
      if (memory_type == DeviceMemory) {
        if constexpr (IS_GPU_BUILD) {
          cuda_check(cudaSetDevice(device_));
          result = data_ptr{owned_d_ptr{detail::dev_allocate<T>(size)}};
        } else {
          throw TritonException(
            Error::Internal,
            "DeviceMemory requested in CPU-only build of FIL backend"
          );
        }
      } else {
        result = std::make_unique<T[]>(size);
      }
      return result;
    }

    // Helper function for copying memory in constructors, where there are
    // stronger guarantees on conditions that would otherwise need to be
    // checked
    static void copy(
        data_ptr dst, data_ptr src, size_type len, cudaStream_t stream) {
      auto raw_dst = get_raw_ptr(dst);
      auto raw_src = get_raw_ptr(src);

      auto dst_mem_type = dst.index() % 2 == 0 ? HostMemory :  DeviceMemory;
      auto src_mem_type = src.index() % 2 == 0 ? HostMemory :  DeviceMemory;

      detail::copy(raw_dst, raw_src, len, dst_mem_type, src_mem_type);
    }

  };

  /**
   * @brief Copy data from one Buffer to another
   *
   * @param dst The destination buffer
   * @param src The source buffer
   * @param dst_begin The offset from the beginning of the destination buffer
   * at which to begin copying to.
   * @param src_begin The offset from the beginning of the source buffer
   * at which to begin copying from.
   * @param src_end The offset from the beginning of the source buffer
   * before which to end copying from.
   *
   * @warning This method is NOT thread-safe. If the stream of the src buffer
   * changes while a copy is in progress, dst may receive incorrect data from
   * src. Avoid interactions between buffers on different streams, *especially*
   * when those buffers may be modified on different host threads as well.
   */
  template<typename T, typename U>
  copy(Buffer<T> dst&&, Buffer<U> src&&, Buffer<U>::size_type dst_begin,
      Buffer<U>::size_type src_begin, Buffer<T>::size_type src_end) {
    if(dst.stream() != src.stream()) {
      dst.set_stream(src.stream());
    }
    auto len = src_end - src_begin;
    if (len < 0 || src_end > src.size() || len > dst.size() - dst_begin) {
      throw TritonException(Error::Internal, "bad copy between buffers");
    }

    auto raw_dst = dst.data() + dst_begin;
    auto raw_src = src.data() + src_begin;

    detail::copy(raw_dst, raw_src, len, dst.stream(), dst.mem_type(),
        src.mem_type());
  }

  template<typename T, typename U>
  void copy(Buffer<T> dst&&, Buffer<U> src&&) {
    copy(dst, src, dst.begin(), src.begin(), src.begin() + src.size());
  }

  template<typename T, typename U>
  void copy(Buffer<T> dst&&, Buffer<U> src&&, Buffer<U>::size_type dst_begin) {
    copy(dst, src, dst_begin, src.begin(), src.begin() + src.size());
  }

  template<typename T, typename U>
  void copy(Buffer<T> dst&&, Buffer<U> src&&, Buffer<U>::size_type src_begin,
      Buffer<T>::size_type src_end) {
    copy(dst, src, dst_begin, src.begin(), src.begin() + src_end);
  }
}}}  // namespace triton::backend::rapids