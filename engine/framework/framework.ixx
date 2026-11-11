// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.
module;
#include "framework/reflection.hpp"
export module GameFramework;

import std;
import <cassert>;
export import Transform;
export import MeshResource;

export class Scene;
export class Actor;
export class ActorComponent;
export class CameraComponent;
export class CameraActor;

export class GameObject
{
public:
    GameObject() = default;
    virtual ~GameObject() = default;
    GameObject(const GameObject&) = delete;
    GameObject& operator=(const GameObject&) = delete;
    GameObject(GameObject&&) = delete;
    GameObject& operator=(GameObject&&) = delete;
    virtual void Update(double) {}

    virtual rttr::type get_type() const;
    virtual void* get_ptr() { return this; }
    virtual rttr::detail::derived_info get_derived_info();
    using base_class_list = rttr::type_list<>;
private:
    friend class Scene;
    std::uint64_t updated_frame_ = 0;
};

export class ActorComponent : public GameObject
{
public:
    ActorComponent(Actor& actor, ActorComponent* parent);
    ~ActorComponent() override;
    template<std::derived_from<ActorComponent> T, class... Args>
    T* AddComponent(Args&&... args);
    void Destroy();
    [[nodiscard]] bool IsPendingDestroy() const { return pending_destroy_; }
    virtual void CollectRenderItems(std::vector<MeshDraw>&) const {}
    [[nodiscard]] Actor& GetActor() const { return actor_; }
    [[nodiscard]] ActorComponent* GetParent() const { return parent_; }
    [[nodiscard]] const Transform& GetLocalTransform() const { return transform_; }
    void SetLocalTransform(const Transform& transform);
    [[nodiscard]] glm::mat4 GetWorldMatrix() const;

    rttr::type get_type() const override;
    void* get_ptr() override { return this; }
    rttr::detail::derived_info get_derived_info() override;
    using base_class_list = rttr::type_list<GameObject>;
private:
    friend class Actor;
    friend class Scene;
    void InvalidateTransform();
    void UpdateWorldTransforms(const glm::mat4& parent_world);
    Actor& actor_;
    ActorComponent* const parent_;
    Transform transform_;
    mutable glm::mat4 world_{ 1.0f };
    mutable bool dirty_ = true;
    bool pending_destroy_ = false;
    std::vector<std::unique_ptr<ActorComponent>> children_;
};

export class MeshComponent : public ActorComponent
{
public:
    using ActorComponent::ActorComponent;
    void SetMesh(MeshHandle mesh) { mesh_ = std::move(mesh); }
    void SetColor(glm::vec3 color) { color_ = color; }
    [[nodiscard]] glm::vec3 GetColor() const { return color_; }
    void CollectRenderItems(std::vector<MeshDraw>& items) const override
    {
        if (auto data = mesh_.Data())
            items.push_back({ std::move(data), GetWorldMatrix(), color_ });
    }

    rttr::type get_type() const override;
    void* get_ptr() override { return this; }
    rttr::detail::derived_info get_derived_info() override;
    using base_class_list = rttr::type_list<ActorComponent>;
private:
    MeshHandle mesh_;
    glm::vec3 color_{ 1.0f };
};

export enum class ProjectionMode { Perspective, Orthographic };

export class CameraComponent : public ActorComponent
{
public:
    using ActorComponent::ActorComponent;
    ~CameraComponent() override;
    [[nodiscard]] glm::mat4 GetViewMatrix() const;
    [[nodiscard]] glm::mat4 GetProjectionMatrix(float aspect_ratio) const;
    [[nodiscard]] glm::mat4 GetViewProjectionMatrix(float aspect_ratio) const;
    [[nodiscard]] std::expected<void, const char*> SetPerspective(
        float fov_y_radians, float near_plane, float far_plane);
    [[nodiscard]] std::expected<void, const char*> SetOrthographic(
        float vertical_size, float near_plane, float far_plane);
    [[nodiscard]] ProjectionMode GetProjectionMode() const { return projection_mode_; }
    [[nodiscard]] float GetFieldOfView() const { return fov_y_; }
    [[nodiscard]] float GetOrthographicSize() const { return orthographic_size_; }
    [[nodiscard]] float GetNearPlane() const { return near_plane_; }
    [[nodiscard]] float GetFarPlane() const { return far_plane_; }

    rttr::type get_type() const override;
    void* get_ptr() override { return this; }
    rttr::detail::derived_info get_derived_info() override;
    using base_class_list = rttr::type_list<ActorComponent>;
private:
    ProjectionMode projection_mode_ = ProjectionMode::Perspective;
    float fov_y_ = std::numbers::pi_v<float> / 3.0f;
    float orthographic_size_ = 10.0f;
    float near_plane_ = 0.1f;
    float far_plane_ = 1000.0f;
};

export class Actor : public GameObject
{
public:
    explicit Actor(Scene& scene) : scene_(scene) {}
    ~Actor() override;
    template<std::derived_from<ActorComponent> T, class... Args>
    T* AddComponent(Args&&... args);
    void Destroy();
    [[nodiscard]] bool IsPendingDestroy() const { return pending_destroy_; }
    [[nodiscard]] Scene& GetScene() const { return scene_; }
    [[nodiscard]] const Transform& GetLocalTransform() const { return transform_; }
    void SetLocalTransform(const Transform& transform);
    [[nodiscard]] glm::mat4 GetWorldMatrix() const;

    rttr::type get_type() const override;
    void* get_ptr() override { return this; }
    rttr::detail::derived_info get_derived_info() override;
    using base_class_list = rttr::type_list<GameObject>;
private:
    friend class Scene;
    Scene& scene_;
    Transform transform_;
    mutable glm::mat4 world_{ 1.0f };
    mutable bool dirty_ = true;
    bool pending_destroy_ = false;
    std::vector<std::unique_ptr<ActorComponent>> children_;
};

export class CameraActor : public Actor
{
public:
    explicit CameraActor(Scene& scene);
    // Recreates the default component if it was destroyed; the actor must be live.
    [[nodiscard]] CameraComponent& GetCameraComponent();

    rttr::type get_type() const override;
    void* get_ptr() override { return this; }
    rttr::detail::derived_info get_derived_info() override;
    using base_class_list = rttr::type_list<Actor>;
private:
    friend class CameraComponent;
    CameraComponent* camera_ = nullptr;
};

// All scene operations and object access belong to the main thread.
export class Scene
{
public:
    Scene() = default;
    ~Scene();
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) = delete;
    Scene& operator=(Scene&&) = delete;

    template<std::derived_from<Actor> T, class... Args>
    T* SpawnActor(Args&&... args)
    {
        if (clearing_)
            return nullptr;
        auto actor = std::make_unique<T>(*this, std::forward<Args>(args)...);
        auto* result = actor.get();
        actors_.push_back(std::move(actor));
        return result;
    }
    [[nodiscard]] std::expected<void, const char*> SetActiveCamera(CameraComponent* camera);
    [[nodiscard]] CameraComponent& GetActiveCamera();
    [[nodiscard]] CameraActor& GetDefaultCameraActor();
    void Update(double delta_seconds);
    void Clear();
    void UpdateWorldTransforms();
    [[nodiscard]] std::vector<MeshDraw> CollectRenderItems() const;

private:
    friend class Actor;
    friend class ActorComponent;
    CameraComponent* active_camera_ = nullptr;
    CameraActor* default_camera_actor_ = nullptr;
    void Collect(ActorComponent& component);
    static void RemoveDestroyed(std::vector<std::unique_ptr<ActorComponent>>& children);
    std::vector<std::unique_ptr<Actor>> actors_;
    std::vector<GameObject*> updates_;
    std::uint64_t frame_ = 0;
    bool updating_ = false;
    bool clearing_ = false;
};

template<std::derived_from<ActorComponent> T, class... Args>
T* Actor::AddComponent(Args&&... args)
{
    if (pending_destroy_)
        return nullptr;
    auto child = std::make_unique<T>(*this, nullptr, std::forward<Args>(args)...);
    auto* result = child.get();
    children_.push_back(std::move(child));
    return result;
}

template<std::derived_from<ActorComponent> T, class... Args>
T* ActorComponent::AddComponent(Args&&... args)
{
    if (pending_destroy_)
        return nullptr;
    auto child = std::make_unique<T>(actor_, this, std::forward<Args>(args)...);
    auto* result = child.get();
    children_.push_back(std::move(child));
    return result;
}
