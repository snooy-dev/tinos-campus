// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.
export module GameFramework;
import std;
import <cassert>;
export import Transform;

export class Scene;
export class Actor;
export class ActorComponent;

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
    [[nodiscard]] Actor& GetActor() const { return actor_; }
    [[nodiscard]] ActorComponent* GetParent() const { return parent_; }
    [[nodiscard]] const Transform& GetLocalTransform() const { return transform_; }
    void SetLocalTransform(const Transform& transform);
    [[nodiscard]] glm::mat4 GetWorldMatrix() const;

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

private:
    friend class Scene;
    Scene& scene_;
    Transform transform_;
    mutable glm::mat4 world_{ 1.0f };
    mutable bool dirty_ = true;
    bool pending_destroy_ = false;
    std::vector<std::unique_ptr<ActorComponent>> children_;
};

// All scene operations and object access belong to the main thread.
export class Scene
{
public:
    Scene() = default;
    ~Scene() = default;
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) = delete;
    Scene& operator=(Scene&&) = delete;

    template<std::derived_from<Actor> T, class... Args>
    T* SpawnActor(Args&&... args)
    {
        auto actor = std::make_unique<T>(*this, std::forward<Args>(args)...);
        auto* result = actor.get();
        actors_.push_back(std::move(actor));
        return result;
    }
    void Update(double delta_seconds);
    void UpdateWorldTransforms();

private:
    void Collect(ActorComponent& component);
    static void RemoveDestroyed(std::vector<std::unique_ptr<ActorComponent>>& children);
    std::vector<std::unique_ptr<Actor>> actors_;
    std::vector<GameObject*> updates_;
    std::uint64_t frame_ = 0;
    bool updating_ = false;
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