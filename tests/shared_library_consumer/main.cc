// SPDX-License-Identifier: Apache-2.0

#include <openmeta/build_info.h>

#include <string>

int main()
{
    std::string line1;
    std::string line2;
    openmeta::format_build_info_lines(&line1, &line2);
    return line1.empty() || line2.empty() ? 1 : 0;
}
