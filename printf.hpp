#pragma once
#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <exception>
#include <iostream>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <type_traits>
#include <cstdint>

namespace sjtu {

using sv_t = std::string_view;

struct format_error : std::exception {
public:
    format_error(const char *msg = "invalid format") : msg(msg) {}
    auto what() const noexcept -> const char * override {
        return msg;
    }

private:
    const char *msg;
};

template <typename Tp>
struct formatter;

struct format_info {
    inline static constexpr auto npos = static_cast<std::size_t>(-1);
    std::size_t position; // where is the specifier
    std::size_t consumed; // how many characters consumed
};

template <typename... Args>
struct format_string {
public:
    // must be constructed at compile time, to ensure the format string is valid
    consteval format_string(const char *fmt);
    constexpr auto get_format() const -> std::string_view {
        return fmt_str;
    }
    constexpr auto get_index() const -> std::span<const format_info> {
        return fmt_idx;
    }

private:
    inline static constexpr auto Nm = sizeof...(Args);
    std::string_view fmt_str;            // the format string
    std::array<format_info, Nm> fmt_idx; // where are the specifiers
};

consteval auto find_specifier(sv_t &fmt) -> bool {
    do {
        if (const auto next = fmt.find('%'); next == sv_t::npos) {
            return false;
        } else if (next + 1 == fmt.size()) {
            throw format_error{"missing specifier after '%'"};
        } else if (fmt[next + 1] == '%') {
            // escape the specifier
            fmt.remove_prefix(next + 2);
        } else {
            fmt.remove_prefix(next + 1);
            return true;
        }
    } while (true);
};

template <typename Arg>
consteval auto parse_one_arg(sv_t &fmt, format_info &info) -> void {
    const auto last_pos = fmt.begin();
    if (!find_specifier(fmt)) {
        // no specifier found
        info = {
            .position = format_info::npos,
            .consumed = 0,
        };
        return;
    }

    const auto position = static_cast<std::size_t>(fmt.begin() - last_pos - 1);
    const auto consumed = formatter<Arg>{}.parse(fmt);

    info = {
        .position = position,
        .consumed = consumed,
    };

    if (consumed > 0) {
        fmt.remove_prefix(consumed);
    } else if (fmt.starts_with("_")) {
        fmt.remove_prefix(1);
    } else {
        throw format_error{"invalid specifier"};
    }
}

template <typename... Args>
consteval auto compile_time_format_check(sv_t fmt, std::span<format_info> idx) -> void {
    std::size_t n = 0;
    (parse_one_arg<Args>(fmt, idx[n++]), ...);
    if (find_specifier(fmt)) // extra specifier found
        throw format_error{"too many specifiers"};
}

template <typename... Args>
consteval format_string<Args...>::format_string(const char *fmt) :
    fmt_str(fmt), fmt_idx() {
    compile_time_format_check<Args...>(fmt_str, fmt_idx);
}

// Formatter for string-like types
template <typename StrLike>
    requires(
        std::same_as<StrLike, std::string> ||      //
        std::same_as<StrLike, std::string_view> || //
        std::same_as<StrLike, char *> ||           //
        std::same_as<StrLike, const char *>        //
    )
struct formatter<StrLike> {
    static constexpr auto parse(sv_t fmt) -> std::size_t {
        if (fmt.starts_with("s")) {
            return 1;
        } else if (fmt.starts_with("_")) {
            return 0; // default format
        }
        return 0;
    }
    static auto format_to(std::ostream &os, const StrLike &val, sv_t fmt = "s") -> void {
        if (fmt.starts_with("s") || fmt.starts_with("_")) {
            os << static_cast<sv_t>(val);
        } else {
            throw format_error{};
        }
    }
};

// Formatter for integral types
template <typename Int>
    requires std::integral<Int>
struct formatter<Int> {
    static constexpr auto parse(sv_t fmt) -> std::size_t {
        if (fmt.starts_with("d")) {
            return 1;
        } else if (fmt.starts_with("u")) {
            return 1;
        } else if (fmt.starts_with("_")) {
            return 0; // default format
        }
        return 0;
    }
    static auto format_to(std::ostream &os, const Int &val, sv_t fmt = "d") -> void {
        if (fmt.starts_with("d")) {
            os << static_cast<int64_t>(val);
        } else if (fmt.starts_with("u")) {
            os << static_cast<uint64_t>(val);
        } else if (fmt.starts_with("_")) {
            // default format: signed -> %d, unsigned -> %u
            if constexpr (std::is_signed_v<Int>) {
                os << static_cast<int64_t>(val);
            } else {
                os << static_cast<uint64_t>(val);
            }
        } else {
            throw format_error{};
        }
    }
};

// Formatter for vector types
template <typename T>
struct formatter<std::vector<T>> {
    static constexpr auto parse(sv_t fmt) -> std::size_t {
        if (fmt.starts_with("_")) {
            return 0; // default format
        }
        return 0;
    }
    static auto format_to(std::ostream &os, const std::vector<T> &val, sv_t fmt = "_") -> void {
        if (fmt.starts_with("_")) {
            os << "[";
            for (size_t i = 0; i < val.size(); ++i) {
                if (i > 0) os << ",";
                formatter<T>::format_to(os, val[i], "_");
            }
            os << "]";
        } else {
            throw format_error{};
        }
    }
};

template <typename... Args>
using format_string_t = format_string<std::decay_t<Args>...>;

template <typename... Args>
inline auto printf(format_string_t<Args...> fmt, const Args &...args) -> void {
    auto fmt_str = fmt.get_format();
    auto fmt_idx = fmt.get_index();
    
    std::size_t arg_idx = 0;
    std::size_t last_pos = 0;
    
    auto format_arg = [&](const auto &arg) {
        if (arg_idx >= fmt_idx.size()) {
            return;
        }
        
        const auto &info = fmt_idx[arg_idx];
        
        if (info.position == format_info::npos) {
            // No specifier found for this argument, just print remaining string
            std::cout << fmt_str.substr(last_pos);
            last_pos = fmt_str.size();
            ++arg_idx;
            return;
        }
        
        // Print everything before the specifier
        std::cout << fmt_str.substr(last_pos, info.position - last_pos);
        
        // Handle %% escapes in the printed portion
        std::size_t escape_pos = last_pos;
        while ((escape_pos = fmt_str.find("%%", escape_pos)) != sv_t::npos && escape_pos < info.position) {
            escape_pos += 2;
        }
        
        // Get the format specifier
        sv_t spec = fmt_str.substr(info.position + 1, info.consumed > 0 ? info.consumed : 1);
        
        // Format the argument
        using ArgType = std::decay_t<decltype(arg)>;
        formatter<ArgType>::format_to(std::cout, arg, spec);
        
        // Update position
        last_pos = info.position + 1 + (info.consumed > 0 ? info.consumed : 1);
        ++arg_idx;
    };
    
    (format_arg(args), ...);
    
    // Print remaining string, handling %% escapes
    if (last_pos < fmt_str.size()) {
        std::string remaining(fmt_str.substr(last_pos));
        std::string output;
        for (size_t i = 0; i < remaining.size(); ++i) {
            if (remaining[i] == '%' && i + 1 < remaining.size() && remaining[i + 1] == '%') {
                output += '%';
                ++i; // skip the second %
            } else {
                output += remaining[i];
            }
        }
        std::cout << output;
    }
}

} // namespace sjtu
