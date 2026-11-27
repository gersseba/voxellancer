#include <bandit/bandit.h>
#include <cmath>
#include <limits>
#include "voxeleffect/combateffects.h"

using namespace bandit;

go_bandit([]() {
    describe("combat effect presentation",[]() {
        using S = CombatEffectSettings;
        it("applies per-gun sliders live to tracked projectiles and resets only that group",[]() {
            CombatEffects effects;
            effects.beginProjectiles();
            effects.projectile({1,1},0,{0,0,0},{0,0,-1});
            effects.endProjectiles();
            const auto initial = effects.build({0,0,10}).front().extent.x;
            effects.settings.set(0,S::ProjectileSize,initial*2);
            AssertThat(effects.build({0,0,10}).front().extent.x,Equals(initial*2));
            const auto other = effects.settings.value(1,S::ProjectileSize);
            effects.settings.reset(0);
            AssertThat(effects.build({0,0,10}).front().extent.x,Equals(initial));
            AssertThat(effects.settings.value(1,S::ProjectileSize),Equals(other));
            AssertThrows(std::invalid_argument,effects.settings.set(0,S::TrailSegments,1.5f));
            AssertThrows(std::invalid_argument,effects.settings.set(0,S::MuzzleTime,
                std::numeric_limits<float>::quiet_NaN()));
        });
        it("keeps trails behind observed travel and expires them without creating a hit",[]() {
            CombatEffects effects;
            effects.beginProjectiles();
            effects.projectile({1,1},3,{0,0,0},{0,0,-1});
            effects.endProjectiles();
            effects.advance(0.02f);
            effects.beginProjectiles();
            effects.projectile({1,1},3,{0,0,-10},{0,0,-1});
            effects.endProjectiles();
            effects.beginProjectiles();
            effects.endProjectiles();
            const auto& trail = effects.build({0,0,10});
            AssertThat(trail.empty(),IsFalse());
            for (const auto& box : trail) {
                AssertThat(box.position.z <= 0 && box.position.z >= -10,IsTrue());
            }
            effects.advance(0.6f);
            effects.beginProjectiles();
            effects.endProjectiles();
            AssertThat(effects.build({0,0,10}).empty(),IsTrue());
        });
        it("attaches hit patches to translating and rotating shield surfaces",[]() {
            CombatEffects effects;
            effects.settings.set(S::SphereShield,S::IdleLight,0);
            effects.settings.set(S::SphereShield,S::SweepLight,0);
            ShieldEffectBody body;
            body.id = {1,1}; body.type = ShieldType::Sphere; body.fraction = 1;
            body.radius = 3;
            body.surface = CombatEffects::sphereSurface({},3);
            effects.shields({body});
            effects.shieldHit(body.id,{0,0,3},20);
            effects.advance(0.08f);
            const auto original = effects.build({0,0,10});
            AssertThat(original.empty(),IsFalse());
            body.transform.setPosition({10,2,0});
            body.transform.setOrientation(glm::angleAxis(1.0f,glm::vec3(0,1,0)));
            effects.shields({body});
            const auto& moved = effects.build({10,2,10});
            AssertThat(moved.size(),Equals(original.size()));
            for (std::size_t i = 0; i < original.size(); ++i) {
                const auto expected = body.transform.applyTo(original[i].position);
                AssertThat(glm::distance(moved[i].position,expected) < 0.0001f,IsTrue());
            }
        });
        it("builds a bounded grid-aligned voxel shell rather than smooth sphere samples",[]() {
            const auto surface = CombatEffects::sphereSurface({0,0,0},3.5f);
            AssertThat(surface->empty(),IsFalse());
            AssertThat(surface->size() < std::size_t(1500),IsTrue());
            for (const auto& point : *surface) {
                AssertThat(glm::abs(point.normal.x)+glm::abs(point.normal.y)+
                    glm::abs(point.normal.z),Equals(1.0f));
                AssertThat(glm::distance(point.position,glm::vec3(0)) <= 4.5f,IsTrue());
            }
            AssertThrows(std::invalid_argument,CombatEffects::sphereSurface({},0));
        });
        it("maps a geometric shield hit onto the nearest large shell cell",[]() {
            CombatEffects effects;
            effects.settings.set(S::SphereShield,S::IdleLight,0);
            effects.settings.set(S::SphereShield,S::SweepLight,0);
            ShieldEffectBody body;
            body.id = {1,1}; body.type = ShieldType::Sphere; body.fraction = 1;
            body.radius = 50;
            body.surface = CombatEffects::sphereSurface({},50);
            effects.shields({body});
            effects.shieldHit(body.id,{0,0,50},20);
            AssertThat(effects.build({0,0,70}).empty(),IsFalse());
        });
        it("shows depletion and recovery but never invents an initial shield break",[]() {
            CombatEffects effects;
            effects.settings.set(S::SphereShield,S::IdleLight,0);
            effects.settings.set(S::SphereShield,S::SweepLight,0);
            ShieldEffectBody body;
            body.id = {1,1}; body.type = ShieldType::Sphere; body.radius = 3;
            body.surface = CombatEffects::sphereSurface({},3);
            effects.shields({body});
            AssertThat(effects.build({0,0,10}).empty(),IsTrue());
            body.fraction = 1;
            effects.shields({body});
            effects.advance(3.1f);
            body.fraction = 0;
            effects.shields({body});
            AssertThat(effects.build({0,0,10}).empty(),IsFalse());
            effects.advance(2.1f);
            AssertThat(effects.build({0,0,10}).empty(),IsTrue());
            body.fraction = 0.1f;
            effects.shields({body});
            effects.advance(0.5f);
            AssertThat(effects.build({0,0,10}).empty(),IsFalse());
        });
        it("merges nearby simultaneous shield hits into one bounded patch",[]() {
            CombatEffects single, volley;
            ShieldEffectBody body;
            body.id = {1,1}; body.type = ShieldType::Sphere; body.fraction = 1; body.radius = 3;
            body.surface = CombatEffects::sphereSurface({},3);
            for (auto* effects : {&single,&volley}) {
                effects->settings.set(S::SphereShield,S::IdleLight,0);
                effects->settings.set(S::SphereShield,S::SweepLight,0);
                effects->shields({body});
            }
            single.shieldHit(body.id,{0,0,3},40);
            volley.shieldHit(body.id,{0,0,3},20);
            volley.shieldHit(body.id,{0,0,3},20);
            single.advance(0.08f); volley.advance(0.08f);
            const auto& a = single.build({0,0,10});
            const auto& b = volley.build({0,0,10});
            AssertThat(a.size(),Equals(b.size()));
            AssertThat(a.empty(),IsFalse());
            for (std::size_t i = 0; i < a.size(); ++i) {
                AssertThat(a[i].position == b[i].position,IsTrue());
                AssertThat(a[i].color == b[i].color,IsTrue());
            }
        });
        it("bounds dense volleys and honors brightness and distance controls",[]() {
            CombatEffects effects;
            effects.settings.set(S::Global,S::BoxBudget,256);
            for (int i = 0; i < 1000; ++i) effects.impact(4,{0,0,0},{0,1,0});
            AssertThat(effects.build({0,0,10}).size(),Equals(std::size_t(256)));
            effects.settings.set(S::Global,S::Brightness,0);
            AssertThat(effects.build({0,0,10}).empty(),IsTrue());
            effects.settings.reset(S::Global);
            AssertThat(effects.build({0,0,4000}).empty(),IsTrue());
        });
        it("keeps recoil and camera kick presentation-only, bounded and adjustable",[]() {
            CombatEffects effects;
            effects.fire(2,{0,0,0},{0,0,-1},1,voxellancer::protocol::EntityId{1,1});
            AssertThat(effects.recoil({1,1}).z > 0,IsTrue());
            AssertThat(effects.recoil({2,1}) == glm::vec3(0),IsTrue());
            effects.kick(10);
            AssertThat(effects.cameraKick(),Equals(effects.settings.value(S::Global,S::CameraKick)));
            effects.settings.set(S::Global,S::CameraKick,0);
            AssertThat(effects.cameraKick(),Equals(0.0f));
            effects.advance(0.2f);
            AssertThat(effects.recoil({1,1}) == glm::vec3(0),IsTrue());
        });
        it("creates a distinct outward shield fracture and honors break brightness",[]() {
            CombatEffects effects;
            auto body = CombatEffects::previewShield(S::SphereShield);
            effects.shields({body});
            for (unsigned i = 0; i < 64; ++i)
                effects.shieldHit({i+10,1},{0,0,0},20);
            body.fraction = 0;
            effects.shields({body});
            effects.advance(0.2f);
            const auto fragments = effects.build({0,0,10},false);
            AssertThat(fragments.empty(),IsFalse());
            bool outward = false;
            for (const auto& fragment : fragments)
                outward |= glm::length(fragment.position) > body.radius*1.01f;
            AssertThat(outward,IsTrue());
            effects.settings.set(S::SphereShield,S::BreakLight,0);
            AssertThat(effects.build({0,0,10},false).empty(),IsTrue());
        });
        it("previews every weapon and shield without touching live effects",[]() {
            CombatEffects effects;
            effects.fire(0,{0,0,0},{0,0,-1});
            const auto count = effects.build({0,0,10}).size();
            for (std::size_t group = 0; group < S::GroupCount; ++group)
                for (float time : {0.01f,0.5f,1.0f,2.1f,4.2f,6.0f}) {
                    for (const auto& box : effects.preview(group,time)) {
                        AssertThat(std::isfinite(glm::length(box.position)),IsTrue());
                        AssertThat(glm::all(glm::greaterThan(box.extent,glm::vec3(0))),IsTrue());
                        AssertThat(box.color.a > 0 && box.color.a <= 1,IsTrue());
                    }
                }
            AssertThat(effects.build({0,0,10}).size(),Equals(count));
            effects.settings.set(0,S::MuzzleSize,2);
            effects.clear();
            AssertThat(effects.build({0,0,10}).empty(),IsTrue());
            effects.fire(0,{0,0,0},{0,0,-1});
            AssertThat(effects.build({0,0,10}).front().extent.z,EqualsWithDelta(1.2f,0.001f));
        });
    });
});
