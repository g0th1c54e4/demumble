#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <algorithm>
#include <iostream>

#include "llvm/Demangle/Demangle.h"
#include "swift/Demangling/Demangle.h"

std::string demangled_conv(std::string strSymbol, bool b = false, bool m = false, bool u = false);

static bool is_mangle_char_itanium(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_' || c == '$';
}
static bool is_mangle_char_rust(char c) {
    // https://rust-lang.github.io/rfcs/2603-rust-symbol-name-mangling-v0.html.
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_';
}
static bool is_mangle_char_swift(char c) {
    // https://github.com/swiftlang/swift/blob/main/docs/ABI/Mangling.rst
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_' || c == '$';
}
static bool is_mangle_char_win(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || strchr("?_@$", c);
}
static bool is_plausible_itanium_prefix(char* s) {
    // Itanium symbols start with 1-4 underscores followed by Z.
    // strnstr() is BSD, so use a small local buffer and strstr().
    const int N = 5;  // == strlen("____Z")
    char prefix[N + 1];
    strncpy(prefix, s, N); prefix[N] = '\0';
    return strstr(prefix, "_Z");
}
static bool is_plausible_rust_prefix(char* s) {
    // Rust symbols start with "_R".
    return s[0] == '_' && s[1] == 'R';
}
static bool is_plausible_swift_prefix(char* s) {
    // https://github.com/swiftlang/swift/blob/main/docs/ABI/Mangling.rst
    // But also swift/test/Demangle/Inputs/manglings.txt, which has
    // _Tt, _TF etc as prefix.

    // FIXME: This is missing prefix `@__swiftmacro_`.
    return
        (s[0] == '$' && s[1] == 's') ||
        (s[0] == '_' && s[1] == 'T') ||
        (s[0] == '$' && s[1] == 'S');
}

static std::string demangled(
    std::string origin,
    const char* format,
    std::string_view s,
    size_t* n_used
) {
    // Helper lambda 用于安全格式化字符串
    auto append_formatted = [&origin](const char* fmt, auto... args) {
        // 计算需要的内存大小
        int len = snprintf(nullptr, 0, fmt, args...);
        if (len <= 0) return;

        // 分配缓冲区
        std::vector<char> buf(len + 1);
        snprintf(buf.data(), buf.size(), fmt, args...);

        // 追加到结果
        origin.append(buf.data(), len);
        };

    if (char* itanium = llvm::itaniumDemangle(s)) {
        append_formatted(format, itanium, (int)s.size(), s.data());
        free(itanium);
    }
    else if (char* rust = llvm::rustDemangle(s)) {
        append_formatted(format, rust, (int)s.size(), s.data());
        free(rust);
    }
    else if (char* ms = llvm::microsoftDemangle(s, n_used, NULL)) {
        append_formatted(format, ms, (int)s.size(), s.data());
        free(ms);
    }
    else if (swift::Demangle::isSwiftSymbol(s)) {
        swift::Demangle::DemangleOptions options;
        options.SynthesizeSugarOnTypes = true;
        std::string swift = swift::Demangle::demangleSymbolAsString(s, options);

        if (swift == s) {
            origin.append(s.data(), s.size()); // 直接追加原始数据
        }
        else {
            append_formatted(format, swift.c_str(), (int)s.size(), s.data());
        }
    }
    else {
        origin.append(s.data(), s.size()); // 直接追加原始数据
    }

    return origin;
}


int main() {

    std::cout << demangled_conv("_Z4funcPci");

    return 1;
}


std::string demangled_conv(std::string strSymbol, bool b, bool m, bool u) {
    std::string result;
    enum { kPrintAll, kPrintMatching } print_mode = kPrintAll;
    const char* print_format = "%s";

    if (b) {
        print_format = "\"%s\" (%.*s)";
    }
    if (m) {
        print_mode = kPrintMatching;
    }
    if (u) {
        setbuf(stdout, NULL);
    }

    char* cur = strSymbol.data();
    char* end = cur + strSymbol.length();

    while (cur != end) {
        size_t offset_to_possible_symbol = strcspn(cur, "_?$");
        if (print_mode == kPrintAll) {
            // printf("%.*s", (int)(offset_to_possible_symbol), cur);
            result.append(cur, offset_to_possible_symbol); // 关键修改
        }

        cur += offset_to_possible_symbol;
        if (cur == end) {
            break;
        }

        size_t n_sym = 0;
        if (*cur == '?') {
            while (cur + n_sym != end && is_mangle_char_win(cur[n_sym])) {
                ++n_sym;
            }
        }
        else if (is_plausible_itanium_prefix(cur)) {
            while (cur + n_sym != end && is_mangle_char_itanium(cur[n_sym])) {
                ++n_sym;
            }
        }
        else if (is_plausible_rust_prefix(cur)) {
            while (cur + n_sym != end && is_mangle_char_rust(cur[n_sym])) {
                ++n_sym;
            }
        }
        else if (is_plausible_swift_prefix(cur)) {
            while (cur + n_sym != end && is_mangle_char_swift(cur[n_sym])) {
                ++n_sym;
            }
        }
        else {
            if (print_mode == kPrintAll) {
                result += "_";
            }
            ++cur;
            continue;
        }

        size_t n_used = n_sym;
        result += demangled(result, print_format, { cur, n_sym }, &n_used);

        cur += n_used;
    }

    return result;
}