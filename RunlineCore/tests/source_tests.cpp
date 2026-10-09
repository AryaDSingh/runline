#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

// Unreal compiles with /utf-8, and stray Windows-1252 bytes in a header are a build warning or
// error there. Keeping RunlineCore's sources plain ASCII sidesteps encodings entirely.
TEST_CASE("RunlineCore sources are plain ASCII") {
    namespace fs = std::filesystem;
    int checked = 0;
    for (const char* dir : {"include", "src"}) {
        for (const auto& e : fs::recursive_directory_iterator(fs::path(RUNLINE_SOURCE_DIR) / dir)) {
            if (!e.is_regular_file()) continue;
            std::ifstream in(e.path(), std::ios::binary);
            const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            ++checked;
            for (size_t i = 0; i < bytes.size(); ++i)
                if (static_cast<unsigned char>(bytes[i]) > 127) {
                    FAIL_CHECK(e.path().filename().string() << " has a non-ASCII byte at offset " << i);
                    break;
                }
        }
    }
    CHECK(checked >= 8);
}
