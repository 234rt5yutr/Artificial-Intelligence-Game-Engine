#pragma once

#include <string>
#include <utility>

namespace Core {
namespace Editor {

    template <typename T>
    struct Result {
        bool Ok = false;
        T Value{};
        std::string Error;

        static Result Success(T value) {
            Result result;
            result.Ok = true;
            result.Value = std::move(value);
            return result;
        }

        static Result Failure(std::string error) {
            Result result;
            result.Ok = false;
            result.Error = std::move(error);
            return result;
        }
    };

    template <>
    struct Result<void> {
        bool Ok = false;
        std::string Error;

        static Result Success() {
            Result result;
            result.Ok = true;
            return result;
        }

        static Result Failure(std::string error) {
            Result result;
            result.Ok = false;
            result.Error = std::move(error);
            return result;
        }
    };

} // namespace Editor
} // namespace Core

