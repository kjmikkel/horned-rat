#include "platform/pe.h"

#include <cstring>

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

void** find_import_slot(HMODULE module, const char* dll, const char* function) {
    const auto base = reinterpret_cast<BYTE*>(module);
    const auto nt = nt_headers(base);
    if (nt == nullptr) {
        return nullptr;
    }
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (dir.VirtualAddress == 0) {
        return nullptr;
    }
    for (auto desc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress);
         desc->Name != 0; ++desc) {
        if (_stricmp(reinterpret_cast<const char*>(base + desc->Name), dll) != 0 ||
            desc->OriginalFirstThunk == 0) {
            continue;
        }
        auto names = reinterpret_cast<const IMAGE_THUNK_DATA*>(base + desc->OriginalFirstThunk);
        auto slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->FirstThunk);
        for (; names->u1.AddressOfData != 0; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) {
                continue;
            }
            const auto by_name =
                reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(by_name->Name), function) == 0) {
                return reinterpret_cast<void**>(&slots->u1.Function);
            }
        }
    }
    return nullptr;
}

} // namespace hr::platform::pe
