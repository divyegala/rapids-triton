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

#include <cuda_runtime_api.h>
#include <string>
#include <rapids_triton/memory/types.hpp>
#include <rapids_triton/triton/device.hpp>
#include <rapids_triton/triton/requests.hpp>
#include <triton/backend/backend_input_collector.h>
#include <triton/backend/backend_output_responder.h>

namespace triton { namespace backend { namespace rapids {
  struct Batch {
    using size_type = std::size_t;

    Batch(TRITONBACKEND_Request** raw_requests, request_size_t count, TRITONBACKEND_MemoryManager& triton_mem_manager, bool use_pinned_input, bool use_pinned_output, size_type max_batch_size, cudaStream_t stream) :
      requests_(raw_requests, count),
      responses_(construct_responses(requests_.begin(), requests_.end())),
      collector_{
        raw_requests,
        count,
        &responses_,
        &triton_mem_manager,
        use_pinned_input,
        stream
      },
      responder_{std::make_shared<BackendOutputResponder>(
        raw_requests,
        count,
        &responses_,
        max_batch_size,
        use_pinned_output,
        stream
      )},
      stream_{stream} {}


    template<typename T>
    auto get_input(std::string const& name, MemoryType memory_type, device_id_t device_id) {
      auto shape = get_input_shape(requests_.begin(), requests_.end(), name);
      auto size_bytes = sizeof(T) * std::reduce(shape.begin(), shape.end(), std::size_t{1}, std::multiplies<>());

      auto const* raw_buffer = static_cast<char*>(nullptr);
      auto reported_bytes = std::size_t{};
      auto reported_mem_type = memory_type;
      auto reported_device_id = device_id;

      collector_.ProcessTensor(
        name.c_str(),
        nullptr, // Return data without copy if possible
        size_bytes,
        {{memory_type, device_id}},
        &raw_buffer,
        &reported_bytes,
        &reported_memory_type,
        &reported_device_id
      );

      auto buffer = Buffer(
        reinterpret_cast<T*>(raw_buffer),
        reported_bytes,
        reported_memory_type,
        stream_
      );

      if (reported_memory_type != memory_type || reported_device_id != device_id) {
        throw(TritonException(Error::Internal, "data collected in wrong location");
      }

      return Tensor(std::move(shape), std::move(buffer));
    }

    template<typename T>
    auto get_output(std::string const& name, MemoryType memory_type, device_id_t device_id) {
      auto shape = get_output_shape(requests_.begin(), requests_.end(), name); // TODO(wphicks)
      // TODO(wphicks): construct buffer
      return OutputTensor(std::move(shape), std::move(buffer), responder_, name);
    }

    auto stream() const {
      return stream_;
    }

    void finalize() {
      // TODO(wphicks): report statistics
      if (responder_->Finalize()) {
        cuda_check(cudaStreamSynchronize(stream_));
      }
      //TODO(wphicks): Send responses
    }

    private:
      std::vector<TRITONBACKEND_Request*> requests_;
      std::vector<TRITONBACKEND_Response*> responses_;
      BackendInputCollector collector_;
      std::shared_pointer<BackendOutputResponder> responder_;
      cudaStream_t stream_;

      auto get_input_shape(std::string const& name) {
        auto result = decltype(get_triton_input_shape(*std::begin(requests_))){};
        if(!requests_.empty()) {
          result = get_triton_input_shape(*std::begin(requests_))
        }
        return result;
      }
  };
}}}  // namespace triton::backend::rapids
