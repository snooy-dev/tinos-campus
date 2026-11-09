// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.
export module Transform;
export import <glm/glm.hpp>;
export import <glm/gtc/quaternion.hpp>;
import <glm/gtc/matrix_transform.hpp>;

export struct Transform
{
    glm::vec3 position{ 0.0f };
    glm::quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
    glm::vec3 scale{ 1.0f };

    [[nodiscard]] glm::mat4 ToMatrix() const
    {
        return glm::translate(glm::mat4{ 1.0f }, position) *
            glm::mat4_cast(rotation) * glm::scale(glm::mat4{ 1.0f }, scale);
    }
};