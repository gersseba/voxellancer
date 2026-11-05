#include <glow/AbstractLogHandler.h>
#include <glow/LogMessage.h>
#include <glow/LogMessageBuilder.h>

#include <cstring>

namespace glow {

LogMessageBuilder::LogMessageBuilder(
    LogMessage::Level level,
    AbstractLogHandler* handler
) :
    m_level(level),
    m_handler(handler),
    m_stream(new std::stringstream)
{
}

LogMessageBuilder::LogMessageBuilder(const LogMessageBuilder& builder) :
    m_level(builder.m_level),
    m_handler(builder.m_handler),
    m_stream(builder.m_stream)
{
}

LogMessageBuilder::~LogMessageBuilder() {
    if (m_handler) {
        m_handler->handle(LogMessage(m_level, m_stream->str()));
    }
}

LogMessageBuilder& LogMessageBuilder::operator<<(const char* value) {
    if (value) {
        m_stream->write(value, std::strlen(value));
    }
    return *this;
}

LogMessageBuilder& LogMessageBuilder::operator<<(const std::string& value) {
    m_stream->write(value.c_str(), static_cast<std::streamsize>(value.length()));
    return *this;
}

LogMessageBuilder& LogMessageBuilder::operator<<(bool value) {
    *m_stream << (value ? "true" : "false");
    return *this;
}

LogMessageBuilder& LogMessageBuilder::operator<<(char value) {
    *m_stream << value;
    return *this;
}

LogMessageBuilder& LogMessageBuilder::operator<<(int value) {
    *m_stream << value;
    return *this;
}

LogMessageBuilder& LogMessageBuilder::operator<<(float value) {
    *m_stream << value;
    return *this;
}

LogMessageBuilder& LogMessageBuilder::operator<<(double value) {
    *m_stream << value;
    return *this;
}

LogMessageBuilder& LogMessageBuilder::operator<<(long double value) {
    *m_stream << value;
    return *this;
}

LogMessageBuilder& LogMessageBuilder::operator<<(unsigned value) {
    *m_stream << value;
    return *this;
}

LogMessageBuilder& LogMessageBuilder::operator<<(long value) {
    *m_stream << value;
    return *this;
}

LogMessageBuilder& LogMessageBuilder::operator<<(long long value) {
    *m_stream << value;
    return *this;
}

LogMessageBuilder& LogMessageBuilder::operator<<(unsigned long value) {
    *m_stream << value;
    return *this;
}

LogMessageBuilder& LogMessageBuilder::operator<<(unsigned char value) {
    *m_stream << value;
    return *this;
}

LogMessageBuilder& LogMessageBuilder::operator<<(const void* value) {
    *m_stream << value;
    return *this;
}

LogMessageBuilder& LogMessageBuilder::operator<<(
    std::ostream& (*manipulator)(std::ostream&)
) {
    *m_stream << manipulator;
    return *this;
}

LogMessageBuilder& LogMessageBuilder::operator<<(
    PrecisionManipulator manipulator
) {
    *m_stream << manipulator;
    return *this;
}

LogMessageBuilder& LogMessageBuilder::operator<<(FillManipulator manipulator) {
    *m_stream << manipulator;
    return *this;
}

#ifndef _MSC_VER
LogMessageBuilder& LogMessageBuilder::operator<<(WidthManipulator manipulator) {
    *m_stream << manipulator;
    return *this;
}
#endif

} // namespace glow
