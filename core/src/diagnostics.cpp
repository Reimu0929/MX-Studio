#include "msx/common.h"

#include <sstream>

namespace msx {

void Diagnostics::add(Severity severity, int line, int column, const std::string& message) {
    Diagnostic d;
    d.severity = severity;
    d.line = line;
    d.column = column;
    d.sample = 0;
    d.message = message;
    items_.push_back(d);
    if (severity == Severity::Error) ++errors_;
    if (severity == Severity::Warning) ++warnings_;
}

void Diagnostics::addAtSample(Severity severity, uint64_t sample, const std::string& message) {
    Diagnostic d;
    d.severity = severity;
    d.line = 0;
    d.column = 0;
    d.sample = sample;
    d.message = message;
    items_.push_back(d);
    if (severity == Severity::Error) ++errors_;
    if (severity == Severity::Warning) ++warnings_;
}

void Diagnostics::info(const std::string& m) { add(Severity::Info, 0, 0, m); }
void Diagnostics::warning(const std::string& m) { add(Severity::Warning, 0, 0, m); }
void Diagnostics::error(const std::string& m) { add(Severity::Error, 0, 0, m); }
void Diagnostics::info(int line, int column, const std::string& m) { add(Severity::Info, line, column, m); }
void Diagnostics::warning(int line, int column, const std::string& m) { add(Severity::Warning, line, column, m); }
void Diagnostics::error(int line, int column, const std::string& m) { add(Severity::Error, line, column, m); }

bool Diagnostics::hasErrors() const { return errors_ != 0; }

std::string Diagnostics::format() const {
    std::ostringstream os;
    for (std::size_t i = 0; i < items_.size(); ++i) {
        const Diagnostic& d = items_[i];
        if (d.line > 0) {
            os << d.line;
            if (d.column > 0) os << ':' << d.column;
            os << ": ";
        } else if (d.sample > 0) {
            os << "sample " << d.sample << ": ";
        }
        switch (d.severity) {
            case Severity::Error: os << "error: "; break;
            case Severity::Warning: os << "warning: "; break;
            case Severity::Info: os << "info: "; break;
        }
        os << d.message << '\n';
    }
    return os.str();
}

std::string formatSeconds(uint64_t sample, unsigned sampleRate) {
    if (sampleRate == 0) return "0.000 s";
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.3f s", static_cast<double>(sample) / sampleRate);
    return buf;
}

std::string hexByte(uint8_t value) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02X", value);
    return buf;
}

}  // namespace msx
