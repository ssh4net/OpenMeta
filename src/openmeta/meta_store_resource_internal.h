// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openmeta/meta_store.h"

namespace openmeta {

struct MetaStoreResourceAccess final {
    static void inherit(const MetaStore& source,
                        MetaStore& destination) noexcept
    {
        destination.constrain_resources(source.max_entries_,
                                        source.arena_.max_size_bytes_);
        destination.entry_limit_exceeded_ = destination.entry_limit_exceeded_
                                            || source.entry_limit_exceeded_;
        destination.arena_.limit_exceeded_ = destination.arena_.limit_exceeded_
                                             || source.arena_.limit_exceeded_;
    }
};

}  // namespace openmeta
