// Copyright 2026 The Cobalt Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "media/base/platform_init_data_types.h"

#include <map>

#include "base/no_destructor.h"

namespace media {

namespace {

struct Registry {
  std::map<std::string, EmeInitDataType> string_to_enum;
  std::map<EmeInitDataType, std::string> enum_to_string;
};

Registry& GetRegistry() {
  static base::NoDestructor<Registry> registry;
  return *registry;
}

const std::string& EmptyString() {
  static base::NoDestructor<std::string> empty;
  return *empty;
}

}  // namespace

// static
void PlatformInitDataTypes::Register(const std::string& type_string,
                                     EmeInitDataType enum_value) {
  auto& registry = GetRegistry();
  registry.string_to_enum[type_string] = enum_value;
  registry.enum_to_string[enum_value] = type_string;
}

// static
EmeInitDataType PlatformInitDataTypes::ToEnum(
    const std::string& type_string) {
  const auto& registry = GetRegistry();
  auto it = registry.string_to_enum.find(type_string);
  if (it != registry.string_to_enum.end()) {
    return it->second;
  }
  return EmeInitDataType::UNKNOWN;
}

// static
const std::string& PlatformInitDataTypes::ToString(
    EmeInitDataType enum_value) {
  const auto& registry = GetRegistry();
  auto it = registry.enum_to_string.find(enum_value);
  if (it != registry.enum_to_string.end()) {
    return it->second;
  }
  return EmptyString();
}

}  // namespace media
