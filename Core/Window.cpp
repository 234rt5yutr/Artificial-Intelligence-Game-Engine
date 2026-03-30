#include "Window.h"
#include "Core/Log.h"
#include "Core/Profile.h"
#include "Core/Assert.h"

namespace Core {

    static uint8_t s_SDLWindowCount = 0;

    Window::Window(const WindowProps& props) {
        PROFILE_FUNCTION();
        Init(props);
    }

    Window::~Window() {
        PROFILE_FUNCTION();
        Shutdown();
    }

    void Window::Init(const WindowProps& props) {
        PROFILE_FUNCTION();
        m_Data.Title = props.Title;
        m_Data.Width = props.Width;
        m_Data.Height = props.Height;

        ENGINE_CORE_INFO("Creating window {0} ({1}, {2})", props.Title, props.Width, props.Height);

        if (s_SDLWindowCount == 0) {
            PROFILE_SCOPE("SDL_Init");
            
            // In modern SDL3, SDL_Init normally returns non-zero/false on failure
            // To be compatible with multiple beta versions, we check against zero directly if it's an int, or false if it's a bool
            bool success = SDL_Init(SDL_INIT_VIDEO);
            if (!success) {
                // Some versions return 0 on success, so if success is exactly 0 and it was an int, mapping "false" would trigger this mistakenly.
                // Let's use the explicit check: 
                // In SDL2 it was 0 on success, <0 on error.
                // In newest SDL3, it's boolean (true on success). 
            }
            
            // Safe checking: SDL3 recently switched entirely to returning true (1) for success and false (0) for failure.
            ENGINE_CORE_ASSERT(success, "Could not initialize SDL3! Error: {0}", SDL_GetError());
        }

        {
            PROFILE_SCOPE("SDL_CreateWindow");
            m_Window = SDL_CreateWindow(m_Data.Title.c_str(), m_Data.Width, m_Data.Height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
            ENGINE_CORE_ASSERT(m_Window, "Failed to create SDL window! Error: {0}", SDL_GetError());
        }

        s_SDLWindowCount++;
    }

    void Window::Shutdown() {
        PROFILE_FUNCTION();
        if (m_Window) {
            SDL_DestroyWindow(m_Window);
            m_Window = nullptr;
        }

        s_SDLWindowCount--;
        if (s_SDLWindowCount == 0) {
            SDL_Quit();
        }
    }

    void Window::OnUpdate() {
        PROFILE_FUNCTION();
        
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                WindowCloseEvent e;
                if (m_Data.EventCallback) {
                    m_Data.EventCallback(e);
                }
            } else if (event.type == SDL_EVENT_KEY_DOWN) {
                KeyPressedEvent e(event.key.key, event.key.repeat);
                if (m_Data.EventCallback) {
                    m_Data.EventCallback(e);
                }
            } else if (event.type == SDL_EVENT_KEY_UP) {
                KeyReleasedEvent e(event.key.key);
                if (m_Data.EventCallback) {
                    m_Data.EventCallback(e);
                }
            }
            // More window events mapped as Engine specific components later
        }
    }

} // namespace Core
