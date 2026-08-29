#include "./utils/Pattern.h"

#include <cctype>
#include <vector>

DWORD CUtil_Pattern::Find(const char* const moduleName, const char* const signature)
{
    if (!moduleName || !signature || !*signature) return 0;
    const DWORD module = reinterpret_cast<DWORD>(GetModuleHandleA(moduleName));
    if (!module) return 0;

    const auto* dos = reinterpret_cast<const PIMAGE_DOS_HEADER>(module);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x100000) return 0;
    const auto* nt = reinterpret_cast<const PIMAGE_NT_HEADERS32>(module + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) return 0;

    const DWORD start = module + nt->OptionalHeader.BaseOfCode;
    const DWORD size = nt->OptionalHeader.SizeOfCode;
    const DWORD end = start + size;
    if (end <= start || size > nt->OptionalHeader.SizeOfImage) return 0;
    return FindPattern(start, end, signature);
}

DWORD CUtil_Pattern::FindPattern(const DWORD address, const DWORD end, const char* const signature)
{
    if (!address || end <= address || !signature) return 0;

    std::vector<int> pattern;
    const char* cursor = signature;
    const auto hex = [](char value) -> int {
        if (value >= '0' && value <= '9') return value - '0';
        value = static_cast<char>(std::toupper(static_cast<unsigned char>(value)));
        return value - 'A' + 10;
    };
    while (*cursor) {
        while (*cursor && std::isspace(static_cast<unsigned char>(*cursor))) ++cursor;
        if (!*cursor) break;
        if (*cursor == '?') {
            pattern.push_back(-1);
            ++cursor;
            if (*cursor == '?') ++cursor;
            continue;
        }
        if (!cursor[1] || !std::isxdigit(static_cast<unsigned char>(cursor[0])) ||
            !std::isxdigit(static_cast<unsigned char>(cursor[1]))) return 0;
        pattern.push_back((hex(cursor[0]) << 4) | hex(cursor[1]));
        cursor += 2;
    }
    if (pattern.empty() || static_cast<DWORD>(pattern.size()) > end - address) return 0;

    for (DWORD current = address; current <= end - pattern.size(); ++current) {
        bool match = true;
        for (std::size_t i = 0; i < pattern.size(); ++i) {
            if (pattern[i] >= 0 && *reinterpret_cast<const BYTE*>(current + i) != pattern[i]) {
                match = false;
                break;
            }
        }
        if (match) return current;
    }
    return 0;
}
