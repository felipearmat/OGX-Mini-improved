#ifndef _NVS_TOOL_H_
#define _NVS_TOOL_H_

#include <atomic>
#include <cstdint>
#include <string>
#include <array>
#include <cstring>
#include <hardware/flash.h>
#include <hardware/sync.h>
#include <pico/flash.h>
#include <pico/mutex.h>

/* Define NVS_SECTORS (number of sectors to allocate to storage) either here or with CMake */

/* Custom fix: the last two of those sectors are BTstack's bank (its link keys: pico_btstack's
 * default PICO_FLASH_BANK_STORAGE_OFFSET = the last FLASH_SECTOR_SIZE * 2 of flash) on the Pico W
 * boards. NVSTool keeps its entries in the sectors before it, so formatting the settings (first
 * boot) no longer writes over the pairings and a growing list of settings never reaches them. The
 * same on every board, so the settings sit at the same place on all of them. */
#ifndef NVS_BT_BANK_SECTORS
#define NVS_BT_BANK_SECTORS 2
#endif
#if defined(PICO_FLASH_BANK_TOTAL_SIZE)
static_assert(PICO_FLASH_BANK_TOTAL_SIZE == NVS_BT_BANK_SECTORS * FLASH_SECTOR_SIZE,
              "NVSTool: BTstack's bank is not the last NVS_BT_BANK_SECTORS sectors");
#endif
static_assert(NVS_SECTORS > NVS_BT_BANK_SECTORS, "NVSTool: no sector left for the settings");

/* Custom fix: the journal, NVS_JOURNAL_SECTORS (2) sectors just below the NVS sectors (taken from
 * the top of the flash log, Board/FlashLog.cpp), so a sector rewrite a reset cuts loses nothing.
 * Every rewrite first writes the new sector's content and a header (target, CRC-32) there, then
 * rewrites the sector, then clears the header; the next boot finishes a rewrite whose header is
 * still valid. Without it a cut between erase and program left 16 settings erased (2026-10-22). */
#ifndef NVS_JOURNAL_SECTORS
#define NVS_JOURNAL_SECTORS 2
#endif
static_assert(NVS_JOURNAL_SECTORS == 2, "NVSTool: the journal is one content sector and one header sector");

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
extern "C" char __flash_binary_end;
#endif

class NVSTool
{
public:
    static constexpr size_t   KEY_LEN_MAX = 16; //Including null terminator
    static constexpr size_t   VALUE_LEN_MAX = FLASH_PAGE_SIZE - KEY_LEN_MAX;
    static constexpr uint32_t USED_SECTORS = NVS_SECTORS - NVS_BT_BANK_SECTORS;
    static constexpr uint32_t MAX_ENTRIES = ((USED_SECTORS * FLASH_SECTOR_SIZE) / FLASH_PAGE_SIZE) - 1;

    static NVSTool& get_instance()
    {
        static NVSTool instance;
        return instance;
    }

    /* Custom: usb::disconnect_all() has reset the other core (a flash write and a reboot
     * follow). Flash is then written directly: flash_safe_execute() would wait for the dead
     * core to acknowledge a lockout, time out and skip the write. */
    static void set_other_core_halted()
    {
        other_core_halted_.store(true, std::memory_order_release);
    }

    bool write(const std::string& key, const void* value, size_t len)
    {
        if (!valid_args(key, len))
        {
            return false;
        }

        mutex_enter_blocking(&nvs_mutex_);

        /* Custom fix: the key's own entry wherever it is, else the first free one. Taking the first
         * free entry before looking further wrote a second copy of a key into a hole (16 entries
         * erased by a sector rewrite cut short) in front of the old one. */
        const int at = find(key);
        const int slot = at >= 0 ? at : first_free();
        const bool ok = slot >= 0 && update_entry(static_cast<uint32_t>(slot), key, value, len);

        mutex_exit(&nvs_mutex_);
        return ok; // false: no space for a new entry
    }

    bool read(const std::string& key, void* value, size_t len)
    {
        if (!valid_args(key, len))
        {
            return false;
        }

        mutex_enter_blocking(&nvs_mutex_);

        /* Custom fix: every entry is looked at. Stopping at the first free one hid what came after
         * a hole (a sector rewrite cut short): those settings read as the defaults, though still in
         * flash (2026-11: the dongle options back to the defaults after flashing a new image). */
        const int at = find(key);
        if (at >= 0)
            std::memcpy(value, get_entry(static_cast<uint32_t>(at))->value, len);

        mutex_exit(&nvs_mutex_);
        return at >= 0;
    }

    /* Custom: the table's state, for the diagnostics report: entries in use, the first free one,
     * and how many in use come after it (more than 0: there is a hole). */
    struct Scan
    {
        uint32_t used, first_free, used_after_free;
    };
    Scan scan()
    {
        mutex_enter_blocking(&nvs_mutex_);
        Scan s{0, 0, 0};
        for (uint32_t i = 1; i < MAX_ENTRIES; ++i) {
            if (is_valid_entry(get_entry(i))) {
                ++s.used;
                if (s.first_free) ++s.used_after_free;
            } else if (!s.first_free) {
                s.first_free = i;
            }
        }
        mutex_exit(&nvs_mutex_);
        return s;
    }

    /* Custom: finish a sector rewrite a reset cut (the journal above). Runs at the first use;
     * public for the host test. Returns true when a sector was rewritten. */
    bool recover()
    {
        if (!journal_usable())
            return false;
        const JournalHeader* h = journal_header();
        if (!header_ok(*h) || crc32(journal_content(), FLASH_SECTOR_SIZE) != h->crc)
            return false;
        const uint32_t target = h->target;
        const bool differs = std::memcmp(reinterpret_cast<const void*>(XIP_BASE + target), journal_content(),
                                         FLASH_SECTOR_SIZE) != 0;
        static std::array<uint8_t, FLASH_SECTOR_SIZE> copy;  // XIP is off while the sector is written
        std::memcpy(copy.data(), journal_content(), FLASH_SECTOR_SIZE);
        bool ok = true;
        if (differs)
            ok = run_flash_op([target]() { program_sector(target, copy.data()); });
        clear_journal();
        recovered_ = differs && ok;
        return recovered_;
    }
    bool recovered_at_boot() const { return recovered_; }

    bool erase_all()
    {
        mutex_enter_blocking(&nvs_mutex_);

        const bool ok = run_flash_op([]() {
            Entry entry;
            for (uint32_t i = 0; i < USED_SECTORS; ++i)
            {
                flash_range_erase(NVS_START_OFFSET + i * FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE);
            }
            for (uint32_t i = 0; i < MAX_ENTRIES + 1; ++i)
            {
                flash_range_program(NVS_START_OFFSET + i * sizeof(Entry),
                                    reinterpret_cast<const uint8_t*>(&entry),
                                    sizeof(Entry));
            }
        });

        mutex_exit(&nvs_mutex_);
        return ok;
    }

private:
    NVSTool()
    {
        mutex_init(&nvs_mutex_);

        recover();  // a rewrite cut by a reset: finished before anything is read

        Entry* initial_entry = get_entry(0);

        if (std::strcmp(initial_entry->key, INVALID_KEY) != 0) 
        {
            erase_all();
        }
    }

    ~NVSTool() = default;
    NVSTool(const NVSTool&) = delete;
    NVSTool& operator=(const NVSTool&) = delete;

    struct Entry 
    {
        char key[KEY_LEN_MAX];
        uint8_t value[VALUE_LEN_MAX];

        Entry()
        {
            std::fill(std::begin(key), std::end(key), '\0');
            std::strncpy(key, INVALID_KEY, sizeof(key) - 1);
            std::fill(std::begin(value), std::end(value), 0xFF);
        }
    };
    static_assert(sizeof(Entry) == FLASH_PAGE_SIZE, "NVSTool::Entry size mismatch");

    static constexpr const char INVALID_KEY[KEY_LEN_MAX] = "INVALID";
    static constexpr uint32_t NVS_START_OFFSET = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE * NVS_SECTORS;

    mutex_t nvs_mutex_;
    static inline std::atomic<bool> other_core_halted_{false};
    bool recovered_ = false;

    static constexpr uint32_t JOURNAL_OFFSET = NVS_START_OFFSET - FLASH_SECTOR_SIZE * NVS_JOURNAL_SECTORS;
    static constexpr uint32_t JOURNAL_MAGIC = 0x4A53564Eu;  // "NVSJ"
    struct JournalHeader
    {
        uint32_t magic;
        uint32_t target;  // flash offset of the sector being rewritten
        uint32_t crc;     // crc32() of the content sector
        uint32_t check;   // ~(magic ^ target ^ crc)
    };

    static const uint8_t* journal_content()
    {
        return reinterpret_cast<const uint8_t*>(XIP_BASE + JOURNAL_OFFSET);
    }
    static const JournalHeader* journal_header()
    {
        return reinterpret_cast<const JournalHeader*>(XIP_BASE + JOURNAL_OFFSET + FLASH_SECTOR_SIZE);
    }
    static bool header_ok(const JournalHeader& h)
    {
        return h.magic == JOURNAL_MAGIC && h.check == ~(h.magic ^ h.target ^ h.crc) &&
               h.target >= NVS_START_OFFSET && h.target < NVS_START_OFFSET + USED_SECTORS * FLASH_SECTOR_SIZE &&
               h.target % FLASH_SECTOR_SIZE == 0;
    }
    /* The journal is below the settings, where the flash log was: not over the firmware image. */
    static bool journal_usable()
    {
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
        return reinterpret_cast<uintptr_t>(&__flash_binary_end) <= XIP_BASE + JOURNAL_OFFSET;
#else
        return true;
#endif
    }
    static uint32_t crc32(const uint8_t* data, size_t len)
    {
        uint32_t crc = 0xFFFFFFFFu;
        for (size_t i = 0; i < len; ++i)
        {
            crc ^= data[i];
            for (int b = 0; b < 8; ++b)
                crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
        return ~crc;
    }
    static void program_sector(uint32_t offset, const uint8_t* data)
    {
        flash_range_erase(offset, FLASH_SECTOR_SIZE);
        for (uint32_t i = 0; i < FLASH_SECTOR_SIZE / FLASH_PAGE_SIZE; ++i)
            flash_range_program(offset + i * FLASH_PAGE_SIZE, data + i * FLASH_PAGE_SIZE, FLASH_PAGE_SIZE);
    }
    /* Header bits cleared (no erase needed): the journal no longer counts. */
    static void clear_journal()
    {
        if (!header_ok(*journal_header()))
            return;
        /* In RAM, not const: flash_range_program() cannot read its source from flash (XIP is off
         * while it programs); a const array went to flash and hung the adapter until the watchdog. */
        static std::array<uint8_t, FLASH_PAGE_SIZE> zeros;
        zeros.fill(0);
        run_flash_op([]() { flash_range_program(JOURNAL_OFFSET + FLASH_SECTOR_SIZE, zeros.data(), FLASH_PAGE_SIZE); });
    }
#if defined(NVSTOOL_TEST_HOOKS)
public:
    static inline int test_cut_after_step = 0;  // 1: after the journal, 2: after the sector; 0: no cut
private:
#endif


    /* How long flash_safe_execute() waits for the other core to park. */
    static constexpr uint32_t SAFE_EXECUTE_TIMEOUT_MS = 500;

    /* Custom fix: run a flash erase/program sequence safely. XIP is unavailable meanwhile, so
     * nothing may execute from flash: not an IRQ handler on this core, and not the other core.
     * Interrupts off only covered this core; a write while the other core ran (Bluetooth on
     * Core1) could hard-fault it. Now:
     *  - other core already halted (mode change): interrupts off and write;
     *  - other core registered for lockout: flash_safe_execute() parks it and masks IRQs;
     *  - other core not registered (boards whose Core1 never calls flash_safe_execute_core_init
     *    / multicore_lockout_victim_init): interrupts off and write, as before;
     *  - other core does not answer the lockout: skip the write rather than crash. */
    template <typename Fn>
    static bool run_flash_op(Fn fn)
    {
        if (!other_core_halted_.load(std::memory_order_acquire))
        {
            const int rc = flash_safe_execute([](void* p) { (*static_cast<Fn*>(p))(); }, &fn,
                                              SAFE_EXECUTE_TIMEOUT_MS);
            if (rc == PICO_OK)
            {
                return true;
            }
            if (rc != PICO_ERROR_NOT_PERMITTED)
            {
                return false;
            }
        }
        const uint32_t irq_state = save_and_disable_interrupts();
        fn();
        restore_interrupts(irq_state);
        return true;
    }

    inline Entry* get_entry(const uint32_t index) 
    {
        return reinterpret_cast<Entry*>(XIP_BASE + NVS_START_OFFSET + index * sizeof(Entry));
    }

    /* Custom fix: an erased entry (key byte 0xFF) is free too. A sector rewrite cut between its
     * erase and its program (a reset at that moment) left 16 entries erased; they counted as taken
     * by a nameless key, so their settings read as defaults and every new write found the table
     * "full": settings changed in the web app came back after the restart. */
    inline bool is_valid_entry(const Entry* entry) 
    {
        return static_cast<uint8_t>(entry->key[0]) != 0xFF && std::strcmp(entry->key, INVALID_KEY) != 0;
    }

    /* The entry holding the key, or -1. Under nvs_mutex_. */
    int find(const std::string& key)
    {
        for (uint32_t i = 1; i < MAX_ENTRIES; ++i) {
            const Entry* e = get_entry(i);
            if (is_valid_entry(e) && std::strcmp(e->key, key.c_str()) == 0)
                return static_cast<int>(i);
        }
        return -1;
    }

    /* The first free entry, or -1 (full). Under nvs_mutex_. */
    int first_free()
    {
        for (uint32_t i = 1; i < MAX_ENTRIES; ++i)
            if (!is_valid_entry(get_entry(i)))
                return static_cast<int>(i);
        return -1;
    }

    inline bool valid_args(const std::string& key, size_t len)
    {
        return (key.size() < KEY_LEN_MAX - 1 && len <= sizeof(Entry::value) && std::strcmp(key.c_str(), INVALID_KEY) != 0);
    }

    bool update_entry(uint32_t index, const std::string& key, const void* buffer, size_t len)
    {
        uint32_t entry_offset = index * sizeof(Entry);
        uint32_t sector_offset = ((NVS_START_OFFSET + entry_offset) / FLASH_SECTOR_SIZE) * FLASH_SECTOR_SIZE;

        /* Custom fix: 4 KB does not fit the 2 KB core stack; keep it static (guarded by nvs_mutex_). */
        static std::array<uint8_t, FLASH_SECTOR_SIZE> sector_buffer;
        std::memcpy(sector_buffer.data(), reinterpret_cast<const uint8_t*>(XIP_BASE + sector_offset), FLASH_SECTOR_SIZE);

        /* Entry offset is relative to the sector, not to NVS_START_OFFSET. */
        Entry* entry_to_write = reinterpret_cast<Entry*>(
            sector_buffer.data() + ((NVS_START_OFFSET + entry_offset) - sector_offset));

        *entry_to_write = Entry();
        std::strncpy(entry_to_write->key, key.c_str(), key.size());
        entry_to_write->key[key.size()] = '\0';
        std::memcpy(entry_to_write->value, buffer, len);

        if (!journal_usable())
        {
            return run_flash_op([sector_offset]() { program_sector(sector_offset, sector_buffer.data()); });
        }
        /* The journal first (one flash operation each, to keep the other core's lockout short):
         * a reset at any point leaves either the old sector whole or a journal the next boot
         * applies (recover()). */
        static std::array<uint8_t, FLASH_PAGE_SIZE> header_page;
        header_page.fill(0xFF);
        JournalHeader h{JOURNAL_MAGIC, sector_offset, crc32(sector_buffer.data(), FLASH_SECTOR_SIZE), 0};
        h.check = ~(h.magic ^ h.target ^ h.crc);
        std::memcpy(header_page.data(), &h, sizeof(h));
        if (!run_flash_op([]() {
                program_sector(JOURNAL_OFFSET, sector_buffer.data());
                flash_range_erase(JOURNAL_OFFSET + FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE);
                flash_range_program(JOURNAL_OFFSET + FLASH_SECTOR_SIZE, header_page.data(), FLASH_PAGE_SIZE);
            }))
        {
            return false;
        }
#if defined(NVSTOOL_TEST_HOOKS)
        if (test_cut_after_step == 1) return true;
#endif
        if (!run_flash_op([sector_offset]() { program_sector(sector_offset, sector_buffer.data()); }))
        {
            return false;  // the journal stays: the next boot applies it
        }
#if defined(NVSTOOL_TEST_HOOKS)
        if (test_cut_after_step == 2) return true;
#endif
        clear_journal();
        return true;
    }

}; // class NVSTool

#endif // _NVS_TOOL_H_