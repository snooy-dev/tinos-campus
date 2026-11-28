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
        body_ = AddComponent<MeshComponent>();
        body_->SetMesh(std::move(mesh), "tino.glb");

        arm_ = AddComponent<CameraArmComponent>();
        Transform pivot;
        pivot.position.y = 1.0f;
        arm_->SetLocalTransform(pivot);
        arm_->SetOrbit(camera_yaw_, pitch_);
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
            camera_yaw_ = std::remainder(camera_yaw_ - static_cast<float>(input_.orbit_x) * sensitivity,
                2.0f * std::numbers::pi_v<float>);
            pitch_ = std::clamp(pitch_ - static_cast<float>(input_.orbit_y) * sensitivity,
                -pitch_limit, pitch_limit);
            arm_->SetOrbit(camera_yaw_, pitch_);
        }

        const glm::vec3 direction{ static_cast<float>(input_.move_right), 0.0f,
            -static_cast<float>(input_.move_forward) };
        if (direction != glm::vec3{ 0.0f })
        {
            const glm::vec3 up{ 0.0f, 1.0f, 0.0f };
            const auto camera_heading = glm::angleAxis(camera_yaw_, up);
            const auto movement = camera_heading * glm::normalize(direction);
            Move(movement * 3.0f, delta_seconds);
            walk_time_ += delta_seconds;
            body_->SetAnimationTime(walk_time_);

            // The Tino mesh faces local +Z.
            const float target_yaw = std::atan2(movement.x, movement.z);
            constexpr float full_turn = 2.0f * std::numbers::pi_v<float>;
            const float remaining = std::remainder(target_yaw - body_yaw_, full_turn);
            constexpr float turn_speed = glm::radians(540.0f);
            const float max_turn = turn_speed * static_cast<float>(delta_seconds);
            body_yaw_ = std::remainder(body_yaw_ + std::clamp(remaining, -max_turn, max_turn),
                full_turn);

            auto body_transform = body_->GetLocalTransform();
            body_transform.rotation = glm::angleAxis(body_yaw_, up);
            body_->SetLocalTransform(body_transform);
        }
        else
        {
            walk_time_ = 0.0;
            body_->SetAnimationTime(std::nullopt);
        }

        input_ = {};
    }

private:
    Input input_;
    double walk_time_ = 0.0;
    float camera_yaw_ = 0.0f;
    float pitch_ = glm::radians(-20.0f);
    float body_yaw_ = 0.0f;
    MeshComponent* body_ = nullptr;
    CameraArmComponent* arm_ = nullptr;
    CameraComponent* camera_ = nullptr;
};
