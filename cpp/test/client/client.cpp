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

#include <rapids_triton/client/model.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace triton::client {

TEST(RapidsTriton, client_server) {
    std::string model_repo_path {"/models"};
    TritonServer server {model_repo_path, 1};

    std::string model_name {"identity"};
    TritonModel model {server, model_name, 1};
}

}
