#include "platform/pe.h"

namespace hr::platform::pe {
namespace {

const IMAGE_NT_HEADERS* nt_headers(const BYTE* base) {
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return nullptr;
    }
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE ? nt : nullptr;
}

} // namespace

std::vector<std::string> export_names(HMODULE module) {
    const auto base = reinterpret_cast<const BYTE*>(module);
    const auto nt = nt_headers(base);
    if (nt == nullptr) {
        return {};
    }
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (dir.VirtualAddress == 0) {
        return {};
    }
    const auto exports = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + dir.VirtualAddress);
    const auto names = reinterpret_cast<const DWORD*>(base + exports->AddressOfNames);
    std::vector<std::string> out;
    out.reserve(exports->NumberOfNames);
    for (DWORD i = 0; i < exports->NumberOfNames; ++i) {
        out.emplace_back(reinterpret_cast<const char*>(base + names[i]));
    }
    return out;
}

} // namespace hr::platform::pe
