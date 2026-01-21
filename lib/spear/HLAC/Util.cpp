
#include "HLAC/util.h"
#include <regex>
#include <llvm/Demangle/Demangle.h>

namespace HLAC {

std::string Util::stripParameters(const std::string& s) {
    // Remove everything from the first '(' to the end (handles normal demangles)
    auto pos = s.find('(');
    if (pos == std::string::npos) return s;
    return s.substr(0, pos) + "(...)";
}

std::string Util::dotRecordEscape(llvm::StringRef s) {
    std::string out;
    out.reserve(s.size() * 2);

    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;

                // record-label metacharacters
            case '{': case '}': case '|': case '<': case '>':
                out += '\\';
                out += c;
                break;

            case '\n':
                out += "\\l";   // ✅ LEFT-ALIGNED line break
                break;

            case '\r':
                break;

            default:
                out += c;
        }
    }
    return out;
}

std::string Util::dropReturnType(std::string s) {
    // Try to find the start of the function name by locating " operator" or "::"
    size_t op = s.find(" operator");
    size_t ns = s.find("::");

    size_t start = std::string::npos;
    if (op != std::string::npos) start = op + 1;       // skip leading space
    else if (ns != std::string::npos) {
        // crude: walk back to previous space before the first ::
        size_t sp = s.rfind(' ', ns);
        if (sp != std::string::npos) start = sp + 1;
    }

    if (start != std::string::npos) return s.substr(start);
    return s;
}

std::string Util::escapeDotLabel(std::string s) {
    std::string out;
    out.reserve(s.size() + 8);

    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n";  break;
            case '\r': break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    return out;
}

std::string Util::dotSafeDemangledName(const std::string& mangled) {
    std::string s = llvm::demangle(mangled);
    s = prettifyOperators(std::move(s));
    s = escapeDotLabel(std::move(s));   // assumes you use label="..."
    return s;
}


std::string Util::prettifyOperators(std::string s) {
    auto replace_all = [&](const std::string& from, const std::string& to) {
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.size(), to);
            pos += to.size();
        }
    };

    // Common ones you’ll see a lot
    replace_all("operator<<", "operator pipein");
    replace_all("operator>>", "operator pipeout");
    replace_all("operator<",  "operator less");
    replace_all("operator>",  "operator greater");
    replace_all("operator==", "operator ==");
    replace_all("operator!=", "operator !=");
    replace_all("operator<=", "operator leq");
    replace_all("operator>=", "operator geq");
    replace_all("operator()", "operator ()");
    replace_all("operator[]", "operator []");
    replace_all("operator+",  "operator +");
    replace_all("operator-",  "operator -");
    replace_all("operator*",  "operator *");
    replace_all("operator/",  "operator /");
    replace_all("operator%",  "operator %");
    replace_all("operator=",  "operator =");

    return s;
}

std::string Util::shortenStdStreamOps(std::string s) {
    // Normalize the operator token first
    auto replace_all = [&](const std::string& from, const std::string& to) {
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.size(), to);
            pos += to.size();
        }
    };
    replace_all("operator<<", "operator <<");
    replace_all("operator>>", "operator >>");
    replace_all("operator|",  "operator |"); // if you have those

    // Collapse the extremely common ostream signature noise
    // Return type + namespace prefixes vary, so we match loosely.
    static const std::regex ostreamNoise(
        R"(std::basic_ostream<char,\s*std::char_traits<char>\s*>\s*&\s*)");
    s = std::regex_replace(s, ostreamNoise, "ostream& ");

    static const std::regex istreamNoise(
        R"(std::basic_istream<char,\s*std::char_traits<char>\s*>\s*&\s*)");
    s = std::regex_replace(s, istreamNoise, "istream& ");

    // Also shorten parameter occurrences of those types
    s = std::regex_replace(s, ostreamNoise, "ostream& ");
    s = std::regex_replace(s, istreamNoise, "istream& ");

    // Optional: shorten char traits occurrences in template args
    s = std::regex_replace(s,
        std::regex(R"(std::char_traits<char>)"),
        "char_traits");

    return s;
}

std::string Util::instToString(const llvm::Instruction &I) {
    std::string s;
    llvm::raw_string_ostream rs(s);
    rs << I;
    rs.flush();
    return s;
}

std::string Util::stripParamsInInstText(std::string s) {
    // Your old helper, but applied to ONE instruction line
    auto pos = s.find('(');
    if (pos == std::string::npos) return s;
    return s.substr(0, pos) + "(...)";
}

}