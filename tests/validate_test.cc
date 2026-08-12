// SPDX-License-Identifier: Apache-2.0

#include "openmeta/validate.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#    include <io.h>
#elif defined(_POSIX_VERSION)
#    include <sys/types.h>
#endif

namespace openmeta {
namespace {

    class ScopedFile final {
    public:
        explicit ScopedFile(std::string path)
            : path_(std::move(path))
        {
        }

        ScopedFile(const ScopedFile&)            = delete;
        ScopedFile& operator=(const ScopedFile&) = delete;

        ScopedFile(ScopedFile&& other) noexcept
            : path_(std::move(other.path_))
        {
            other.path_.clear();
        }

        ScopedFile& operator=(ScopedFile&& other) noexcept
        {
            if (this != &other) {
                cleanup();
                path_ = std::move(other.path_);
                other.path_.clear();
            }
            return *this;
        }

        ~ScopedFile() { cleanup(); }

        const std::string& path() const noexcept { return path_; }

    private:
        void cleanup() noexcept
        {
            if (!path_.empty()) {
                (void)std::remove(path_.c_str());
                path_.clear();
            }
        }

        std::string path_;
    };


    static std::string temp_root()
    {
        const char* tmp = std::getenv("TMPDIR");
        if (!tmp || !*tmp) {
            tmp = std::getenv("TEMP");
        }
        if (!tmp || !*tmp) {
            tmp = std::getenv("TMP");
        }
        if (!tmp || !*tmp) {
            tmp = "/tmp";
        }
        return std::string(tmp);
    }


    static std::string make_temp_path(const char* suffix)
    {
        static uint64_t seq = 0;
        seq += 1U;

        const uint64_t now = static_cast<uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());

        std::string path = temp_root();
        if (!path.empty() && path.back() != '/' && path.back() != '\\') {
            path.push_back('/');
        }

        char name[160];
        std::snprintf(name, sizeof(name), "openmeta_validate_%llu_%llu%s",
                      static_cast<unsigned long long>(now),
                      static_cast<unsigned long long>(seq),
                      suffix ? suffix : "");
        path.append(name);
        return path;
    }


    static bool write_bytes_file(const std::string& path,
                                 std::span<const std::byte> bytes)
    {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) {
            return false;
        }
        if (!bytes.empty()) {
            const size_t n = std::fwrite(bytes.data(), 1, bytes.size(), f);
            if (n != bytes.size()) {
                std::fclose(f);
                return false;
            }
        }
        return std::fclose(f) == 0;
    }


    static bool write_text_file(const std::string& path, std::string_view text)
    {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) {
            return false;
        }
        if (!text.empty()) {
            const size_t n = std::fwrite(text.data(), 1, text.size(), f);
            if (n != text.size()) {
                std::fclose(f);
                return false;
            }
        }
        return std::fclose(f) == 0;
    }


    static bool write_sparse_file(const std::string& path, uint64_t size)
    {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) {
            return false;
        }
        if (size == 0U) {
            return std::fclose(f) == 0;
        }

        const uint64_t last = size - 1U;
#if defined(_WIN32)
        if (last > static_cast<uint64_t>(std::numeric_limits<__int64>::max())
            || ::_fseeki64(f, static_cast<__int64>(last), SEEK_SET) != 0) {
            std::fclose(f);
            return false;
        }
#elif defined(_POSIX_VERSION)
        if (last > static_cast<uint64_t>(std::numeric_limits<off_t>::max())
            || ::fseeko(f, static_cast<off_t>(last), SEEK_SET) != 0) {
            std::fclose(f);
            return false;
        }
#else
        if (last > static_cast<uint64_t>(std::numeric_limits<long>::max())
            || std::fseek(f, static_cast<long>(last), SEEK_SET) != 0) {
            std::fclose(f);
            return false;
        }
#endif

        static constexpr unsigned char kZero = 0U;
        if (std::fwrite(&kZero, 1, 1, f) != 1U) {
            std::fclose(f);
            return false;
        }
        return std::fclose(f) == 0;
    }


    static std::vector<std::byte> make_minimal_jpeg()
    {
        return {
            std::byte { 0xFF },
            std::byte { 0xD8 },
            std::byte { 0xFF },
            std::byte { 0xD9 },
        };
    }


    static std::vector<std::byte> make_jpeg_with_truncated_exif_ifd()
    {
        // JPEG with APP1 Exif payload containing a TIFF header with a truncated
        // IFD entry count field.
        return {
            std::byte { 0xFF }, std::byte { 0xD8 }, std::byte { 0xFF },
            std::byte { 0xE1 }, std::byte { 0x00 }, std::byte { 0x11 },
            std::byte { 0x45 }, std::byte { 0x78 }, std::byte { 0x69 },
            std::byte { 0x66 }, std::byte { 0x00 }, std::byte { 0x00 },
            std::byte { 0x49 }, std::byte { 0x49 }, std::byte { 0x2A },
            std::byte { 0x00 }, std::byte { 0x08 }, std::byte { 0x00 },
            std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x01 },
            std::byte { 0xFF }, std::byte { 0xD9 },
        };
    }


    static std::vector<std::byte> make_jpeg_with_com_segments(uint32_t count)
    {
        std::vector<std::byte> bytes;
        bytes.reserve(static_cast<size_t>(count) * 4U + 4U);
        bytes.push_back(std::byte { 0xFF });
        bytes.push_back(std::byte { 0xD8 });
        for (uint32_t i = 0; i < count; ++i) {
            bytes.push_back(std::byte { 0xFF });
            bytes.push_back(std::byte { 0xFE });
            bytes.push_back(std::byte { 0x00 });
            bytes.push_back(std::byte { 0x02 });
        }
        bytes.push_back(std::byte { 0xFF });
        bytes.push_back(std::byte { 0xD9 });
        return bytes;
    }


    static bool has_issue(const ValidateResult& result,
                          std::string_view category, std::string_view code)
    {
        for (size_t i = 0; i < result.issues.size(); ++i) {
            const ValidateIssue& issue = result.issues[i];
            if (issue.category == category && issue.code == code) {
                return true;
            }
        }
        return false;
    }

}  // namespace

TEST(ValidateFile, ReturnsOpenFailedForMissingPath)
{
    const std::string path = make_temp_path(".jpg");
    (void)std::remove(path.c_str());

    const ValidateResult result = validate_file(path.c_str(),
                                                ValidateOptions {});
    EXPECT_EQ(result.status, ValidateStatus::OpenFailed);
    EXPECT_TRUE(result.failed);
    EXPECT_GE(result.error_count, 1U);
    EXPECT_TRUE(has_issue(result, "file", "open_failed"));
}

TEST(ValidateFile, EnforcesMaxFileBytes)
{
    const std::string path = make_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(path, make_minimal_jpeg()));
    const ScopedFile cleanup(path);

    ValidateOptions options;
    options.policy.max_file_bytes = 3U;
    const ValidateResult result   = validate_file(path.c_str(), options);
    EXPECT_EQ(result.status, ValidateStatus::TooLarge);
    EXPECT_TRUE(result.failed);
    EXPECT_EQ(result.file_size, 4U);
    EXPECT_GE(result.error_count, 1U);
    EXPECT_TRUE(has_issue(result, "file", "too_large"));
}

TEST(ValidateFile, ReportsMalformedExifAsError)
{
    const std::string path = make_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(path, make_jpeg_with_truncated_exif_ifd()));
    const ScopedFile cleanup(path);

    const ValidateResult result = validate_file(path.c_str(),
                                                ValidateOptions {});
    EXPECT_EQ(result.status, ValidateStatus::Ok);
    EXPECT_EQ(result.read.exif.status, ExifDecodeStatus::Malformed);
    EXPECT_TRUE(result.failed);
    EXPECT_GE(result.error_count, 1U);
    EXPECT_TRUE(has_issue(result, "exif", "malformed"));
}


TEST(ValidateFile, ExplicitC2paVerificationFailsWhenNoManifestWasEvaluated)
{
    const std::string path = make_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(path, make_minimal_jpeg()));
    const ScopedFile cleanup(path);

    ValidateOptions options;
    options.verify_c2pa         = true;
    const ValidateResult result = validate_file(path.c_str(), options);

    EXPECT_EQ(result.status, ValidateStatus::Ok);
    EXPECT_EQ(result.read.jumbf.verify_status, C2paVerifyStatus::NotRequested);
    EXPECT_TRUE(result.failed);
    EXPECT_GE(result.error_count, 1U);
    EXPECT_TRUE(has_issue(result, "c2pa", "not_requested"));
}

TEST(ValidateFile, ReportsLargeSparseFileSize)
{
    const std::string path                = make_temp_path(".bin");
    static constexpr uint64_t kSparseSize = 0x80000010ULL;
    ASSERT_TRUE(write_sparse_file(path, kSparseSize));
    const ScopedFile cleanup(path);

    ValidateOptions options;
    options.policy.max_file_bytes = 1U;

    const ValidateResult result = validate_file(path.c_str(), options);
    EXPECT_EQ(result.status, ValidateStatus::TooLarge);
    EXPECT_TRUE(result.failed);
    EXPECT_EQ(result.file_size, kSparseSize);
    EXPECT_TRUE(has_issue(result, "file", "too_large"));
}

TEST(ValidateFile, WarningsAsErrorsPromotesFailure)
{
    const std::string jpeg_path = make_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(jpeg_path, make_minimal_jpeg()));
    const ScopedFile cleanup_jpeg(jpeg_path);

    const std::string sidecar_path = jpeg_path.substr(0, jpeg_path.size() - 4)
                                     + ".xmp";
    ASSERT_TRUE(write_text_file(sidecar_path, "<x:xmpmeta><rdf:RDF>"));
    const ScopedFile cleanup_sidecar(sidecar_path);

    ValidateOptions loose;
    loose.include_xmp_sidecar        = true;
    const ValidateResult warn_result = validate_file(jpeg_path.c_str(), loose);
    EXPECT_EQ(warn_result.status, ValidateStatus::Ok);
    EXPECT_FALSE(warn_result.failed);
    EXPECT_EQ(warn_result.error_count, 0U);
    EXPECT_GE(warn_result.warning_count, 1U);
    EXPECT_EQ(warn_result.read.xmp.status, XmpDecodeStatus::OutputTruncated);
    EXPECT_TRUE(has_issue(warn_result, "xmp", "output_truncated"));
    EXPECT_TRUE(has_issue(warn_result, "xmp", "invalid_or_malformed_xml_text"));

    ValidateOptions strict             = loose;
    strict.warnings_as_errors          = true;
    const ValidateResult strict_result = validate_file(jpeg_path.c_str(),
                                                       strict);
    EXPECT_EQ(strict_result.status, ValidateStatus::Ok);
    EXPECT_TRUE(strict_result.failed);
    EXPECT_EQ(strict_result.error_count, 0U);
    EXPECT_GE(strict_result.warning_count, 1U);
}

TEST(ValidateFile, RejectsExcessiveScanScratchGrowth)
{
    const std::string path = make_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(path, make_jpeg_with_com_segments(131073U)));
    const ScopedFile cleanup(path);

    const ValidateResult result = validate_file(path.c_str(),
                                                ValidateOptions {});
    EXPECT_EQ(result.status, ValidateStatus::ReadFailed);
    EXPECT_TRUE(result.failed);
    EXPECT_GE(result.error_count, 1U);
    EXPECT_TRUE(has_issue(result, "scan", "scratch_limit_exceeded"));
}

}  // namespace openmeta
