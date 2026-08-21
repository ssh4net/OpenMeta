// SPDX-License-Identifier: Apache-2.0

#include <openmeta/build_info.h>
#include <openmeta/host_adoption.h>

#include <string>

int
main()
{
    std::string line1;
    std::string line2;
    openmeta::format_build_info_lines(&line1, &line2);
    const bool profile_matches = openmeta::host_adoption_profile_matches(
        openmeta::kHostAdoptionProfileV1);
    return line1.empty() || line2.empty() || !profile_matches ? 1 : 0;
}
