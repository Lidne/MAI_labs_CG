#pragma once

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <vector>
#include <veekay/application.hpp>
#include <veekay/input.hpp>

class Camera {
   public:
    Camera(float verticalFOV, float nearClip, float farClip, uint32_t vp_width, uint32_t vp_height, bool lookAt)
        : m_VerticalFOV(verticalFOV), m_NearClip(nearClip), m_FarClip(farClip), m_LastLookAt(lookAt) {
        m_ForwardDirection = glm::vec3(0, 0, -1);
        m_Position = glm::vec3(0, -1, 6);

        OnResize(vp_width, vp_height);
        RecalculateViewLookAt();
        RecalculateProjection();
    }

    void OnUpdate(bool useLookAt, bool& fpsMode, float ts) {
        using namespace veekay::input;

        auto vkMousePos = mouse::cursorPosition();
        glm::vec2 mousePos = {vkMousePos.x, vkMousePos.y};
        glm::vec2 delta = (mousePos - m_LastMousePosition) * 0.002f;
        m_LastMousePosition = mousePos;

        if (!mouse::isButtonDown(mouse::Button::right)) {
            glfwSetInputMode(veekay::app.window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            fpsMode = false;
            return;
        }

        fpsMode = true;
        glfwSetInputMode(veekay::app.window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);

        constexpr glm::vec3 upDirection(0.0f, 1.0f, 0.0f);
        glm::vec3 rightDirection = glm::cross(m_ForwardDirection, upDirection);

        float speed = .1f;

        // Movement
        if (keyboard::isKeyDown(keyboard::Key::w)) {
            m_Position += m_ForwardDirection * speed;
        } else if (keyboard::isKeyDown(keyboard::Key::s)) {
            m_Position -= m_ForwardDirection * speed;
        }
        if (keyboard::isKeyDown(keyboard::Key::a)) {
            m_Position -= rightDirection * speed;
        } else if (keyboard::isKeyDown(keyboard::Key::d)) {
            m_Position += rightDirection * speed;
        }
        if (keyboard::isKeyDown(keyboard::Key::q)) {
            m_Position -= upDirection * speed;
        } else if (keyboard::isKeyDown(keyboard::Key::e)) {
            m_Position += upDirection * speed;
        }

        if (delta.x != 0.0f || delta.y != 0.0f) {
            float pitch = -delta.y * GetRotationSpeed();
            float yaw = delta.x * GetRotationSpeed();

            m_Pitch += pitch;
            m_Yaw += yaw;

            if (useLookAt) {
                glm::quat q = glm::normalize(glm::cross(glm::angleAxis(-pitch, rightDirection),
                                                        glm::angleAxis(-yaw, glm::vec3(0.f, 1.0f, 0.0f))));
                m_ForwardDirection = glm::rotate(q, m_ForwardDirection);
            }
        }

        if (!useLookAt) {
            glm::quat q = glm::normalize(glm::cross(glm::angleAxis(-m_Pitch, glm::vec3(-1.f, 0.0f, 0.0f)),
                                                    glm::angleAxis(-m_Yaw, glm::vec3(0.f, 1.0f, 0.0f))));
            m_ForwardDirection = glm::rotate(q, glm::vec3(0.f, 0.0f, 1.0f));
        }

        if (useLookAt)
            RecalculateViewLookAt();
        else
            RecalculateViewMatrix();
        RecalculateProjection();
    }

    void OnResize(uint32_t width, uint32_t height) {
        if (width == m_ViewportWidth && height == m_ViewportHeight)
            return;

        m_ViewportWidth = width;
        m_ViewportHeight = height;

        RecalculateProjection();
    }

    const glm::mat4& GetProjection() const { return m_Projection; }
    const glm::mat4& GetView() const { return m_View; }

    glm::vec2 GetOrientation() const { return glm::vec2(m_Pitch, m_Yaw); }

    glm::vec3& GetPosition() { return m_Position; }
    const glm::vec3& GetDirection() const { return m_ForwardDirection; }

    float GetRotationSpeed() const { return 0.3f; }

    void SyncBeforeSwitch(bool newLookAt) {
        if (newLookAt) {
            m_Position_LookAt = m_Position;
            m_ForwardDirection_LookAt = m_ForwardDirection;
            m_Position = m_Position_Free;
            m_Pitch = m_Pitch_Free;
            m_Yaw = m_Yaw_Free;
            RecalculateViewMatrix();
        } else {
            m_Position_Free = m_Position;
            m_Pitch_Free = m_Pitch;
            m_Yaw_Free = m_Yaw;
            m_Position = m_Position_LookAt;
            m_ForwardDirection = m_ForwardDirection_LookAt;
            RecalculateViewLookAt();
        }
    }

   private:
    void RecalculateProjection() {
        m_Projection = glm::perspectiveFov(glm::radians(m_VerticalFOV), (float)m_ViewportWidth, (float)m_ViewportHeight, m_NearClip, m_FarClip);
    }

    void RecalculateViewMatrix() {
        glm::mat4 rotation =
            glm::rotate(glm::mat4(1.0f), m_Pitch, glm::vec3(1, 0, 0)) *
            glm::rotate(glm::mat4(1.0f), m_Yaw, glm::vec3(0, 1, 0));

        glm::mat4 translation = glm::translate(glm::mat4(1.0f), m_Position);

        m_View = rotation * translation;
    }

    void RecalculateViewLookAt() {
        m_View = glm::lookAt(m_Position, m_Position + m_ForwardDirection, glm::vec3(0, 1, 0));
    }

   private:
    bool m_LastLookAt;

    glm::mat4 m_Projection{1.0f};
    glm::mat4 m_View{1.0f};

    float m_VerticalFOV = 45.0f;
    float m_NearClip = 0.1f;
    float m_FarClip = 100.0f;

    float m_Pitch = 0.0f;
    float m_Yaw = 0.0f;

    float m_Pitch_Free = 0.0f;
    float m_Yaw_Free = 0.0f;

    glm::vec3 m_Position{0.0f, 0.0f, 1.0f};
    glm::vec3 m_Position_LookAt{0.0f, 0.0f, 0.0f};
    glm::vec3 m_Position_Free{0.0f, 0.0f, 0.0f};

    glm::vec3 m_ForwardDirection{0.0f, 0.0f, 1.0f};
    glm::vec3 m_ForwardDirection_LookAt{0.0f, 0.0f, 1.0f};

    glm::vec2 m_LastMousePosition{0.0f, 0.0f};

    uint32_t m_ViewportWidth = 0, m_ViewportHeight = 0;
};
