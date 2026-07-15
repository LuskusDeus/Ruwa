// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   C O M M O N   R E S U L T   T Y P E
// ==========================================================================

#ifndef RUWA_CORE_COMMON_RESULT_H
#define RUWA_CORE_COMMON_RESULT_H

#include <string>
#include <variant>
#include <optional>

namespace aether {

// ==========================================================================
//   E R R O R   C O D E S
// ==========================================================================

enum class ErrorCode {
    None = 0,

    // Vulkan initialization errors
    VulkanNotAvailable,
    VulkanInstanceCreationFailed,
    VulkanDeviceCreationFailed,
    VulkanSurfaceCreationFailed,
    VulkanSwapchainCreationFailed,

    // Resource errors
    ShaderCompilationFailed,
    PipelineCreationFailed,
    BufferAllocationFailed,
    ImageAllocationFailed,

    // Runtime errors
    SwapchainOutOfDate,
    RenderingFailed,
    SynchronizationFailed,

    // General errors
    InvalidArgument,
    OutOfMemory,
    Unknown
};

// ==========================================================================
//   E R R O R
// ==========================================================================

struct Error {
    ErrorCode code = ErrorCode::None;
    std::string message;

    Error() = default;
    Error(ErrorCode c, std::string msg = "")
        : code(c)
        , message(std::move(msg))
    {
    }

    explicit operator bool() const { return code != ErrorCode::None; }

    bool isOk() const { return code == ErrorCode::None; }
    bool isFailed() const { return code != ErrorCode::None; }
};

// ==========================================================================
//   R E S U L T < T >
// ==========================================================================

template <typename T> class Result {
public:
    Result(T value)
        : m_data(std::move(value))
    {
    }
    Result(Error error)
        : m_data(std::move(error))
    {
    }
    Result(ErrorCode code, std::string message = "")
        : m_data(Error { code, std::move(message) })
    {
    }

    bool isOk() const { return std::holds_alternative<T>(m_data); }

    bool isFailed() const { return std::holds_alternative<Error>(m_data); }

    explicit operator bool() const { return isOk(); }

    T& value() { return std::get<T>(m_data); }

    const T& value() const { return std::get<T>(m_data); }

    T valueOr(T defaultValue) const
    {
        if (isOk()) {
            return std::get<T>(m_data);
        }
        return defaultValue;
    }

    Error& error() { return std::get<Error>(m_data); }

    const Error& error() const { return std::get<Error>(m_data); }

    T* operator->() { return &std::get<T>(m_data); }

    const T* operator->() const { return &std::get<T>(m_data); }

    T& operator*() { return std::get<T>(m_data); }

    const T& operator*() const { return std::get<T>(m_data); }

private:
    std::variant<T, Error> m_data;
};

// ==========================================================================
//   R E S U L T < V O I D >   S P E C I A L I Z A T I O N
// ==========================================================================

template <> class Result<void> {
public:
    Result()
        : m_error(std::nullopt)
    {
    }
    Result(Error error)
        : m_error(std::move(error))
    {
    }
    Result(ErrorCode code, std::string message = "")
        : m_error(Error { code, std::move(message) })
    {
    }

    static Result ok() { return Result(); }

    bool isOk() const { return !m_error.has_value(); }

    bool isFailed() const { return m_error.has_value(); }

    explicit operator bool() const { return isOk(); }

    Error& error() { return *m_error; }

    const Error& error() const { return *m_error; }

private:
    std::optional<Error> m_error;
};

// Convenience alias
using VoidResult = Result<void>;

} // namespace aether

#endif // RUWA_CORE_COMMON_RESULT_H
