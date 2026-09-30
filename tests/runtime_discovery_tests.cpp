// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include <functional>
#include <iostream>

#include "../src/runtime_discovery.cpp"

using namespace DishonoredHeadTracking;
namespace {
unsigned checks = 0;
void Check(bool value, const char* what) {
    ++checks;
    if (!value) throw std::runtime_error(what);
}
void Reject(const std::function<void()>& operation, const char* what) {
    bool rejected = false;
    try {
        operation();
    } catch (const Unavailable&) {
        rejected = true;
    }
    Check(rejected, what);
}
struct Fixture {
    std::uint8_t* allocation;
    std::uint32_t base;
    std::vector<std::uint8_t> bytes;
    Fixture()
        : allocation(static_cast<std::uint8_t*>(VirtualAlloc(
              nullptr, 0x10000, MEM_RESERVE | MEM_COMMIT | MEM_TOP_DOWN, PAGE_READWRITE))),
          base(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(allocation))),
          bytes(0x6000) {
        if (!allocation) throw std::runtime_error("VirtualAlloc failed");
        IMAGE_DOS_HEADER dos{};
        dos.e_magic = IMAGE_DOS_SIGNATURE;
        dos.e_lfanew = 0x100;
        Put(0, dos);
        IMAGE_NT_HEADERS32 nt{};
        nt.Signature = IMAGE_NT_SIGNATURE;
        nt.FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
        nt.FileHeader.NumberOfSections = 2;
        nt.FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER32);
        nt.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
        nt.OptionalHeader.SizeOfImage = static_cast<DWORD>(bytes.size());
        Put(0x100, nt);
        IMAGE_SECTION_HEADER section{};
        section.VirtualAddress = 0x1000;
        section.Misc.VirtualSize = 0x1000;
        section.Characteristics = IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE;
        Put(0x100 + sizeof(nt), section);
        section.VirtualAddress = 0x3000;
        section.Misc.VirtualSize = 0x2000;
        section.Characteristics = IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE;
        Put(0x100 + sizeof(nt) + sizeof(section), section);
        Put(0x3000, base + 0x6000);
        Put(0x3004, 128u);
        Put(0x3008, 128u);
        const char* names[] = {"None",
                               "ByteProperty",
                               "IntProperty",
                               "BoolProperty",
                               "FloatProperty",
                               "ObjectProperty",
                               "NameProperty",
                               "DelegateProperty",
                               "ClassProperty",
                               "ArrayProperty",
                               "StructProperty",
                               "VectorProperty",
                               "RotatorProperty",
                               "StrProperty",
                               "MapProperty",
                               "InterfaceProperty",
                               "FixtureOwner",
                               "CameraFov",
                               "Class",
                               "Engine"};
        for (unsigned i = 0; i < 20; ++i) {
            auto entry = base + 0x7000 + i * 128;
            Live(0x6000 + i * 4, entry);
            Live(0x7000 + i * 128 + 8, i * 2);
            std::memcpy(allocation + 0x7000 + i * 128 + 16, names[i], std::strlen(names[i]) + 1);
        }
        Put(0x3500 + 0x28, 16u);
        Put(0x3500 + 0x48, base + 0x3600);
        Put(0x3500 + 0x4c, 0x200u);
        Put(0x3600 + 0x28, 17u);
        Put(0x3600 + 0x30, base + 0x3700);
        Put(0x3600 + 0x24, base + 0x3500);
        Put(0x3600 + 0x3c, 1u);
        Put(0x3600 + 0x40, 4u);
        Put(0x3600 + 0x5c, 0x80u);
        Put(0x3700 + 0x28, 4u);
        std::memcpy(bytes.data() + 0x3100, "ToyexecView", 12);
        Put(0x3200, base + 0x3100);
        Put(0x3204, base + 0x1100);
        const std::uint8_t code[] = {0x55, 0x8b, 0xec, 0xe8, 0, 0, 0, 0, 0x5d, 0xc2, 8, 0};
        std::memcpy(bytes.data() + 0x1100, code, sizeof(code));
        Put(0x1104, 0x1200u - 0x1108u);
        bytes[0x1200] = 0xc3;
        Sync();
    }
    ~Fixture() { VirtualFree(allocation, 0, MEM_RELEASE); }
    template <class T>
    void Put(std::uint32_t offset, T value) {
        std::memcpy(bytes.data() + offset, &value, sizeof(value));
    }
    template <class T>
    void Live(std::uint32_t offset, T value) {
        std::memcpy(allocation + offset, &value, sizeof(value));
    }
    void Sync() { std::memcpy(allocation, bytes.data(), bytes.size()); }
    Property Field() {
        Image image(base, bytes);
        Memory memory{GetCurrentProcess()};
        Metadata meta(image, memory);
        meta.names = base + 0x3000;
        return meta.Field(base + 0x3500, "CameraFov", "FloatProperty", 4);
    }
};
void Run() {
    Fixture f;
    Check(f.base > 0x80000000u, "fixture must exercise large-address-aware pointers");
    Image image(f.base, f.bytes);
    Check(image.Native("ToyexecView") == f.base + 0x1100, "writable native registration");
    Check(image.Direct("ToyexecView") == f.base + 0x1200, "relative native call");
    Check(image.Function(f.base + 0x1100).back().h.imm.imm16 == 8, "x86 stack cleanup");
    Reject([&] { image.At<std::uint32_t>(f.base - 1); }, "read before image rejected");
    Reject([&] { image.At<std::uint32_t>(f.base + 0x5ffe); }, "partial image read rejected");
    Reject([&] { image.Decode(f.base + 0x3000); }, "data cannot be decoded as code");
    auto shortImage = f.bytes;
    shortImage.resize(12);
    Reject([&] { Image bad(f.base, shortImage); }, "short DOS header rejected");
    auto saved = f.bytes;
    f.Put(0x100 + 4, static_cast<WORD>(IMAGE_FILE_MACHINE_AMD64));
    Reject([&] { Image bad(f.base, f.bytes); }, "wrong architecture rejected");
    f.bytes = saved;
    f.Put(0x100 + sizeof(IMAGE_NT_HEADERS32) + 8, 0xfffffff0u);
    Reject([&] { Image bad(f.base, f.bytes); }, "overflowing section rejected");
    f.bytes = saved;
    f.bytes[0x1fff] = 0x0f;
    Image truncated(f.base, f.bytes);
    Reject([&] { truncated.Decode(f.base + 0x1fff); }, "truncated instruction rejected");
    f.bytes = saved;
    f.Put(0x3210, f.base + 0x3100);
    f.Put(0x3214, f.base + 0x1200);
    Image duplicate(f.base, f.bytes);
    Reject([&] { duplicate.Native("ToyexecView"); }, "ambiguous native rejected");
    f.bytes = saved;
    f.Put(0x3204, f.base + 0x3500);
    Image notCode(f.base, f.bytes);
    Reject([&] { notCode.Native("ToyexecView"); }, "native target must be executable");
    f.bytes = saved;
    Check(f.Field().offset == 0x80, "typed reflected property");
    f.Live(0x3600 + 0x5c, 0xc0u);
    Check(f.Field().offset == 0xc0, "member offset discovered after movement");
    f.Sync();
    Memory memory{GetCurrentProcess()};
    std::memcpy(f.allocation + 0x7000 + 17 * 128 + 16, "camerafov", 10);
    Check(f.Field().offset == 0x80, "FName property lookup is case-insensitive");
    std::memcpy(f.allocation + 0x7000 + 17 * 128 + 16, "CameraFov", 10);
    Metadata metadata(image, memory);
    metadata.FindNames();
    Check(metadata.names == f.base + 0x3000, "flat name table discovered");
    Check(metadata.Name(4) == "FloatProperty", "name index resolved");
    f.Live(0x7000 + 4 * 128 + 8, 11u);
    Metadata badIndex(image, memory);
    badIndex.names = f.base + 0x3000;
    Reject([&] { badIndex.Name(4); }, "name entry index checked");
    f.Live(0x7000 + 4 * 128 + 8, 8u);
    f.Live(0x7000 + 4 * 128 + 8, 9u);
    const wchar_t wide[] = L"FloatProperty";
    std::memcpy(f.allocation + 0x7000 + 4 * 128 + 16, wide, sizeof(wide));
    Metadata unicode(image, memory);
    unicode.names = f.base + 0x3000;
    Check(unicode.Name(4) == "FloatProperty", "wide name entry resolved");
    f.Live(0x7000 + 4 * 128 + 8, 8u);
    std::memset(f.allocation + 0x7000 + 4 * 128 + 16, 0, 112);
    std::memcpy(f.allocation + 0x7000 + 4 * 128 + 16, "FloatProperty", 14);
    f.Live(0x3600 + 0x24, f.base + 0x3504);
    Reject([&] { f.Field(); }, "wrong property owner rejected");
    f.Sync();
    f.Live(0x3700 + 0x28, 2u);
    Reject([&] { f.Field(); }, "integer does not satisfy float property");
    f.Sync();
    f.Live(0x3600 + 0x3c, 2u);
    Reject([&] { f.Field(); }, "array does not satisfy scalar field");
    f.Sync();
    f.Live(0x3600 + 0x40, 8u);
    Reject([&] { f.Field(); }, "wrong element width rejected");
    f.Sync();
    f.Live(0x3600 + 0x5c, 0x1feu);
    Reject([&] { f.Field(); }, "partial field outside owner rejected");
    f.Sync();
    f.Live(0x3600 + 0x5c, 0xfffffffcu);
    Reject([&] { f.Field(); }, "overflowing member offset rejected");
    f.Sync();
    f.Live(0x3600 + 0x38, f.base + 0x3600);
    Reject([&] { f.Field(); }, "cyclic property list rejected");
    f.Sync();
    std::memcpy(f.allocation + 0x3800, f.allocation + 0x3600, 0x70);
    f.Live(0x3600 + 0x38, f.base + 0x3800);
    Reject([&] { f.Field(); }, "duplicate property rejected");
    f.Sync();
    f.Live(0x3500 + 0x48, 0u);
    Reject([&] { f.Field(); }, "missing field rejected");
    f.Sync();
    f.Live(0x3600 + 0x38, 0x10000u);
    Reject([&] { f.Field(); }, "unreadable property chain rejected");
    f.Sync();
    f.Live(0x3004, 0xffffffffu);
    Metadata oversized(image, memory);
    oversized.names = f.base + 0x3000;
    Reject([&] { oversized.Name(4); }, "name table count bounded");
    f.Sync();
    f.Live(0x3700 + 0x28, 3u);
    f.Live(0x3600 + 0x6c, 0x80000000u);
    Metadata bits(image, memory);
    bits.names = f.base + 0x3000;
    Check(bits.Bool(f.base + 0x3500, "CameraFov").second == 0x80000000u,
          "high boolean bit preserved");
    f.Live(0x3600 + 0x6c, 3u);
    Reject([&] { bits.Bool(f.base + 0x3500, "CameraFov"); }, "multi-bit boolean mask rejected");
}
}  // namespace
int main() {
    try {
        Run();
        std::cout << checks << " discovery checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
