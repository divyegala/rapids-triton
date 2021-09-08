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
#include <memory>
#include <rapids_triton/triton/model.hpp>
#include <triton/backend/backend_model.h>

namespace triton { namespace backend { namespace rapids {
template<typename RapidsSharedState>
struct TritonModelState : public BackendModel {

  TritonModelState(TRITONBACKEND_Model& triton_model)
      : BackendModel(&triton_model), 
        state_{std::make_shared<RapidsSharedState>(
            get_model_config(triton_model))} {}

  void load() { state_->load(); }
  void unload() { state_->unload(); }

  auto get_shared_state() { return state_; }

 private:
  std::shared_ptr<RapidsSharedState> state_;
};

}}}