// Claimed spin locks (Pico SDK): only 8 can be claimed (IDs 24-31), and the claim that finds none
// left panics, which in a Release build silently freezes the adapter at boot. TaskQueue and
// TinyUSB already use all of them they may need, so any other code must use a shared ("striped")
// spin lock: critical_section_init_with_lock_num(&cs, next_striped_spin_lock_num()).
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include "test.h"

namespace {

namespace fs = std::filesystem;

// Calls that claim one of the 8 spin locks.
const char* const kClaims[] = {"critical_section_init(", "spin_lock_claim_unused(", "spin_lock_claim("};

// Files allowed to claim, with the number of locks each claims at run time.
struct Allowed { const char* file; int locks; };
const Allowed kAllowed[] = {
    {"TaskQueue/TaskQueue.h", 4},    // 2 per TaskQueue, one TaskQueue per core
    {"TaskQueue/TaskQueue.cpp", 1},  // time lock, claimed on first use
};
constexpr int kTinyUsbLocks = 2;     // osal_queue_create(): device and host event queues
constexpr int kClaimable = 8;

std::string code_without_comments(const fs::path& path)
{
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string src = ss.str();
    std::string out;
    for (size_t i = 0; i < src.size(); ++i) {
        if (src.compare(i, 2, "//") == 0) {
            while (i < src.size() && src[i] != '\n') ++i;
            out += '\n';
        } else if (src.compare(i, 2, "/*") == 0) {
            const size_t end = src.find("*/", i + 2);
            i = end == std::string::npos ? src.size() : end + 1;
        } else {
            out += src[i];
        }
    }
    return out;
}

}  // namespace

TEST(only_allowed_files_claim_spin_locks) {
    const fs::path root = OGXM_FW_SRC;
    std::set<std::string> claimers;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        const std::string ext = entry.path().extension().string();
        if (ext != ".cpp" && ext != ".h" && ext != ".c" && ext != ".hpp") continue;
        const std::string rel = fs::relative(entry.path(), root).generic_string();
        if (rel.find("_wip_backup") != std::string::npos) continue;  // not built
        const std::string code = code_without_comments(entry.path());
        for (const char* claim : kClaims)
            if (code.find(claim) != std::string::npos) claimers.insert(rel);
    }
    for (const auto& file : claimers) {
        bool allowed = false;
        for (const auto& a : kAllowed) allowed |= file == a.file;
        if (!allowed)
            std::printf("  %s claims a spin lock: use critical_section_init_with_lock_num(&cs, "
                        "next_striped_spin_lock_num()) instead\n", file.c_str());
        CHECK(allowed);
    }
}

TEST(claimed_spin_locks_fit_in_the_claimable_range) {
    int locks = kTinyUsbLocks;
    for (const auto& a : kAllowed) locks += a.locks;
    CHECK(locks <= kClaimable);
}

TEST_MAIN()
