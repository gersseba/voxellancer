#include <bandit/bandit.h>
#include "ui/configurationmenu.h"

using namespace bandit;

go_bandit([]() {
    describe("generic configuration controls",[]() {
        it("applies sliders, fine adjustment and reset immediately but saves only on request",[]() {
            std::vector<ConfigurationField> fields{
                {"SPEED",1,2000,100,false,"speed"}, {"PELLETS",1,8,1,true,"pellets"}};
            std::vector<float> values{100,1}, applied = values;
            int saves = 0;
            ConfigurationMenu menu{fields,
                [&](std::size_t index) { return values.at(index); },
                [&](std::size_t index,float value) {
                    fields.at(index).validate(value);
                    values.at(index) = value; applied = values;
                },
                [&]() { values = {100,1}; applied = values; },[&]() { ++saves; }};
            menu.slide(0,0.5f);
            AssertThat(applied[0],Equals(1000.5f));
            menu.slide(1,0.5f);
            AssertThat(applied[1],Equals(5.0f));
            menu.nudge(1,-1);
            AssertThat(applied[1],Equals(4.0f));
            menu.slide(0,2);
            menu.nudge(0,1);
            AssertThat(applied[0],Equals(2000.0f));
            AssertThat(saves,Equals(0));
            menu.reset();
            AssertThat(applied[0],Equals(100.0f));
            AssertThat(saves,Equals(0));
            menu.save();
            AssertThat(saves,Equals(1));
        });
        it("hit-tests only real slider rows for any menu layout",[]() {
            const std::vector<ConfigurationField> fields{{"ONE",0,1,0},{"TWO",0,1,0}};
            ConfigurationMenu menu{fields,{}, {}, {}, {}};
            const ConfigurationLayout layout{0.3f,0.04f,0.4f,0.2f,0.7f,0.3f,0.1f,0.014f};
            AssertThat(menu.rowAt(layout,{0.5f,0.31f}),Equals(0));
            AssertThat(menu.rowAt(layout,{0.5f,0.35f}),Equals(1));
            AssertThat(menu.rowAt(layout,{0.5f,0.299f}),Equals(-1));
            AssertThat(menu.rowAt(layout,{0.5f,0.381f}),Equals(-1));
            AssertThat(menu.rowAt(layout,{0.399f,0.31f}),Equals(-1));
        });
    });
});
