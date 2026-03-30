#pragma once

#include <string>
#include <functional>
#include <SDL3/SDL.h>
#include "Core/Event.h"

namespace Core {

    struct WindowProps {
        std::string Title;
        uint32_t Width;
        uint32_t Height;

        WindowProps(const std::string& title = "AIGameEngine",
                    uint32_t width = 1280,
                    uint32_t height = 720)
            : Title(title), Width(width), Height(height) {}
    };

    class Window {
    public:
        Window(const WindowProps& props);
        ~Window();

        // Disallow copying
        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;

        void OnUpdate();

        inline uint32_t GetWidth() const { return m_Data.Width; }
        inline uint32_t GetHeight() const { return m_Data.Height; }
        inline SDL_Window* GetNativeWindow() const { return m_Window; }
        
        using EventCallbackFn = std::function<void(Event&)>;
        inline void SetEventCallback(const EventCallbackFn& callback) { m_Data.EventCallback = callback; }

    private:
        void Init(const WindowProps& props);
        void Shutdown();

    private:
        SDL_Window* m_Window;

        struct WindowData {
            std::string Title;
            uint32_t Width, Height;
            EventCallbackFn EventCallback;
        };

        WindowData m_Data;
    };

} // namespace Core
