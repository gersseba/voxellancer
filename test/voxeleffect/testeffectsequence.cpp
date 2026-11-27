#include <bandit/bandit.h>
#include "voxeleffect/effectsequence.h"

using namespace bandit;

go_bandit([]() {
    describe("isolated effect sequences",[]() {
        it("captures frame zero and the complete transient without looping or unrelated decoration",[]() {
            for (const std::string kind : {"muzzle","impact"}) {
                EffectSequence sequence("pulselaser",kind,60);
                AssertThat(sequence.frame(0).empty(),IsFalse());
                AssertThat(sequence.frame(120).empty(),IsTrue());
            }
            EffectSequence projectile("pulselaser","projectile",60);
            AssertThat(projectile.frame(0).empty(),IsFalse());
            AssertThat(projectile.frame(30).empty(),IsFalse());
            AssertThat(projectile.frame(120).empty(),IsTrue());
            for (const std::string group : {"hullshield","sphereshield"}) {
                EffectSequence idle(group,"shield-idle",60);
                AssertThat(idle.frame(0).empty(),IsFalse());
                AssertThat(idle.frame(240).empty(),IsFalse());
                for (const std::string kind : {"shield-hit","shield-break","shield-recharge"}) {
                    EffectSequence sequence(group,kind,60);
                    AssertThat(sequence.frame(12).empty(),IsFalse());
                    AssertThat(sequence.frame(240).empty(),IsTrue());
                }
            }
        });
        it("replays identical fixed-step geometry after seeking backward or repeating a frame",[]() {
            EffectSequence sequence("kineticrailgun","projectile",60);
            const auto first = sequence.frame(25);
            const auto again = sequence.frame(25);
            sequence.frame(0);
            const auto replay = sequence.frame(25);
            AssertThat(first.size(),Equals(replay.size()));
            AssertThat(first.size(),Equals(again.size()));
            for (std::size_t i = 0; i < first.size(); ++i) {
                AssertThat(first[i].position == replay[i].position,IsTrue());
                AssertThat(first[i].color == replay[i].color,IsTrue());
                AssertThat(first[i].extent == replay[i].extent,IsTrue());
            }
        });
        it("uses changed effect parameters on the next animation without rebuilding the sequence",[]() {
            EffectSequence sequence("pulselaser","muzzle",60);
            const auto before = sequence.frame(0).front().extent;
            sequence.settings().set(0,CombatEffectSettings::MuzzleSize,2);
            const auto after = sequence.frame(0).front().extent;
            AssertThat(after.z > before.z,IsTrue());
            sequence.settings().set(CombatEffectSettings::Global,CombatEffectSettings::Brightness,0);
            AssertThat(sequence.frame(1).empty(),IsTrue());
            sequence.settings().reset(CombatEffectSettings::Global);
            AssertThat(sequence.frame(0).empty(),IsFalse());
        });
        it("rejects incompatible effects, unknown groups and invalid frame rates or indices",[]() {
            AssertThrows(std::invalid_argument,EffectSequence("missing","muzzle",60));
            AssertThrows(std::invalid_argument,EffectSequence("pulselaser","shield-hit",60));
            AssertThrows(std::invalid_argument,EffectSequence("sphereshield","impact",60));
            AssertThrows(std::invalid_argument,EffectSequence("pulselaser","muzzle",0));
            EffectSequence sequence("pulselaser","muzzle",60);
            AssertThrows(std::invalid_argument,sequence.frame(-1));
            AssertThrows(std::invalid_argument,sequence.frame(14401));
        });
    });
});
