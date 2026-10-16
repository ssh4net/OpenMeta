// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/iptc_iim_serialize.h"

#include <vector>

namespace openmeta::detail {

// Reads entry and arena storage directly; callers must prevent concurrent
// mutation. Finalization is not required by this internal transfer path.
IptcIimSerializeResult
build_iptc_iim_payload(const MetaStore& store,
                       const IptcIimSerializeOptions& options,
                       bool skip_unsupported,
                       std::vector<std::byte>* output) noexcept;

}  // namespace openmeta::detail
