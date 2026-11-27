#include <bandit/bandit.h>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>

#include "equipment/weapontuning.h"
#include "property/propertymanager.h"
#include "settings/settingsstore.h"
#include "voxeleffect/combateffects.h"

using namespace bandit;

go_bandit([]() {
    describe("explicit settings persistence",[]() {
        std::filesystem::path path;
        before_each([&]() {
            path = std::filesystem::temp_directory_path() /
                ("voxellancer-settings-" + std::to_string(std::random_device{}()) + ".ini");
        });
        after_each([&]() { std::filesystem::remove(path); });

        it("writes only submitted groups and replaces an existing snapshot", [&]() {
            {
                SettingsStore store(path);
                AssertThat(std::filesystem::exists(path),IsFalse());
                store.save({{"audio",{{"effectsEnabled","false"}}},
                    {"effects_global",{{"brightness","0.3"}}}});
                store.save({{"weapon_pulselaser",{{"pulselaser_bullet.general.speed","200"}}}});
                store.save({{"audio",{{"effectsEnabled","true"}}}});
                AssertThat(store.status() == SettingsStore::Status::Saved,IsTrue());
            }
            SettingsStore restored(path);
            AssertThat(restored.group("audio").at("effectsEnabled"),Equals(std::string("true")));
            AssertThat(restored.group("effects_global").at("brightness"),Equals(std::string("0.3")));
            AssertThat(restored.group("weapon_pulselaser").at("pulselaser_bullet.general.speed"),
                Equals(std::string("200")));
            AssertThrows(std::invalid_argument,
                restored.save({{"audio",{{"effectsEnabled","true\n[injected]"}}}}));
            AssertThat(SettingsStore(path).group("audio").at("effectsEnabled"),
                Equals(std::string("true")));
        });

        it("roundtrips precise floats and rejects malformed or non-finite settings",[&]() {
            for (float value : {0.02f,0.3f,1.2345678f,5000.0f}) {
                AssertThat(SettingsStore::number(SettingsStore::number(value)),Equals(value));
            }
            for (const std::string value : {"nan","inf","1.0x","","1e999"}) {
                AssertThrows(std::invalid_argument,SettingsStore::number(value));
            }
            AssertThrows(std::invalid_argument,
                SettingsStore::number(std::numeric_limits<float>::infinity()));
            std::ofstream(path) << "[audio]\neffectsEnabled=true\neffectsEnabled=false\n";
            AssertThrows(std::runtime_error,SettingsStore{path});
        });

        it("reports failed writes without changing the saved snapshot",[&]() {
            SettingsStore store(path / "missing" / "settings.ini");
            AssertThrows(std::runtime_error,store.save({{"audio",{{"effectsEnabled","false"}}}}));
            AssertThat(store.status() == SettingsStore::Status::Failed,IsTrue());
            AssertThat(store.group("audio").empty(),IsTrue());
            AssertThat(std::filesystem::exists(path),IsFalse());
        });

        it("restores selected weapon drafts without changing authored properties or saving reset",[&]() {
            PropertyManager::instance()->load("data/equipment/weapons/pulselaser.ini","pulselaser");
            PropertyManager::instance()->load("data/equipment/projectiles/pulselaser_bullet.ini",
                "pulselaser_bullet");
            SettingsStore store(path);
            WeaponTuning gun("pulselaser");
            const float authored = gun.value(0);
            gun.set(0,authored + 1);
            AssertThat(std::filesystem::exists(path),IsFalse());
            gun.persist(store);
            gun.reset();
            WeaponTuning restored("pulselaser");
            AssertThat(restored.value(0),Equals(authored));
            restored.restore(SettingsStore(path));
            AssertThat(restored.value(0),Equals(authored + 1));
            restored.set(0,authored + 2);
            CombatEffectSettings effects;
            effects.persist(store,CombatEffectSettings::Global);
            restored.restore(SettingsStore(path));
            AssertThat(restored.value(0),Equals(authored + 1));
            store.save({{"weapon_pulselaser",{
                {restored.fields()[0].property,"400"},
                {restored.fields()[1].property,"-1"}}}});
            AssertThrows(std::invalid_argument,restored.restore(store));
            AssertThat(restored.value(0),Equals(authored + 1));
        });

        it("saves effect groups independently and validates restoration transactionally",[&]() {
            using S = CombatEffectSettings;
            SettingsStore store(path);
            S effects, defaults;
            effects.set(0,S::MuzzleSize,1.23f);
            effects.set(1,S::MuzzleSize,2.34f);
            effects.set(S::SphereShield,S::HitLight,2);
            effects.set(S::Global,S::Brightness,0.3f);
            AssertThat(std::filesystem::exists(path),IsFalse());
            effects.persist(store,0);
            effects.persist(store,S::SphereShield);
            effects.persist(store,S::Global);
            effects.reset(0);
            S restored;
            restored.restore(SettingsStore(path));
            AssertThat(restored.value(0,S::MuzzleSize),Equals(1.23f));
            AssertThat(restored.value(1,S::MuzzleSize),Equals(defaults.value(1,S::MuzzleSize)));
            AssertThat(restored.value(S::SphereShield,S::HitLight),Equals(2.0f));
            AssertThat(restored.value(S::Global,S::Brightness),Equals(0.3f));
            store.save({{"effects_global",{{"brightness","0.7"},{"boxBudget","1.5"}}}});
            AssertThrows(std::invalid_argument,restored.restore(store));
            AssertThat(restored.value(S::Global,S::Brightness),Equals(0.3f));
        });
    });
});
