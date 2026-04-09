#include "UIManager.h"
#include "Core/Log.h"
#include "Core/Window.h"
#include "Core/RHI/Vulkan/VulkanContext.h"

namespace Core {
namespace UI {

// ============================================================================
// Singleton Instance
// ============================================================================

UIManager& UIManager::Get() {
    static UIManager instance;
    return instance;
}

UIManager::~UIManager() {
    if (m_Initialized) {
        Shutdown();
    }
}

// ============================================================================
// Lifecycle
// ============================================================================

void UIManager::Initialize(RHI::VulkanContext* vulkanContext, Window* window, VkRenderPass renderPass) {
    if (m_Initialized) {
        ENGINE_CORE_WARN("UIManager::Initialize - Already initialized");
        return;
    }

    m_VulkanContext = vulkanContext;
    m_Window = window;

    // Get initial viewport size
    int width, height;
    SDL_GetWindowSize(window->GetSDLWindow(), &width, &height);
    m_ViewportSize = {static_cast<float>(width), static_cast<float>(height)};

    // Initialize ImGui subsystem
    m_ImGui = std::make_unique<ImGuiSubsystem>();
    if (!m_ImGui->Initialize(vulkanContext, window)) {
        ENGINE_CORE_ERROR("UIManager::Initialize - Failed to initialize ImGui");
        Shutdown();
        return;
    }

    // Initialize text renderer
    m_TextRenderer = std::make_unique<TextRenderer>();
    m_TextRenderer->Initialize(vulkanContext, renderPass);
    m_TextRenderer->SetViewportSize(static_cast<uint32_t>(m_ViewportSize.x), 
                                     static_cast<uint32_t>(m_ViewportSize.y));

    // Initialize FontManager
    FontManager::Get().Initialize(vulkanContext);

    m_Initialized = true;
    ENGINE_CORE_INFO("UIManager initialized (viewport: {}x{})", 
                     static_cast<int>(m_ViewportSize.x), 
                     static_cast<int>(m_ViewportSize.y));
}

void UIManager::Shutdown() {
    if (!m_Initialized) {
        return;
    }

    // Clear messages
    m_ActiveMessages.clear();

    // Shutdown subsystems in reverse order
    if (m_TextRenderer) {
        m_TextRenderer->Shutdown();
        m_TextRenderer.reset();
    }

    FontManager::Get().Shutdown();

    if (m_ImGui) {
        m_ImGui->Shutdown();
        m_ImGui.reset();
    }

    m_VulkanContext = nullptr;
    m_Window = nullptr;
    m_Initialized = false;

    ENGINE_CORE_INFO("UIManager shut down");
}

// ============================================================================
// Per-Frame Operations
// ============================================================================

void UIManager::BeginFrame() {
    if (!m_Initialized || m_InFrame) {
        return;
    }

    m_InFrame = true;

    if (m_ImGui) {
        m_ImGui->BeginFrame();
    }
}

void UIManager::Update(float deltaTime) {
    if (!m_Initialized) {
        return;
    }

    m_DeltaTime = deltaTime;

    // Update messages (remove expired)
    for (auto it = m_ActiveMessages.begin(); it != m_ActiveMessages.end();) {
        it->elapsed += deltaTime;
        if (it->elapsed >= it->duration) {
            it = m_ActiveMessages.erase(it);
        } else {
            ++it;
        }
    }
}

void UIManager::Render(VkCommandBuffer commandBuffer) {
    if (!m_Initialized || !m_InFrame) {
        return;
    }

    // Render messages first (below ImGui)
    RenderMessages();

    // Flush text renderer
    if (m_TextRenderer) {
        m_TextRenderer->Flush(commandBuffer);
    }

    // Render ImGui last (on top)
    if (m_ImGui) {
        m_ImGui->Render(commandBuffer);
    }
}

void UIManager::EndFrame() {
    if (!m_Initialized || !m_InFrame) {
        return;
    }

    if (m_ImGui) {
        m_ImGui->EndFrame();
    }

    m_InFrame = false;
}

// ============================================================================
// Quick Text API
// ============================================================================

void UIManager::DrawText(std::string_view text, glm::vec2 screenPos, const TextStyle& style) {
    if (m_TextRenderer) {
        m_TextRenderer->DrawText(text, screenPos, style);
    }
}

void UIManager::DrawText3D(std::string_view text, glm::vec3 worldPos, const glm::mat4& viewProj,
                           const TextStyle& style, bool billboard) {
    if (m_TextRenderer) {
        m_TextRenderer->DrawText3D(text, worldPos, viewProj, style, billboard);
    }
}

void UIManager::DrawTextAnchored(std::string_view text, Anchor anchor, glm::vec2 offset,
                                  const TextStyle& style) {
    if (m_TextRenderer) {
        m_TextRenderer->DrawTextAnchored(text, anchor, offset, style);
    }
}

// ============================================================================
// Screen Messages
// ============================================================================

void UIManager::ShowMessage(std::string_view text, float duration, DisplayMessage::Type type) {
    constexpr size_t MAX_MESSAGE_LENGTH = 1024;
    constexpr float MIN_DURATION = 0.1f;
    constexpr float MAX_DURATION = 60.0f;
    
    DisplayMessage msg;
    
    // Truncate if too long
    if (text.length() > MAX_MESSAGE_LENGTH) {
        msg.text = std::string(text.substr(0, MAX_MESSAGE_LENGTH)) + "...";
        ENGINE_CORE_WARN("UIManager: Message truncated from {} to {} chars", 
                        text.length(), MAX_MESSAGE_LENGTH);
    } else {
        msg.text = std::string(text);
    }
    
    // Validate duration is reasonable
    msg.duration = std::clamp(duration, MIN_DURATION, MAX_DURATION);
    msg.type = type;
    msg.color = GetMessageTypeColor(type);
    
    ShowMessage(msg);
}

void UIManager::ShowMessage(const DisplayMessage& message) {
    if (!m_Initialized) {
        return;
    }

    // Enforce max messages
    while (m_ActiveMessages.size() >= m_MaxMessages) {
        // Remove oldest message
        m_ActiveMessages.erase(m_ActiveMessages.begin());
    }

    m_ActiveMessages.push_back(message);
    ENGINE_CORE_INFO("UIManager: Showing message '{}'", message.text);
}

void UIManager::ClearMessages() {
    m_ActiveMessages.clear();
}

glm::vec4 UIManager::GetMessageTypeColor(DisplayMessage::Type type) const {
    switch (type) {
        case DisplayMessage::Type::Info:
            return {1.0f, 1.0f, 1.0f, 1.0f};  // White
        case DisplayMessage::Type::Warning:
            return {1.0f, 0.9f, 0.2f, 1.0f};  // Yellow
        case DisplayMessage::Type::Error:
            return {1.0f, 0.3f, 0.3f, 1.0f};  // Red
        case DisplayMessage::Type::Narrative:
            return {0.9f, 0.9f, 0.8f, 1.0f};  // Warm white
        case DisplayMessage::Type::Tutorial:
            return {0.4f, 0.8f, 1.0f, 1.0f};  // Light blue
        case DisplayMessage::Type::Success:
            return {0.3f, 1.0f, 0.4f, 1.0f};  // Green
        default:
            return {1.0f, 1.0f, 1.0f, 1.0f};
    }
}

glm::vec2 UIManager::GetMessagePosition(const DisplayMessage& msg, size_t index) const {
    float y = 0.0f;
    float spacing = msg.fontSize * 1.5f;

    switch (msg.position) {
        case Anchor::TopCenter:
        case Anchor::TopLeft:
        case Anchor::TopRight:
            y = 50.0f + index * spacing;
            break;
        case Anchor::BottomCenter:
        case Anchor::BottomLeft:
        case Anchor::BottomRight:
            y = m_ViewportSize.y - 100.0f - index * spacing;
            break;
        default:  // Center
            y = m_ViewportSize.y * 0.4f + index * spacing;
            break;
    }

    return {m_ViewportSize.x * 0.5f, y};
}

void UIManager::RenderMessages() {
    if (!m_TextRenderer || m_ActiveMessages.empty()) {
        return;
    }

    for (size_t i = 0; i < m_ActiveMessages.size(); ++i) {
        const auto& msg = m_ActiveMessages[i];
        
        // Calculate fade alpha
        float alpha = 1.0f;
        float fadeTime = 0.3f;
        
        // Fade in
        if (msg.elapsed < fadeTime) {
            alpha = msg.elapsed / fadeTime;
        }
        // Fade out
        else if (msg.elapsed > msg.duration - fadeTime) {
            alpha = (msg.duration - msg.elapsed) / fadeTime;
        }

        TextStyle style;
        style.fontSize = msg.fontSize;
        style.color = glm::vec4(msg.color.r, msg.color.g, msg.color.b, msg.color.a * alpha);
        style.hAlign = HorizontalAlign::Center;
        style.vAlign = VerticalAlign::Center;
        
        if (msg.outline) {
            style.outlineWidth = 1.5f;
            style.outlineColor = {0.0f, 0.0f, 0.0f, alpha};
        }

        glm::vec2 pos = GetMessagePosition(msg, i);
        m_TextRenderer->DrawText(msg.text, pos, style);
    }
}

// ============================================================================
// Viewport Management
// ============================================================================

void UIManager::OnResize(uint32_t width, uint32_t height) {
    m_ViewportSize = {static_cast<float>(width), static_cast<float>(height)};
    
    if (m_TextRenderer) {
        m_TextRenderer->SetViewportSize(width, height);
    }
}

// ============================================================================
// Input Handling
// ============================================================================

bool UIManager::ProcessEvent(const SDL_Event& event) {
    if (!m_Initialized) {
        return false;
    }

    if (m_ImGui) {
        return m_ImGui->ProcessEvent(event);
    }
    return false;
}

bool UIManager::WantsKeyboardInput() const {
    if (m_ImGui) {
        return m_ImGui->WantsKeyboardInput();
    }
    return false;
}

bool UIManager::WantMouseInput() const {
    if (m_ImGui) {
        return m_ImGui->WantMouseInput();
    }
    return false;
}

// ============================================================================
// Configuration
// ============================================================================

void UIManager::SetDebugOverlayEnabled(bool enabled) {
    if (m_ImGui) {
        m_ImGui->GetConfig().showPerformance = enabled;
    }
}

} // namespace UI
} // namespace Core
