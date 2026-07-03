#pragma once

#include <QString>

#include <utility>
#include <variant>

namespace tlm {

/** Describes a recoverable operation failure with a user-facing message. */
struct Error {
    QString message;
    QString details;
};

/** Small std::expected-like result type used by persistence and asset operations. */
template <typename T>
class Result {
public:
    static Result success(T value) { return Result(std::move(value)); }
    static Result failure(QString message, QString details = {}) {
        return Result(Error{std::move(message), std::move(details)});
    }

    bool hasValue() const { return std::holds_alternative<T>(m_storage); }
    explicit operator bool() const { return hasValue(); }

    const T& value() const { return std::get<T>(m_storage); }
    T& value() { return std::get<T>(m_storage); }
    T takeValue() { return std::move(std::get<T>(m_storage)); }

    const Error& error() const { return std::get<Error>(m_storage); }

private:
    explicit Result(T value) : m_storage(std::move(value)) {}
    explicit Result(Error error) : m_storage(std::move(error)) {}

    std::variant<T, Error> m_storage;
};

template <>
class Result<void> {
public:
    static Result success() { return Result(true); }
    static Result failure(QString message, QString details = {}) {
        return Result(Error{std::move(message), std::move(details)});
    }

    bool hasValue() const { return m_ok; }
    explicit operator bool() const { return hasValue(); }
    const Error& error() const { return m_error; }

private:
    explicit Result(bool ok) : m_ok(ok) {}
    explicit Result(Error error) : m_ok(false), m_error(std::move(error)) {}

    bool m_ok{false};
    Error m_error;
};

} // namespace tlm

