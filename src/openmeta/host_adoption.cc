// SPDX-License-Identifier: Apache-2.0

#include "openmeta/host_adoption.h"

namespace openmeta {

HostAdoptionProfile
host_adoption_profile() noexcept
{
    return kHostAdoptionProfileV1;
}

bool
host_adoption_profile_matches(const HostAdoptionProfile& expected) noexcept
{
    return host_adoption_profile() == expected;
}

}  // namespace openmeta
