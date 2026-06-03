#ifndef GDEXTENSION_CPP_EXAMPLE_LOGGER_H
#define GDEXTENSION_CPP_EXAMPLE_LOGGER_H

#include <godot_cpp/variant/string.hpp>

namespace godot_rt {

    enum class LogLevel {
        Info,
        Warning,
        Error,
    };

    class Logger {
    public:
        static void log(LogLevel level, const godot::String& message);
        static void info(const godot::String& message);
        static void warning(const godot::String& message);
        static void error(const godot::String& message);
    };

}

#endif // GDEXTENSION_CPP_EXAMPLE_LOGGER_H
