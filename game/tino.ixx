// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.
export module TinoCharacter;

import std;
import GameFramework;
import Input;

export class Tino final : public Character
{
public:
    Tino(Scene& scene, MeshHandle mesh) : Character(scene)
    {
        AddComponent<MeshComponent>()->SetMesh(std::move(mesh), "tino.glb");

        arm_ = AddComponent<CameraArmComponent>();
        Transform pivot;
        pivot.position.y = 1.0f;
        arm_->SetLocalTransform(pivot);
        arm_->SetOrbit(yaw_, pitch_);
        camera_ = arm_->AttachCamera(5.0f);
    }

    [[nodiscard]] CameraComponent* GetCamera() const { return camera_; }
    void SetInput(const Input& input) { input_ = input; }

    void Update(double delta_seconds) override
    {
        if (input_.orbit_x != 0.0 || input_.orbit_y != 0.0)
        {
            constexpr float sensitivity = glm::radians(0.2f);
            constexpr float pitch_limit = glm::radians(80.0f);
            yaw_ = std::remainder(yaw_ - static_cast<float>(input_.orbit_x) * sensitivity,
                2.0f * std::numbers::pi_v<float>);
            pitch_ = std::clamp(pitch_ - static_cast<float>(input_.orbit_y) * sensitivity,
                -pitch_limit, pitch_limit);
            arm_->SetOrbit(yaw_, pitch_);
        }

        const glm::vec3 direction{ static_cast<float>(input_.move_right), 0.0f,
            -static_cast<float>(input_.move_forward) };
        if (direction != glm::vec3{ 0.0f })
        {
            const auto heading = glm::angleAxis(yaw_, glm::vec3{ 0.0f, 1.0f, 0.0f });
            Move(heading * glm::normalize(direction) * 3.0f, delta_seconds);
        }

        input_ = {};
    }

private:
    Input input_;
    float yaw_ = 0.0f;
    float pitch_ = glm::radians(-20.0f);
    CameraArmComponent* arm_ = nullptr;
    CameraComponent* camera_ = nullptr;
};
