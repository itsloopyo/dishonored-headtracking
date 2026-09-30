// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "runtime_discovery.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>

#include "hde32.h"
namespace DishonoredHeadTracking {
namespace {
class Unavailable : public std::runtime_error {
   public:
    using std::runtime_error::runtime_error;
};
void Need(bool ok, const char* why) {
    if (!ok) throw Unavailable(why);
}
bool SameName(const std::string& value, const char* expected) {
    return _stricmp(value.c_str(), expected) == 0;
}
struct Memory {
    HANDLE process;
    template <class T>
    bool Try(std::uint32_t at, T& value) const {
        SIZE_T got = 0;
        return at >= 0x10000 && at <= UINT32_MAX - sizeof(T) &&
               ReadProcessMemory(process,
                                 reinterpret_cast<const void*>(static_cast<std::uintptr_t>(at)),
                                 &value, sizeof(T), &got) &&
               got == sizeof(T);
    }
    template <class T>
    T Read(std::uint32_t at) const {
        T value{};
        Need(Try(at, value), "unreadable live metadata");
        return value;
    }
};
struct Section {
    std::uint32_t low, high, flags;
};
struct Ins {
    std::uint32_t at;
    hde32s h{};
    std::uint32_t End() const { return at + h.len; }
    bool Call() const { return h.opcode == 0xe8 && h.len == 5 && !h.p_66 && !h.p_67; }
    std::uint32_t Target() const { return End() + h.imm.imm32; }
    bool Ret() const { return h.opcode == 0xc2 || h.opcode == 0xc3; }
    int Disp() const {
        return h.flags & F_DISP8 ? static_cast<std::int8_t>(h.disp.disp8)
                                 : static_cast<int>(h.disp.disp32);
    }
    bool Mem(unsigned base) const {
        return !h.p_66 && !h.p_67 && !h.p_seg && !h.p_lock && h.modrm_mod != 3 &&
               h.modrm_rm == base && (base != 5 || h.modrm_mod != 0) &&
               (base != 4 || (h.sib_index == 4 && h.sib_base == 4));
    }
};
class Image {
   public:
    std::uint32_t base;
    const std::vector<std::uint8_t>& bytes;
    std::vector<Section> sections;
    Image(std::uint32_t b, const std::vector<std::uint8_t>& data) : base(b), bytes(data) {
        Need(bytes.size() >= sizeof(IMAGE_DOS_HEADER), "short image");
        auto dos = At<IMAGE_DOS_HEADER>(base);
        Need(dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew > 0, "invalid DOS header");
        auto nt = At<IMAGE_NT_HEADERS32>(base + static_cast<std::uint32_t>(dos.e_lfanew));
        Need(nt.Signature == IMAGE_NT_SIGNATURE &&
                 nt.FileHeader.Machine == IMAGE_FILE_MACHINE_I386 &&
                 nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC,
             "expected PE32 x86 image");
        Need(nt.OptionalHeader.SizeOfImage == bytes.size() && bytes.size() <= UINT32_MAX - base,
             "image bounds mismatch");
        auto sh = base + static_cast<std::uint32_t>(dos.e_lfanew) + 24 +
                  nt.FileHeader.SizeOfOptionalHeader;
        Need(nt.FileHeader.NumberOfSections > 0 && nt.FileHeader.NumberOfSections < 96,
             "section count invalid");
        for (unsigned i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
            auto s = At<IMAGE_SECTION_HEADER>(sh + i * sizeof(IMAGE_SECTION_HEADER));
            Need(s.VirtualAddress < bytes.size() &&
                     s.Misc.VirtualSize <= bytes.size() - s.VirtualAddress,
                 "section outside image");
            sections.push_back({base + s.VirtualAddress,
                                base + s.VirtualAddress + s.Misc.VirtualSize, s.Characteristics});
        }
    }
    template <class T>
    T At(std::uint32_t address) const {
        T value{};
        Need(address >= base && address - base <= bytes.size() &&
                 sizeof(T) <= bytes.size() - (address - base),
             "image read outside bounds");
        std::memcpy(&value, bytes.data() + address - base, sizeof(T));
        return value;
    }
    bool In(std::uint32_t address, std::size_t size, std::uint32_t required,
            std::uint32_t excluded = 0) const {
        for (const auto& s : sections)
            if (address >= s.low && address < s.high && size <= s.high - address &&
                (s.flags & required) == required && !(s.flags & excluded))
                return true;
        return false;
    }
    bool Code(std::uint32_t a, std::size_t size = 1) const {
        return In(a, size, IMAGE_SCN_MEM_EXECUTE);
    }
    Ins Decode(std::uint32_t at) const {
        Need(Code(at), "instruction outside executable section");
        std::uint8_t scratch[32]{};
        std::size_t available = 0;
        for (const auto& s : sections)
            if (at >= s.low && at < s.high) {
                available = std::min<std::size_t>(15, s.high - at);
                break;
            }
        std::memcpy(scratch, bytes.data() + at - base, available);
        Ins i{at, {}};
        hde32_disasm(scratch, &i.h);
        Need(!(i.h.flags & F_ERROR) && i.h.len && i.h.len <= available,
             "invalid or truncated x86 instruction");
        return i;
    }
    std::vector<Ins> Function(std::uint32_t at, std::uint32_t limit = 8192) const {
        std::vector<Ins> result;
        auto start = at;
        while (at - start < limit) {
            auto i = Decode(at);
            result.push_back(i);
            if (i.Ret()) return result;
            at = i.End();
        }
        throw Unavailable("function has no bounded return");
    }
    std::vector<std::uint32_t> Strings(const char* name) const {
        std::vector<std::uint32_t> found;
        auto len = std::strlen(name) + 1;
        for (const auto& s : sections)
            if ((s.flags & IMAGE_SCN_MEM_READ) && !(s.flags & IMAGE_SCN_MEM_EXECUTE)) {
                for (auto a = s.low; a < s.high && len <= s.high - a; ++a)
                    if ((a == s.low || At<std::uint8_t>(a - 1) == 0) &&
                        !std::memcmp(bytes.data() + a - base, name, len))
                        found.push_back(a);
            }
        return found;
    }
    std::uint32_t Native(const char* name) const {
        std::set<std::uint32_t> targets;
        for (auto str : Strings(name))
            for (const auto& s : sections)
                if ((s.flags & IMAGE_SCN_MEM_READ) && !(s.flags & IMAGE_SCN_MEM_EXECUTE)) {
                    for (auto a = (s.low + 3) & ~3u; a < s.high && 8 <= s.high - a; a += 4)
                        if (At<std::uint32_t>(a) == str && Code(At<std::uint32_t>(a + 4)))
                            targets.insert(At<std::uint32_t>(a + 4));
                }
        Need(targets.size() == 1, "native registration missing or ambiguous");
        return *targets.begin();
    }
    std::vector<Ins> Calls(std::uint32_t fn) const {
        auto ins = Function(fn);
        std::vector<Ins> out;
        for (auto i : ins)
            if (i.Call()) {
                Need(Code(i.Target()), "native call leaves image");
                out.push_back(i);
            }
        return out;
    }
    std::uint32_t Direct(const char* name) const {
        auto c = Calls(Native(name));
        Need(c.size() == 1, "native wrapper does not have one direct call");
        return c[0].Target();
    }
};
struct Property {
    std::uint32_t offset = 0, size = 0, inner = 0, address = 0;
};
class Metadata {
    const Image& image;
    const Memory& mem;
    mutable std::map<std::uint32_t, std::string> nameCache;

   public:
    std::uint32_t names = 0;
    Metadata(const Image& i, const Memory& m) : image(i), mem(m) {}
    std::string Name(std::uint32_t id) const {
        auto known = nameCache.find(id);
        if (known != nameCache.end()) return known->second;
        auto data = mem.Read<std::uint32_t>(names), count = mem.Read<std::uint32_t>(names + 4),
             cap = mem.Read<std::uint32_t>(names + 8);
        Need(data >= 0x10000 && !(data & 3) && count && count <= 2000000 && cap >= count &&
                 cap <= 4000000 && id < count && count <= (UINT32_MAX - data) / 4,
             "invalid flat name array");
        auto entry = mem.Read<std::uint32_t>(data + id * 4);
        auto header = mem.Read<std::uint32_t>(entry + 8);
        Need((header >> 1) == id, "name entry index mismatch");
        std::string result;
        for (unsigned j = 0; j < 256; ++j) {
            auto ch = (header & 1) ? mem.Read<std::uint16_t>(entry + 16 + j * 2)
                                   : mem.Read<std::uint8_t>(entry + 16 + j);
            if (!ch) {
                nameCache.emplace(id, result);
                return result;
            }
            Need(ch < 128, "non-ASCII metadata identifier");
            result += static_cast<char>(ch);
        }
        throw Unavailable("unterminated metadata identifier");
    }
    std::string ObjectName(std::uint32_t obj) const {
        Need(obj && obj <= UINT32_MAX - 0x80, "invalid metadata object");
        return Name(mem.Read<std::uint32_t>(obj + 0x28));
    }
    bool Type(std::uint32_t obj, const char* name, const char* kind, const char* package) const {
        return SameName(ObjectName(obj), name) &&
               SameName(ObjectName(mem.Read<std::uint32_t>(obj + 0x30)), kind) &&
               SameName(ObjectName(mem.Read<std::uint32_t>(obj + 0x24)), package);
    }
    void FindNames() {
        const char* expected[] = {"None",
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
                                  "InterfaceProperty"};
        std::vector<std::uint32_t> found;
        for (const auto& s : image.sections)
            if ((s.flags & IMAGE_SCN_MEM_WRITE) && !(s.flags & IMAGE_SCN_MEM_EXECUTE))
                for (auto a = (s.low + 3) & ~3u; a < s.high && 12 <= s.high - a; a += 4) {
                    auto ptr = image.At<std::uint32_t>(a), count = image.At<std::uint32_t>(a + 4),
                         cap = image.At<std::uint32_t>(a + 8);
                    if (ptr < 0x10000 || (ptr & 3) || count < 100 || count > 2000000 ||
                        cap < count || cap > 4000000)
                        continue;
                    names = a;
                    nameCache.clear();
                    bool valid = true;
                    for (unsigned k = 0; k < 16; ++k) {
                        try {
                            if (!SameName(Name(k), expected[k])) {
                                valid = false;
                                break;
                            }
                        } catch (const Unavailable&) {
                            valid = false;
                            break;
                        }
                    }
                    if (valid) found.push_back(a);
                }
        Need(found.size() == 1, "flat name array missing or ambiguous");
        names = found[0];
        nameCache.clear();
    }
    std::uint32_t Class(const char* name, const char* package) const {
        std::vector<std::uint32_t> found;
        auto count = mem.Read<std::uint32_t>(names + 4);
        std::uint32_t id = 0;
        bool named = false;
        for (std::uint32_t i = 0; i < count; ++i) {
            auto ptr = mem.Read<std::uint32_t>(mem.Read<std::uint32_t>(names) + i * 4);
            if (!ptr) continue;
            std::string candidate;
            try {
                candidate = Name(i);
            } catch (const Unavailable&) {
                continue;
            }
            if (SameName(candidate, name)) {
                Need(!named, "duplicate class name index");
                named = true;
                id = i;
            }
        }
        Need(named, "class identifier missing");
        for (const auto& s : image.sections)
            if ((s.flags & IMAGE_SCN_MEM_WRITE) && !(s.flags & IMAGE_SCN_MEM_EXECUTE))
                for (auto a = (s.low + 3) & ~3u; a < s.high && 0x80 <= s.high - a; a += 4) {
                    if (image.At<std::uint32_t>(a + 0x28) != id ||
                        image.At<std::uint32_t>(a + 0x2c) != 0)
                        continue;
                    try {
                        if (Type(a, name, "Class", package)) found.push_back(a);
                    } catch (const Unavailable&) {
                    }
                }
        Need(found.size() == 1, "class owner missing or ambiguous");
        return found[0];
    }
    Property Field(std::uint32_t owner, const char* name, const char* kind,
                   std::uint32_t width) const {
        auto bound = mem.Read<std::uint32_t>(owner + 0x4c);
        Need(bound && bound < 0x100000, "invalid reflected owner size");
        auto field = mem.Read<std::uint32_t>(owner + 0x48);
        std::set<std::uint32_t> seen;
        Property result{};
        unsigned matches = 0;
        for (unsigned i = 0; field && i < 4096; ++i) {
            Need(seen.insert(field).second, "cyclic property chain");
            if (SameName(ObjectName(field), name)) {
                Need(mem.Read<std::uint32_t>(field + 0x24) == owner &&
                         SameName(ObjectName(mem.Read<std::uint32_t>(field + 0x30)), kind),
                     "property owner or kind mismatch");
                Need(mem.Read<std::uint32_t>(field + 0x3c) == 1,
                     "unexpected property array dimension");
                auto size = mem.Read<std::uint32_t>(field + 0x40),
                     off = mem.Read<std::uint32_t>(field + 0x5c);
                Need(size && (!width || size == width) && off < bound && size <= bound - off,
                     "property outside owner or wrong width");
                result = {off, size, 0, field};
                ++matches;
                if (!std::strcmp(kind, "StructProperty") || !std::strcmp(kind, "ObjectProperty")) {
                    result.inner = mem.Read<std::uint32_t>(field + 0x6c);
                    Need(result.inner != 0, "missing property target");
                }
                if (!std::strcmp(kind, "StructProperty"))
                    Need(mem.Read<std::uint32_t>(result.inner + 0x4c) == size,
                         "inner struct size mismatch");
            }
            field = mem.Read<std::uint32_t>(field + 0x38);
        }
        Need(!field && matches == 1, "property missing or ambiguous");
        return result;
    }
    std::uint32_t ObjectField(std::uint32_t owner, const char* name, std::uint32_t inner) const {
        auto p = Field(owner, name, "ObjectProperty", 4);
        Need(p.inner == inner, "object property target mismatch");
        return p.offset;
    }
    std::uint32_t Scalar(std::uint32_t owner, const char* name,
                         const char* kind = "FloatProperty") const {
        return Field(owner, name, kind, 4).offset;
    }
    std::pair<std::uint32_t, std::uint32_t> Bool(std::uint32_t owner, const char* name) const {
        auto p = Field(owner, name, "BoolProperty", 4);
        auto mask = mem.Read<std::uint32_t>(p.address + 0x6c);
        Need(mask && !(mask & (mask - 1)), "invalid bool mask");
        return {p.offset, mask};
    }
};
}  // namespace
}  // namespace DishonoredHeadTracking
namespace DishonoredHeadTracking {
namespace {
void StructIdentity(const Metadata& m, const Memory& memory, std::uint32_t value, const char* name,
                    std::uint32_t owner) {
    Need(SameName(m.ObjectName(value), name) &&
             SameName(m.ObjectName(memory.Read<std::uint32_t>(value + 0x30)), "ScriptStruct") &&
             memory.Read<std::uint32_t>(value + 0x24) == owner,
         "inner structure identity mismatch");
}
void Reflect(const Image& image, const Memory& memory, Metadata& m, BuildProfile& p,
             RuntimeLayout& l) {
    m.FindNames();
    l.names = m.names - image.base;
    auto object = m.Class("Object", "Core"), actor = m.Class("Actor", "Engine");
    l.pcClass = m.Class("PlayerController", "Engine");
    l.cameraClass = m.Class("Camera", "Engine");
    l.worldInfoClass = m.Class("WorldInfo", "Engine");
    auto gameInfo = m.Class("GameInfo", "Engine");
    l.worldClass = m.Class("World", "Engine");
    l.gameClass = m.Class("DishonoredGameInfo", "DishonoredGame");
    l.uiClass = m.Class("DisGlobalUIManager", "DishonoredGame");
    l.movieClass = m.Class("GFxMoviePlayer", "GFxUI");
    l.hudClass = m.Class("DisGFxMoviePlayerHUD", "DishonoredGame");
    auto mainMenu = m.Class("DisGFxMoviePlayerMainMenu", "DishonoredGame"),
         pauseMenu = m.Class("DisGFxMoviePlayerPauseMenu", "DishonoredGame");
    p.offPlayerCamera = m.ObjectField(l.pcClass, "PlayerCamera", l.cameraClass);
    l.actorWorldInfo = m.ObjectField(actor, "WorldInfo", l.worldInfoClass);
    l.game = m.ObjectField(l.worldInfoClass, "Game", gameInfo);
    l.ui = m.ObjectField(l.gameClass, "m_pGlobalUIManager", l.uiClass);
    l.mainMenu = m.ObjectField(l.uiClass, "m_pMainMenu", mainMenu);
    l.pauseMenu = m.ObjectField(l.uiClass, "m_pPauseMenu", pauseMenu);
    auto controller = m.Class("Controller", "Engine");
    l.controllerList = m.ObjectField(l.worldInfoClass, "ControllerList", controller);
    l.nextController = m.ObjectField(controller, "NextController", controller);
    l.player = m.ObjectField(l.pcClass, "Player", m.Class("Player", "Engine"));
    l.localPlayerClass = m.Class("LocalPlayer", "Engine");
    auto open = m.Bool(l.movieClass, "bMovieIsOpen");
    l.movieOpen = open.first;
    l.movieOpenMask = open.second;
    l.camDefaultFov = m.Scalar(l.cameraClass, "DefaultFOV");
    l.controllerDefaultFov = m.Scalar(l.pcClass, "DefaultFOV");
    auto locked = m.Bool(l.cameraClass, "bLockedFOV");
    l.lockedBits = locked.first;
    l.lockedMask = locked.second;
    l.lockedFov = m.Scalar(l.cameraClass, "LockedFOV");
    auto aspect = m.Bool(l.cameraClass, "bConstrainAspectRatio");
    l.aspectBits = aspect.first;
    l.aspectMask = aspect.second;
    l.aspect = m.Scalar(l.cameraClass, "ConstrainedAspectRatio");
    auto cache = m.Field(l.cameraClass, "CameraCache", "StructProperty", 0);
    StructIdentity(m, memory, cache.inner, "TCameraCache", l.cameraClass);
    auto pov = m.Field(cache.inner, "POV", "StructProperty", 28);
    StructIdentity(m, memory, pov.inner, "TPOV", object);
    auto loc = m.Field(pov.inner, "Location", "StructProperty", 12),
         rot = m.Field(pov.inner, "Rotation", "StructProperty", 12);
    StructIdentity(m, memory, loc.inner, "Vector", object);
    StructIdentity(m, memory, rot.inner, "Rotator", object);
    Need(m.Scalar(loc.inner, "X") == 0 && m.Scalar(loc.inner, "Y") == 4 &&
             m.Scalar(loc.inner, "Z") == 8,
         "vector component layout mismatch");
    Need(m.Scalar(rot.inner, "Pitch", "IntProperty") == 0 &&
             m.Scalar(rot.inner, "Yaw", "IntProperty") == 4 &&
             m.Scalar(rot.inner, "Roll", "IntProperty") == 8,
         "rotator component layout mismatch");
    l.povLocation = cache.offset + pov.offset + loc.offset;
    l.povRotation = cache.offset + pov.offset + rot.offset;
    l.povFov = cache.offset + pov.offset + m.Scalar(pov.inner, "FOV");
    auto dot = m.Field(l.hudClass, "m_CrosshairCurPos", "StructProperty", 8);
    StructIdentity(m, memory, dot.inner, "Vector2D", object);
    Need(m.Scalar(dot.inner, "X") == 0 && m.Scalar(dot.inner, "Y") == 4,
         "crosshair vector layout mismatch");
    l.hudDotX = dot.offset;
    l.hudDotY = dot.offset + 4;
}
bool IsA(const Memory& mem, std::uint32_t obj, std::uint32_t wanted) {
    auto cls = mem.Read<std::uint32_t>(obj + 0x30);
    std::set<std::uint32_t> seen;
    for (unsigned i = 0; cls && i < 32; ++i) {
        Need(seen.insert(cls).second, "cyclic class hierarchy");
        if (cls == wanted) return true;
        cls = mem.Read<std::uint32_t>(cls + 0x44);
    }
    return false;
}
std::uint32_t Slot(const Image& image) {
    auto v = image.Function(image.Native("AControllerexecGetPlayerViewPoint"));
    for (std::size_t n = v.size(); n-- > 4;)
        if (v[n].h.opcode == 0xff && v[n].h.modrm_reg == 2) {
            const auto& load = v[n - 4];
            Need(v[n].h.modrm == 0xd2 && v[n - 1].h.opcode == 0x8b && v[n - 1].h.modrm == 0xcb &&
                     v[n - 2].h.opcode == 0x50 && v[n - 3].h.opcode == 0x57 &&
                     load.h.opcode == 0x8b && load.h.modrm_reg == 2 && load.Mem(2),
                 "viewpoint exec argument or virtual dispatch mismatch");
            auto offset = static_cast<std::uint32_t>(load.Disp());
            Need(offset && offset < 0x2000 && !(offset & 3), "invalid viewpoint vtable slot");
            return offset;
        }
    throw Unavailable("viewpoint virtual dispatch missing");
}
std::vector<Ins> Containing(const Image& image, std::uint32_t address, std::uint32_t span) {
    for (auto a = address & ~15u; a >= image.base && address - a < span; a -= 16) {
        if (!image.Code(a, 3)) continue;
        auto first = image.At<std::uint8_t>(a);
        if (first != 0x55 && first != 0x53) continue;
        auto second = image.Decode(a + 1);
        if (second.h.opcode != 0x8b || second.h.modrm != (first == 0x55 ? 0xec : 0xdc)) continue;
        try {
            auto code = image.Function(a, 0x10000);
            if (std::any_of(code.begin(), code.end(),
                            [address](const Ins& i) { return i.at == address; }))
                return code;
        } catch (const Unavailable&) {
        }
    }
    throw Unavailable("containing function is not bounded");
}
void CameraTargets(const Image& image, const Memory& mem, const Metadata& m, BuildProfile& p,
                   RuntimeLayout& l) {
    l.slot = Slot(image);
    p.rvaGetFovAngle = image.Direct("APlayerControllerexecGetFOVAngle") - image.base;
    auto fov = image.Function(image.base + static_cast<std::uint32_t>(p.rvaGetFovAngle));
    auto fovCalls = image.Calls(image.base + static_cast<std::uint32_t>(p.rvaGetFovAngle));
    Need(fovCalls.size() == 1 && fov.size() > 6 && fov[3].h.opcode == 0x8b &&
             fov[3].h.modrm == 0xc1 && fov[4].h.opcode == 0x8b && fov[4].h.modrm_reg == 1 &&
             fov[4].Mem(0) && fov[4].Disp() == static_cast<int>(p.offPlayerCamera),
         "FOV getter camera flow disagrees with reflection");
    auto selector = image.Function(fovCalls[0].Target());
    auto flagOffset = l.lockedBits, mask = l.lockedMask;
    while (mask > 255) {
        mask >>= 8;
        ++flagOffset;
    }
    Need(selector.size() > 6 && selector[3].h.opcode == 0xf6 && selector[3].h.modrm_reg == 0 &&
             selector[3].Mem(1) && selector[3].Disp() == static_cast<int>(flagOffset) &&
             selector[3].h.imm.imm8 == mask && selector[4].h.opcode == 0x74,
         "FOV selector flag disagrees with reflection");
    const auto& lockedLoad = selector[5];
    Need(lockedLoad.h.opcode == 0x0f && lockedLoad.h.opcode2 == 0x10 &&
             lockedLoad.h.p_rep == 0xf3 && lockedLoad.Mem(1) &&
             lockedLoad.Disp() == static_cast<int>(l.lockedFov),
         "locked FOV read disagrees with reflection");
    auto cached =
        image.Decode(selector[4].End() + static_cast<std::int8_t>(selector[4].h.imm.imm8));
    Need(cached.h.opcode == 0x0f && cached.h.opcode2 == 0x10 && cached.h.p_rep == 0xf3 &&
             cached.Mem(1) && cached.Disp() == static_cast<int>(l.povFov),
         "cached FOV read disagrees with reflection");
    auto world = image.Function(image.Direct("AWorldInfoexecGetWorldInfo"));
    Need(world.size() >= 4 && world[0].h.opcode == 0xa1 && world[3].h.opcode == 0x8b &&
             world[3].h.modrm_reg == 0 && world[3].Mem(0),
         "world getter shape mismatch");
    p.rvaGWorld = world[0].h.imm.imm32 - image.base;
    l.worldInfo = static_cast<std::uint32_t>(world[3].Disp());
    Need(image.In(image.base + p.rvaGWorld, 4, IMAGE_SCN_MEM_WRITE) && l.worldInfo < 0x10000,
         "world pointer or owner field outside bounds");
    auto worldSize = mem.Read<std::uint32_t>(l.worldClass + 0x4c);
    Need(worldSize >= 4 && l.worldInfo <= worldSize - 4, "world info field exceeds world size");
    auto w = mem.Read<std::uint32_t>(image.base + static_cast<std::uint32_t>(p.rvaGWorld));
    Need(w && IsA(mem, w, l.worldClass), "world not initialized or wrong type");
    auto wi = mem.Read<std::uint32_t>(w + l.worldInfo);
    Need(wi && IsA(mem, wi, l.worldInfoClass), "world info not initialized or wrong type");
    auto item = mem.Read<std::uint32_t>(wi + l.controllerList);
    std::set<std::uint32_t> visited;
    std::uint32_t pc = 0;
    for (unsigned n = 0; item && n < 512; ++n) {
        Need(visited.insert(item).second, "cyclic controller list");
        if (IsA(mem, item, l.pcClass)) {
            auto player = mem.Read<std::uint32_t>(item + l.player);
            if (player && IsA(mem, player, l.localPlayerClass)) {
                Need(pc == 0, "multiple local player controllers");
                pc = item;
            }
        }
        item = mem.Read<std::uint32_t>(item + l.nextController);
    }
    Need(!item && pc, "local player controller not initialized");
    auto table = mem.Read<std::uint32_t>(pc), gpv = mem.Read<std::uint32_t>(table + l.slot);
    Need(image.Code(gpv), "viewpoint vtable target outside image");
    auto gpvcode = image.Function(gpv);
    std::vector<std::uint32_t> cameraCopies;
    for (const auto& i : gpvcode)
        if (i.h.opcode == 0x8b && i.Mem(0) && (i.h.modrm_reg == 2 || i.h.modrm_reg == 0) &&
            i.Disp() >= static_cast<int>(l.povLocation) &&
            i.Disp() <= static_cast<int>(l.povRotation + 8))
            cameraCopies.push_back(static_cast<std::uint32_t>(i.Disp()));
    Need(cameraCopies == std::vector<std::uint32_t>{l.povLocation, l.povLocation + 4,
                                                    l.povLocation + 8, l.povRotation,
                                                    l.povRotation + 4, l.povRotation + 8} &&
             gpvcode.back().h.opcode == 0xc2 && gpvcode.back().h.imm.imm16 == 8,
         "camera output copies or viewpoint ABI mismatch");
    auto firstCopy = std::find_if(gpvcode.begin(), gpvcode.end(), [&l](const Ins& i) {
        return i.h.opcode == 0x8b && i.Mem(0) && i.Disp() == static_cast<int>(l.povLocation);
    });
    auto copyIndex = static_cast<std::size_t>(firstCopy - gpvcode.begin());
    Need(copyIndex >= 3 && copyIndex + 13 < gpvcode.size(), "incomplete camera output copy");
    const auto& camera = gpvcode[copyIndex - 3];
    Need(camera.h.opcode == 0x8b && camera.h.modrm_reg == 0 && camera.Mem(6) &&
             camera.Disp() == static_cast<int>(p.offPlayerCamera),
         "viewpoint camera pointer disagrees with reflection");
    for (unsigned part = 0; part < 2; ++part) {
        auto start = copyIndex + part * 7;
        const auto& output = gpvcode[start + 1];
        Need(output.h.opcode == 0x8b && output.Mem(5) &&
                 output.Disp() == static_cast<int>(8 + part * 4),
             "viewpoint output stack argument mismatch");
        const unsigned loads[] = {0, 3, 5}, stores[] = {2, 4, 6};
        for (unsigned k = 0; k < 3; ++k) {
            const auto& from = gpvcode[start + loads[k]];
            const auto& to = gpvcode[start + stores[k]];
            Need(from.h.opcode == 0x8b && from.Mem(0) &&
                     from.Disp() ==
                         static_cast<int>((part ? l.povRotation : l.povLocation) + k * 4) &&
                     to.h.opcode == 0x89 && to.Mem(output.h.modrm_reg) &&
                     to.Disp() == static_cast<int>(k * 4) && to.h.modrm_reg == from.h.modrm_reg,
                 "viewpoint scalar output flow mismatch");
        }
    }
    p.rvaGetPlayerViewPoint = gpv - image.base;
    auto deproject = image.Direct("ULocalPlayerexecDeProject");
    std::uint32_t scene = 0;
    unsigned matches = 0;
    for (const auto& c : image.Calls(deproject))
        for (const auto& inner : image.Calls(c.Target()))
            if (inner.Target() == image.base + p.rvaGetFovAngle) {
                scene = c.Target();
                p.rvaDeProjectCaller = c.End() - image.base;
                p.rvaCalcSceneViewFovReturn = inner.End() - image.base;
                ++matches;
            }
    Need(matches == 1, "scene view / FOV call relationship is ambiguous");
    auto code = image.Function(scene);
    const bool classicFrame = code.size() > 2 && code[0].h.opcode == 0x55 &&
                              code[1].h.opcode == 0x8b && code[1].h.modrm == 0xec;
    const bool alignedFrame =
        code.size() > 9 && code[0].h.opcode == 0x53 && code[1].h.opcode == 0x8b &&
        code[1].h.modrm == 0xdc && code[2].h.opcode == 0x83 && code[2].h.modrm == 0xec &&
        code[2].h.imm.imm8 == 8 && code[3].h.opcode == 0x83 && code[3].h.modrm == 0xe4 &&
        code[3].h.imm.imm8 == 0xf0 && code[4].h.opcode == 0x83 && code[4].h.modrm == 0xc4 &&
        code[4].h.imm.imm8 == 4 && code[5].h.opcode == 0x55 && code[6].h.opcode == 0x8b &&
        code[6].h.modrm_reg == 5 && code[6].Mem(3) && code[6].Disp() == 4 &&
        code[7].h.opcode == 0x89 && code[7].h.modrm_reg == 5 && code[7].Mem(4) &&
        code[7].Disp() == 4 && code[8].h.opcode == 0x8b && code[8].h.modrm == 0xec;
    Need(classicFrame || alignedFrame, "scene view caller frame layout is unsupported");
    unsigned calls = 0;
    for (std::size_t n = 5; n < code.size(); ++n)
        if (code[n].Call() && code[n].Target() == image.base + p.rvaGetFovAngle) {
            auto load = code[n - 5];
            Need(code[n - 2].h.opcode == 0xff && code[n - 2].h.modrm == 0xd0 &&
                     load.h.opcode == 0x8b && load.h.modrm_reg == 0 && load.Mem(0) &&
                     load.Disp() == static_cast<int>(l.slot),
                 "render viewpoint dispatch disagrees with exec native");
            p.rvaCalcSceneViewReturn = code[n - 2].End() - image.base;
            ++calls;
        }
    Need(calls == 1, "render viewpoint caller missing or ambiguous");
    std::map<std::string, std::uint32_t> callers;
    for (const auto& s : image.sections)
        if (s.flags & IMAGE_SCN_MEM_EXECUTE)
            for (auto a = s.low; a < s.high && 5 <= s.high - a; ++a) {
                if (image.At<std::uint8_t>(a) != 0xe8 ||
                    a + 5 + image.At<std::uint32_t>(a + 1) != scene)
                    continue;
                auto outer = Containing(image, a, 0x5000);
                auto at = std::find_if(outer.begin(), outer.end(),
                                       [a](const Ins& i) { return i.at == a; });
                auto index = static_cast<std::size_t>(at - outer.begin());
                unsigned pushed = 0;
                std::size_t first = index;
                for (auto j = index; j-- > 0 && index - j <= 24;) {
                    const auto& h = outer[j].h;
                    if (h.opcode == 0xe8 || h.opcode == 0xe9 || h.opcode == 0xeb ||
                        (h.opcode >= 0x70 && h.opcode <= 0x7f) ||
                        (h.opcode == 0x0f && h.opcode2 >= 0x80 && h.opcode2 <= 0x8f))
                        break;
                    if ((h.opcode >= 0x50 && h.opcode <= 0x57) || h.opcode == 0x68 ||
                        h.opcode == 0x6a) {
                        if (++pushed == 5) {
                            first = j;
                            break;
                        }
                    }
                }
                Need(pushed == 5, "scene view caller argument count mismatch");
                const auto& push = outer[first];
                std::string kind;
                if (push.h.opcode == 0x6a || push.h.opcode == 0x68) {
                    Need(push.h.imm.imm32 == 0, "unexpected scene view caller argument");
                    kind = a + 5 - image.base == p.rvaDeProjectCaller ? "deproject" : "streaming";
                } else {
                    Need(first > 0 && outer[first - 1].h.opcode == 0x8d &&
                             outer[first - 1].h.modrm_reg == push.h.opcode - 0x50 &&
                             outer[first - 1].Mem(5),
                         "viewport scene argument is not stack storage");
                    kind = "viewport";
                }
                Need(callers.emplace(kind, a + 5 - image.base).second,
                     "multiple scene view callers for one role");
            }
    Need(callers.size() == 3 && callers.count("deproject") && callers.count("streaming") &&
             callers.count("viewport"),
         "scene view caller roles incomplete");
    p.rvaStreamingCaller = callers.at("streaming");
    p.rvaViewportSceneViewCaller = callers.at("viewport");
    Need(SameName(m.ObjectName(mem.Read<std::uint32_t>(pc + 0x30)), "DishonoredPlayerController"),
         "unexpected live controller class");
}
}  // namespace
namespace {
void HudTargets(const Image&, const Memory&, const Metadata&, BuildProfile&, RuntimeLayout&);
void TraceTarget(const Image&, BuildProfile&, RuntimeLayout&);
}  // namespace
bool DiscoverRuntime(HANDLE process, std::uint32_t base, const std::vector<std::uint8_t>& bytes,
                     BuildProfile& profile, RuntimeLayout& layout, std::string& error) {
    profile = {};
    layout = {};
    const char* stage = "image";
    try {
        Image image(base, bytes);
        Memory mem{process};
        Metadata metadata(image, mem);
        BuildProfile candidate{};
        RuntimeLayout fields{};
        stage = "reflection";
        Reflect(image, mem, metadata, candidate, fields);
        stage = "camera";
        CameraTargets(image, mem, metadata, candidate, fields);
        stage = "HUD";
        HudTargets(image, mem, metadata, candidate, fields);
        stage = "collision";
        TraceTarget(image, candidate, fields);
        profile = candidate;
        layout = fields;
        error.clear();
        return true;
    } catch (const Unavailable& e) {
        error = std::string(stage) + ": " + e.what();
        return false;
    }
}
}  // namespace DishonoredHeadTracking
namespace DishonoredHeadTracking {
namespace {
void HudTargets(const Image& image, const Memory& mem, const Metadata& m, BuildProfile& p,
                RuntimeLayout& l) {
    std::set<std::uint32_t> targets;
    for (auto str : image.Strings("SetCrosshairState"))
        for (const auto& s : image.sections)
            if (s.flags & IMAGE_SCN_MEM_EXECUTE) {
                for (auto a = s.low; a < s.high && 5 <= s.high - a; ++a)
                    if (image.At<std::uint8_t>(a) == 0x68 &&
                        image.At<std::uint32_t>(a + 1) == str) {
                        auto code = Containing(image, a, 0x2000);
                        targets.insert(code.front().at);
                    }
            }
    Need(targets.size() == 1, "HUD update missing or ambiguous");
    auto code = image.Function(*targets.begin());
    bool hudThis = false;
    for (const auto& instruction : code) {
        if (instruction.Call() || (instruction.h.opcode == 0xff && instruction.h.modrm_reg == 2))
            break;
        if (instruction.h.opcode == 0x8b && instruction.h.modrm == 0xf1) hudThis = true;
    }
    Need(hudThis, "HUD object register flow is unsupported");
    Need(code.back().h.opcode == 0xc2 && code.back().h.imm.imm16 == 4,
         "HUD update argument width mismatch");
    auto target = m.Field(l.hudClass, "m_CrosshairTargetPos", "StructProperty", 8);
    struct Value {
        int field = -1;
        bool half = false, scaled = false;
    };
    Value xmm[8]{}, gpr[8]{};
    std::map<int, Value> stack;
    for (const auto& i : code) {
        const auto& h = i.h;
        if (h.opcode == 0xe8 || (h.opcode == 0xff && h.modrm_reg == 2)) {
            for (auto& v : xmm) v = {};
            for (auto& v : gpr) v = {};
            continue;
        }
        if (h.opcode == 0x0f && h.opcode2 == 0x57 && h.modrm_mod == 3) {
            xmm[h.modrm_reg] = {};
            continue;
        }
        if (h.opcode == 0x0f && h.p_rep == 0xf3 && h.opcode2 == 0x10) {
            Value value{};
            if (h.modrm_mod == 3)
                value = xmm[h.modrm_rm];
            else if (h.modrm_mod == 0 && h.modrm_rm == 5) {
                auto addr = h.disp.disp32;
                if (image.In(addr, 4, IMAGE_SCN_MEM_READ))
                    value.half = image.At<std::uint32_t>(addr) == 0x3f000000;
            } else if (i.Mem(6))
                value.field = i.Disp();
            else if (i.Mem(5))
                value = stack[i.Disp()];
            xmm[h.modrm_reg] = value;
            continue;
        }
        if (h.opcode == 0x0f && h.p_rep == 0xf3 && h.opcode2 == 0x59) {
            auto source = h.modrm_mod == 3 ? xmm[h.modrm_rm] : Value{};
            auto& dest = xmm[h.modrm_reg];
            if (dest.field >= 0 && !dest.scaled && source.half)
                dest.scaled = true;
            else
                dest = {};
            continue;
        }
        if (h.opcode == 0x0f && h.p_rep == 0xf3 && h.opcode2 == 0x11 && i.Mem(5)) {
            stack[i.Disp()] = xmm[h.modrm_reg];
            continue;
        }
        if (h.opcode == 0x8b) {
            gpr[h.modrm_reg] = h.modrm_mod == 3 ? gpr[h.modrm_rm]
                               : i.Mem(5)       ? stack[i.Disp()]
                                                : Value{};
            continue;
        }
        if (h.opcode == 0x89 && i.Mem(5)) {
            stack[i.Disp()] = gpr[h.modrm_reg];
            continue;
        }
        if (h.opcode == 0x89 && h.p_66 && !h.p_67 && !h.p_seg && h.modrm_mod != 0 &&
            h.modrm_mod != 3 && h.modrm_rm == 5) {
            for (auto it = stack.begin(); it != stack.end();) {
                if (it->first < i.Disp() + 2 && i.Disp() < it->first + 4)
                    it = stack.erase(it);
                else
                    ++it;
            }
            continue;
        }
        if (h.opcode == 0x89 && i.Mem(6)) {
            auto value = gpr[h.modrm_reg];
            if (value.scaled && value.field > 0) {
                if (i.Disp() == static_cast<int>(target.offset))
                    l.hudViewportW = static_cast<std::uint32_t>(value.field);
                if (i.Disp() == static_cast<int>(target.offset + 4))
                    l.hudViewportH = static_cast<std::uint32_t>(value.field);
                if (l.hudViewportW && l.hudViewportH) break;
            }
            continue;
        }
        if (h.opcode >= 0xb8 && h.opcode <= 0xbf) {
            gpr[h.opcode - 0xb8] = {};
            continue;
        }
        if ((h.opcode == 0x8d || h.opcode == 0x33) && h.modrm_reg < 8) {
            gpr[h.modrm_reg] = {};
            continue;
        }
        for (auto& value : xmm) value = {};
        for (auto& value : gpr) value = {};
        stack.clear();
    }
    auto size = mem.Read<std::uint32_t>(l.hudClass + 0x4c);
    Need(l.hudViewportW && l.hudViewportH && l.hudViewportW != l.hudViewportH &&
             l.hudViewportW < size - 4 && l.hudViewportH < size - 4 && !(l.hudViewportW & 3) &&
             !(l.hudViewportH & 3),
         "HUD viewport centering data flow missing");
    p.rvaCrosshairUpdate = *targets.begin() - image.base;
}
void TraceTarget(const Image& image, BuildProfile& p, RuntimeLayout& l) {
    auto target = image.Direct("AActorexecFastTrace");
    enum class Source { Unknown, This, Stack, Constant, World };
    struct Argument {
        Source source = Source::Unknown;
        int value = 0;
    };
    Argument registers[8]{};
    registers[1].source = Source::This;
    std::vector<Argument> pushed;
    bool argumentsValid = false;
    for (const auto& instruction : image.Function(image.Native("AActorexecFastTrace"))) {
        const auto& h = instruction.h;
        if (instruction.Call() || (h.opcode == 0xff && h.modrm_reg == 2)) {
            if (instruction.Call() && instruction.Target() == target) {
                Need(pushed.size() == 7 && registers[1].source == Source::World &&
                         pushed[0].source == Source::Constant && pushed[0].value == 0 &&
                         pushed[1].source == Source::Stack &&
                         pushed[2].source == Source::Constant &&
                         pushed[3].source == Source::Stack && pushed[4].source == Source::Stack &&
                         pushed[5].source == Source::This && pushed[6].source == Source::Stack &&
                         pushed[1].value == pushed[3].value + 12 &&
                         pushed[3].value == pushed[4].value + 12 &&
                         pushed[6].value < pushed[4].value,
                     "line trace argument order or this pointer mismatch");
                argumentsValid = true;
            }
            pushed.clear();
            registers[0] = registers[1] = registers[2] = {};
        } else if (h.opcode >= 0x50 && h.opcode <= 0x57) {
            pushed.push_back(registers[h.opcode - 0x50]);
        } else if (h.opcode == 0x68 || h.opcode == 0x6a) {
            pushed.push_back({Source::Constant, h.opcode == 0x6a
                                                    ? static_cast<std::int8_t>(h.imm.imm8)
                                                    : static_cast<int>(h.imm.imm32)});
        } else if (h.opcode == 0x8b) {
            Argument source{};
            if (h.modrm_mod == 3) source = registers[h.modrm_rm];
            if (h.modrm_mod == 0 && h.modrm_rm == 5 && h.disp.disp32 == image.base + p.rvaGWorld)
                source.source = Source::World;
            registers[h.modrm_reg] = source;
        } else if (h.opcode == 0x8d) {
            registers[h.modrm_reg] =
                instruction.Mem(5) ? Argument{Source::Stack, instruction.Disp()} : Argument{};
        } else if (h.opcode >= 0xb8 && h.opcode <= 0xbf) {
            registers[h.opcode - 0xb8] = {Source::Constant, static_cast<int>(h.imm.imm32)};
        } else if (h.opcode == 0x33 && h.modrm_mod == 3) {
            registers[h.modrm_reg] =
                h.modrm_reg == h.modrm_rm ? Argument{Source::Constant, 0} : Argument{};
        } else if ((h.opcode == 0x81 || h.opcode == 0x83) && h.modrm_mod == 3) {
            registers[h.modrm_rm] = {};
        } else if (h.opcode >= 0x40 && h.opcode <= 0x4f) {
            registers[h.opcode & 7] = {};
        }
    }
    Need(argumentsValid, "line trace call arguments missing");
    auto line = image.Function(target);
    Need(line.back().h.opcode == 0xc2 && line.back().h.imm.imm16 == 28,
         "line trace does not consume seven arguments");
    unsigned copies = 0, misses = 0;
    for (std::size_t n = 0; n < line.size(); ++n) {
        const auto& i = line[n];
        if (i.h.opcode == 0xa5 && i.h.p_rep == 0xf3 && n >= 3 && line[n - 3].h.opcode == 0xb9 &&
            line[n - 2].h.opcode == 0x8b && line[n - 2].h.modrm == 0xfb) {
            Need(line[n - 3].h.imm.imm32 <= 128, "trace result exceeds scratch capacity");
            l.hitSize = line[n - 3].h.imm.imm32 * 4;
            ++copies;
        }
        if (n + 3 < line.size() && i.h.opcode == 0x8b && i.h.modrm_reg == 0 && i.Mem(5) &&
            i.Disp() == 8) {
            const auto& load = line[n + 1];
            const auto& time = line[n + 2];
            const auto& actor = line[n + 3];
            if (load.h.opcode == 0x0f && load.h.opcode2 == 0x10 && load.h.p_rep == 0xf3 &&
                load.h.modrm_mod == 0 && load.h.modrm_rm == 5 && time.h.opcode == 0x0f &&
                time.h.opcode2 == 0x11 && time.h.p_rep == 0xf3 && time.Mem(0) &&
                time.h.modrm_reg == load.h.modrm_reg && actor.h.opcode == 0x89 && actor.Mem(0)) {
                Need(image.At<std::uint32_t>(load.h.disp.disp32) == 0x3f800000,
                     "trace miss time initializer is not one");
                l.hitTime = static_cast<std::uint32_t>(time.Disp());
                l.hitActor = static_cast<std::uint32_t>(actor.Disp());
                ++misses;
            }
        }
    }
    Need(copies == 1 && misses == 1 && l.hitSize >= 32 && l.hitSize <= 512 &&
             l.hitTime <= l.hitSize - 4 && l.hitActor <= l.hitSize - 4 && !(l.hitTime & 3) &&
             !(l.hitActor & 3) && l.hitTime != l.hitActor,
         "trace result size or miss fields ambiguous");
    auto trace = image.Function(image.Native("AActorexecTrace"));
    unsigned matches = 0;
    for (std::size_t n = 3; n + 17 < trace.size(); ++n)
        if (trace[n].Call() && trace[n].Target() == target) {
            auto hit = trace[n - 2];
            Need(hit.h.opcode == 0x8d && hit.Mem(5) &&
                     trace[n - 1].h.opcode == 0x50 + hit.h.modrm_reg,
                 "trace result buffer argument mismatch");
            int stackBase = hit.Disp();
            auto world = trace[n - 3];
            Need(world.h.opcode == 0x8b && world.h.modrm_reg == 1 && world.h.modrm_mod == 0 &&
                     world.h.modrm_rm == 5 && world.h.disp.disp32 == image.base + p.rvaGWorld,
                 "trace world disagrees with world getter");
            auto actor = trace[n + 1];
            Need(actor.h.opcode == 0x8b && actor.Mem(5) &&
                     actor.Disp() - stackBase == static_cast<int>(l.hitActor),
                 "trace actor output disagrees with miss field");
            auto first = trace[n + 11];
            Need(first.h.opcode == 0x8b && first.Mem(5), "trace normal output missing");
            l.hitNormal = static_cast<std::uint32_t>(first.Disp() - stackBase);
            auto pointer = trace[n + 12];
            Need(pointer.h.opcode == 0x8b && pointer.Mem(5), "trace normal output pointer missing");
            const unsigned loads[] = {11, 14, 16}, stores[] = {13, 15, 17};
            for (unsigned k = 0; k < 3; ++k) {
                const auto& a = trace[n + loads[k]];
                const auto& b = trace[n + stores[k]];
                Need(a.h.opcode == 0x8b && a.Mem(5) &&
                         a.Disp() - stackBase == static_cast<int>(l.hitNormal + k * 4) &&
                         b.h.opcode == 0x89 && b.Mem(pointer.h.modrm_reg) &&
                         b.Disp() == static_cast<int>(k * 4) && b.h.modrm_reg == a.h.modrm_reg,
                     "trace normal component copy mismatch");
            }
            Need(l.hitNormal < l.hitSize && 12 <= l.hitSize - l.hitNormal && !(l.hitNormal & 3) &&
                     (l.hitTime + 4 <= l.hitNormal || l.hitTime >= l.hitNormal + 12) &&
                     (l.hitActor + 4 <= l.hitNormal || l.hitActor >= l.hitNormal + 12),
                 "trace normal outside result");
            ++matches;
        }
    Need(matches == 1, "trace native / line check relationship ambiguous");
    p.rvaSingleLineCheck = target - image.base;
}
}  // namespace
}  // namespace DishonoredHeadTracking
