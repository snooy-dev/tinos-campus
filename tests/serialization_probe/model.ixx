module;
#include <rttr/type>
#include <rttr/rttr_enable.h>
export module SerializationProbe;
import std;
export import Transform;

export namespace probe
{
struct Scene;
struct Component;

struct Object
{
    static inline int live = 0;
    Object() { ++live; }
    virtual ~Object() { --live; }
    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete;
    Object(Object&&) = delete;
    Object& operator=(Object&&) = delete;
    virtual void Update(double) {}
    std::string name;
    RTTR_ENABLE()
};

struct Stats
{
    int health = 100;
    std::vector<int> scores;
};

struct Actor : Object
{
    explicit Actor(Scene& owner) : scene(owner) {}
    ~Actor() override;
    Scene& scene;
    std::vector<std::unique_ptr<Component>> components;
    Transform GetTransform() const { return transform_; }
    void SetTransform(Transform value) { transform_ = value; ++invalidations; }
    int invalidations = 0;
    RTTR_ENABLE(Object)
private:
    Transform transform_;
};

struct Pawn : Actor
{
    explicit Pawn(Scene& scene) : Actor(scene) {}
    Stats stats;
    std::vector<Stats> history;
    Object* target = nullptr;
    bool enabled = true;
    double speed = 2.5;
    int runtime_only = 17;
    RTTR_ENABLE(Actor)
};

struct Component : Object
{
    Component(Actor& owner, Component* parent) : actor(owner), parent(parent) {}
    Actor& actor;
    Component* const parent;
    std::vector<std::unique_ptr<Component>> children;
    std::string asset;
    RTTR_ENABLE(Object)
};

struct Scene
{
    std::vector<std::unique_ptr<Actor>> actors;
};

inline Actor::~Actor() = default;
}
