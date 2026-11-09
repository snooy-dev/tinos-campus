// Copyright (c) 2026 Yoon-Seong Kim, Seong-Hyeon Cho. All rights reserved.
module GameFramework;
import <cassert>;

ActorComponent::ActorComponent(Actor& actor, ActorComponent* parent)
    : actor_(actor), parent_(parent)
{
    assert(!parent || &parent->GetActor() == &actor);
}

ActorComponent::~ActorComponent() = default;
Actor::~Actor() = default;

void ActorComponent::InvalidateTransform()
{
    if (dirty_)
        return;
    dirty_ = true;
    for (auto& child : children_)
        child->InvalidateTransform();
}

void ActorComponent::SetLocalTransform(const Transform& transform)
{
    transform_ = transform;
    InvalidateTransform();
}

void Actor::SetLocalTransform(const Transform& transform)
{
    transform_ = transform;
    dirty_ = true;
    for (auto& child : children_)
        child->InvalidateTransform();
}

glm::mat4 Actor::GetWorldMatrix() const
{
    if (dirty_)
    {
        world_ = transform_.ToMatrix();
        dirty_ = false;
    }
    return world_;
}

glm::mat4 ActorComponent::GetWorldMatrix() const
{
    if (dirty_)
    {
        const auto parent_world = parent_ ? parent_->GetWorldMatrix() : actor_.GetWorldMatrix();
        world_ = parent_world * transform_.ToMatrix();
        dirty_ = false;
    }
    return world_;
}

void ActorComponent::UpdateWorldTransforms(const glm::mat4& parent_world)
{
    if (dirty_)
    {
        world_ = parent_world * transform_.ToMatrix();
        dirty_ = false;
    }
    for (auto& child : children_)
        child->UpdateWorldTransforms(world_);
}

void ActorComponent::Destroy()
{
    if (pending_destroy_)
        return;
    pending_destroy_ = true;
    for (auto& child : children_)
        child->Destroy();
}

void Actor::Destroy()
{
    if (pending_destroy_)
        return;
    pending_destroy_ = true;
    for (auto& child : children_)
        child->Destroy();
}

void Scene::Collect(ActorComponent& component)
{
    if (component.pending_destroy_)
        return;
    if (component.updated_frame_ != frame_)
        updates_.push_back(&component);
    for (auto& child : component.children_)
        Collect(*child);
}

void Scene::RemoveDestroyed(std::vector<std::unique_ptr<ActorComponent>>& children)
{
    std::erase_if(children, [](const auto& child) { return child->pending_destroy_; });
    for (auto& child : children)
        RemoveDestroyed(child->children_);
}

void Scene::Update(double delta_seconds)
{
    assert(!updating_);
    assert(std::isfinite(delta_seconds) && delta_seconds >= 0.0);
    updating_ = true;
    ++frame_;
    // Unbounded spawn chains intentionally keep this frame running.
    while (true)
    {
        updates_.clear();
        for (auto& actor : actors_)
        {
            if (actor->pending_destroy_)
                continue;
            if (actor->updated_frame_ != frame_)
                updates_.push_back(actor.get());
            for (auto& child : actor->children_)
                Collect(*child);
        }
        if (updates_.empty())
            break;
        for (auto* object : updates_)
        {
            // The snapshot stays alive until all update waves finish.
            const auto* actor = dynamic_cast<Actor*>(object);
            const auto* component = dynamic_cast<ActorComponent*>(object);
            if ((actor && actor->pending_destroy_) ||
                (component && component->pending_destroy_))
                continue;
            object->updated_frame_ = frame_;
            object->Update(delta_seconds);
        }
    }
    updates_.clear();
    std::erase_if(actors_, [](const auto& actor) { return actor->pending_destroy_; });
    for (auto& actor : actors_)
        RemoveDestroyed(actor->children_);
    updating_ = false;
}

void Scene::UpdateWorldTransforms()
{
    assert(!updating_);
    for (auto& actor : actors_)
    {
        if (actor->pending_destroy_)
            continue;
        const auto world = actor->GetWorldMatrix();
        for (auto& child : actor->children_)
            if (!child->pending_destroy_)
                child->UpdateWorldTransforms(world);
    }
}