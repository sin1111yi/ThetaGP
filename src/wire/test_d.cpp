/*
 * This file is a part of ThetaGP.
 */

#include "wire/test_d.h"

#include "wire/flash.h"
#include "wire/dispatch.h"
#include "drivers/device/flash/flash_base.h"
#include "drivers/device/keypad.h"
#include "drivers/device/system_timer.h"
#include "gamepad/profile/profile_store.h"
#include "pb_decode.h"
#include "utils/mem_info.h"

namespace ThetaGP::Wire {

using Drivers::Device::FlashBase;
using Drivers::Device::Keypad;
using Drivers::Device::KeypadConfig;
using Drivers::Device::SystemTimer;
using Drivers::Peripheral::BUS::Mode;
using Gamepad::Profile::ProfileStore;

#if THETAGP_CFG_HAS_FLASH
namespace {

// The widest run a flash write may program: one frame's worth of bytes.
constexpr uint32_t kWriteBytesMax = 1000;

// The bytes one flash write brings, collected out of the frame's own bytes.
// The field that carries them is a callback field, and a callback field whose
// collector is missing is dropped without a word, so what the frame carried is
// held against what the request declared rather than against whether the read
// reported a failure.
struct WriteBytes {
    uint8_t *dst;
    uint32_t capacity;
    uint32_t stored;
};

bool collectBytes(pb_istream_t *stream, const pb_field_iter_t *,
                  void **arg) {
    WriteBytes *write = static_cast<WriteBytes *>(*arg);
    const uint32_t declared = static_cast<uint32_t>(stream->bytes_left);
    const uint32_t room = write->capacity - write->stored;
    const uint32_t take = (declared < room) ? declared : room;

    if (take != 0 && !pb_read(stream, write->dst + write->stored, take)) {
        return false;
    }
    write->stored += take;

    if (stream->bytes_left != 0 &&
        !pb_read(stream, nullptr, stream->bytes_left)) {
        return false;
    }
    return true;
}

// Whether the run the request names falls inside the chip. The chip's own size
// is what bounds it: no region of the layout is named here.
bool rangeInsideChip(uint32_t addr, uint32_t len) {
    const uint32_t size = FlashBase::getInstance().getInfo().sizeBytes;
    return addr <= size && len <= size - addr;
}

} // namespace
#endif // THETAGP_CFG_HAS_FLASH

#if THETAGP_CFG_HAS_FLASH

void TestDomain::chipErase(ThetaGP_Reply &reply) {
    // The erase of the whole chip is declared and not served: reaching for it
    // would hold the command task for the minutes a chip takes.
    writeFailure(reply, ThetaGP_ErrorCode_ERR_NOT_SUPPORTED,
                 ThetaGP_Reason_REASON_NOT_IMPLEMENTED);
}

void TestDomain::eraseSector(const ThetaGP_Request &request,
                              ThetaGP_Reply &reply) {
    const uint32_t addr = request.kind.test_erase_sector.addr;
    if (addr >= FlashBase::getInstance().getInfo().sizeBytes) {
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INVALID_PARAM,
                     ThetaGP_Reason_REASON_INVALID_ADDRESS_RANGE);
        return;
    }

    const bool ok = FlashBase::getInstance().eraseSector(addr);

    // The erase changed the chip under the store, so the store is put back in
    // step with it: what it remembered about the sectors the erase reached is
    // read from the chip again.
    if (ok) {
        (void)ProfileStore::getInstance().init();
    }

    reply.which_kind = ThetaGP_Reply_test_erase_sector_tag;
    reply.kind.test_erase_sector.addr = addr;
    reply.kind.test_erase_sector.ok = ok;
}

void TestDomain::compaction(ThetaGP_Reply &reply) {
    reply.which_kind = ThetaGP_Reply_test_compaction_tag;
    reply.kind.test_compaction.ok = ProfileStore::getInstance().compaction();
}

void TestDomain::spiMode(const ThetaGP_Request &request,
                          ThetaGP_Reply &reply) {
    const uint32_t mode = request.kind.test_spi_mode.mode;
    constexpr uint32_t kModeMax = static_cast<uint32_t>(Mode::Dma);
    if (mode > kModeMax) {
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INVALID_PARAM,
                     ThetaGP_Reason_REASON_VALUE_OUT_OF_RANGE, 0, kModeMax);
        return;
    }

    FlashBase::getInstance().setSpiBusMode(static_cast<Mode>(mode));

    reply.which_kind = ThetaGP_Reply_test_spi_mode_tag;
    reply.kind.test_spi_mode.mode = mode;
}

void TestDomain::flashInfo(ThetaGP_Reply &reply) {
    const Drivers::Device::FlashInfo &info = FlashBase::getInstance().getInfo();

    reply.which_kind = ThetaGP_Reply_test_flash_info_tag;
    ThetaGP_TestFlashInfoOk &ok = reply.kind.test_flash_info;
    ok.size_bytes = info.sizeBytes;
    ok.page_size = info.pageSize;
    ok.sector_size = info.sectorSize;
    ok.init = FlashBase::getInstance().isInitialized();
}

void TestDomain::flashRead(const ThetaGP_Request &request,
                            ThetaGP_Reply &reply) {
    const uint32_t addr = request.kind.test_flash_read.addr;
    const uint32_t len = request.kind.test_flash_read.len;

    if (len == 0 || len > Flash::kRunMax) {
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INVALID_PARAM,
                     ThetaGP_Reason_REASON_INVALID_LENGTH, 1,
                     Flash::kRunMax);
        return;
    }
    if (!rangeInsideChip(addr, len)) {
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INVALID_PARAM,
                     ThetaGP_Reason_REASON_INVALID_ADDRESS_RANGE);
        return;
    }

    // The run is read now, whole, into the transfer's staging buffer; the
    // frames that carry it out follow from the transfer unit.
    if (!Flash::open(addr, len)) {
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INTERNAL,
                     ThetaGP_Reason_REASON_FLASH_READ_FAILED);
        return;
    }

    reply.which_kind = ThetaGP_Reply_test_flash_read_tag;
    reply.kind.test_flash_read.total = len;
    reply.kind.test_flash_read.addr = addr;
}

void TestDomain::flashWrite(const uint8_t *payload, uint16_t length,
                             const ThetaGP_Request &request,
                             ThetaGP_Reply &reply) {
    const uint32_t addr = request.kind.test_flash_write.addr;
    const uint32_t len = request.kind.test_flash_write.len;

    if (len == 0 || len > kWriteBytesMax) {
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INVALID_PARAM,
                     ThetaGP_Reason_REASON_INVALID_LENGTH, 1, kWriteBytesMax);
        return;
    }
    if (!rangeInsideChip(addr, len)) {
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INVALID_PARAM,
                     ThetaGP_Reason_REASON_INVALID_ADDRESS_RANGE);
        return;
    }

    // The bytes arrive in a callback field, read out of the frame's own bytes.
    // The request below is read a second time with the collector installed on
    // the arm the message already stands at, so the collector survives the read
    // that uses it.
    WriteBytes write = {Flash::staging(), Flash::kStageBytes, 0};
    ThetaGP_Request again = request;
    again.kind.test_flash_write.data.funcs.decode = collectBytes;
    again.kind.test_flash_write.data.arg = &write;
    pb_istream_t in = pb_istream_from_buffer(payload, length);
    const bool read = pb_decode_noinit(&in, ThetaGP_Request_fields, &again);

    // A write of bytes that did not all arrive is not the run the request
    // declared: the two ends of the length are reported to the arm answering
    // for it.
    if (!read || write.stored != len) {
        writeFailure(reply, ThetaGP_ErrorCode_ERR_INVALID_PARAM,
                     ThetaGP_Reason_REASON_INVALID_LENGTH,
                     static_cast<uint32_t>(write.stored), len);
        return;
    }

    const bool ok = FlashBase::getInstance().write(
        addr, Flash::staging(), static_cast<uint32_t>(len));

    // The write changed the chip under the store, so the store is put back in
    // step with it. A write that did not reach the chip changed nothing.
    if (ok) {
        (void)ProfileStore::getInstance().init();
    }

    reply.which_kind = ThetaGP_Reply_test_flash_write_tag;
    reply.kind.test_flash_write.addr = addr;
    reply.kind.test_flash_write.len = len;
    reply.kind.test_flash_write.ok = ok;
}

#endif // THETAGP_CFG_HAS_FLASH

void TestDomain::memInfo(ThetaGP_Reply &reply) {
    using namespace Util::MemInfo;

    const RegionUsage f = region(RegionId::Flash);
    const RegionUsage dt = region(RegionId::Dtcm);
    const RegionUsage ax = region(RegionId::Axi);
    const RegionUsage d2 = region(RegionId::D2);
    const RegionUsage d3 = region(RegionId::D3);
    const RegionUsage it = region(RegionId::Itcm);

    reply.which_kind = ThetaGP_Reply_test_mem_info_tag;
    ThetaGP_TestMemInfoOk &ok = reply.kind.test_mem_info;
    ok.flash_base = f.base;
    ok.flash_end = f.end;
    ok.flash_size = f.size;
    ok.flash_used = f.used;
    ok.dtcm_base = dt.base;
    ok.dtcm_end = dt.end;
    ok.dtcm_size = dt.size;
    ok.dtcm_used = dt.used;
    ok.axi_base = ax.base;
    ok.axi_end = ax.end;
    ok.axi_size = ax.size;
    ok.axi_used = ax.used;
    ok.d2_base = d2.base;
    ok.d2_end = d2.end;
    ok.d2_size = d2.size;
    ok.d2_used = d2.used;
    ok.d3_base = d3.base;
    ok.d3_end = d3.end;
    ok.d3_size = d3.size;
    ok.d3_used = d3.used;
    ok.itcm_base = it.base;
    ok.itcm_end = it.end;
    ok.itcm_size = it.size;
    ok.itcm_used = it.used;
    ok.ram_reserved_bytes = ramReservedBytes();
    ok.stack_bytes = stackBytes();
    ok.heap_bytes = heapBytes();
    ok.ram_live_bytes = ramLiveBytes();
}

void TestDomain::keypadScan(ThetaGP_Reply &reply) {
    SystemTimer &timer = SystemTimer::getInstance();

    Keypad::ScanStats stats;
    Keypad::getInstance().getScanStats(stats);

    Keypad::CommitStats commits;
    Keypad::getInstance().getCommitStats(commits);

    reply.which_kind = ThetaGP_Reply_test_keypad_scan_tag;
    ThetaGP_TestKeypadScanOk &ok = reply.kind.test_keypad_scan;
    ok.scan_hz = KeypadConfig::DEFAULT_SCAN_FREQ;
    ok.drive_lines = static_cast<uint32_t>(Keypad::getDriveLineCount());
    ok.sense_lines = static_cast<uint32_t>(Keypad::getSenseLineCount());
    ok.cycles_per_us = timer.microsToCycles(1);
    ok.count = stats.count;
    ok.last_cycles = stats.last_cycles;
    ok.max_cycles = stats.max_cycles;
    ok.last_us = static_cast<uint32_t>(
        timer.cyclesToMicros(static_cast<int32_t>(stats.last_cycles)));
    ok.max_us = static_cast<uint32_t>(
        timer.cyclesToMicros(static_cast<int32_t>(stats.max_cycles)));
    ok.commit_count = commits.count;
    // The driver holds lateness in scans and does not divide; the conversion
    // belongs to the readout, where the scan rate is a constant and this runs
    // once per command.
    ok.max_commit_latency_us =
        static_cast<uint32_t>(commits.max_latency_scans) * 1000000UL /
        KeypadConfig::DEFAULT_SCAN_FREQ;
    ok.sum_cycles = stats.sum_cycles;
}

} // namespace ThetaGP::Wire
