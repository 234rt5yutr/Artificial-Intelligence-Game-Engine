#pragma once

#include "Core/UI/Widgets/Widget.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace Core {
namespace UI {
namespace Binding {

    constexpr const char* UI_BINDING_INVALID_WIDGET = "UI_BINDING_INVALID_WIDGET";
    constexpr const char* UI_BINDING_INVALID_PROPERTY_PATH = "UI_BINDING_INVALID_PROPERTY_PATH";
    constexpr const char* UI_BINDING_INVALID_DATA_PATH = "UI_BINDING_INVALID_DATA_PATH";
    constexpr const char* UI_BINDING_CONVERTER_NOT_FOUND = "UI_BINDING_CONVERTER_NOT_FOUND";
    constexpr const char* UI_BINDING_VALIDATOR_NOT_FOUND = "UI_BINDING_VALIDATOR_NOT_FOUND";
    constexpr const char* UI_BINDING_CYCLE_REJECTED = "UI_BINDING_CYCLE_REJECTED";
    constexpr const char* UI_BINDING_SET_FAILED = "UI_BINDING_SET_FAILED";

    enum class BindingMode : uint8_t {
        OneWay = 0,
        TwoWay
    };

    struct BindingRegistrationRequest {
        std::string WidgetId;
        std::string WidgetPropertyPath;
        std::string DataPath;
        BindingMode Mode = BindingMode::OneWay;
        std::string ConverterName;
        std::string ValidatorName;
    };

    struct BindingRegistrationResult {
        bool Success = false;
        std::string ErrorCode;
        std::string Message;
        uint64_t BindingHandle = 0;
    };

    struct UIBindingDiagnostics {
        uint32_t ActiveBindings = 0;
        uint64_t UpdateIterations = 0;
        uint64_t ValidationFailures = 0;
        uint64_t CycleRejections = 0;
        uint64_t AutoCleanupRemovals = 0;
        uint64_t TransitionSuppressions = 0;
    };

    using WidgetPropertyValue = Widget::PropertyValue;
    using BindingConverter = std::function<std::optional<WidgetPropertyValue>(
        const WidgetPropertyValue&)>;
    using BindingValidator = std::function<bool(const WidgetPropertyValue&)>;

} // namespace Binding
} // namespace UI
} // namespace Core

