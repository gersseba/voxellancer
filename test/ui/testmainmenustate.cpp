#include <bandit/bandit.h>

#include "ui/mainmenustate.h"
#include "ui/scenariomenu.h"

using namespace bandit;

go_bandit([]() {
    describe("Main menu connection gating", []() {
        it("blocks starting and resuming until the server is ready", []() {
            MainMenuState menu;
            AssertThat(menu.selectContracts(), Equals(false));
            AssertThat(menu.resume(), Equals(false));
            AssertThat(menu.visible(), Equals(true));
            menu.settings();
            AssertThat(menu.page() == MainMenuState::Page::Settings, Equals(true));
            menu.show();
            menu.setConnectionReady(true);
            AssertThat(menu.resume(), Equals(false));
            AssertThat(menu.selectContracts(), Equals(true));
            AssertThat(menu.visible(), Equals(false));
        });

        it("resumes the existing session without starting it again", []() {
            MainMenuState menu;
            menu.setConnectionReady(true);
            AssertThat(menu.selectContracts(), Equals(true));
            menu.show();
            AssertThat(menu.selectContracts(), Equals(true));
            menu.show();
            menu.settings();
            menu.show();
            AssertThat(menu.resume(), Equals(true));
            AssertThat(menu.visible(), Equals(false));
        });

        it("returns to the menu on connection loss and blocks session actions", []() {
            MainMenuState menu;
            menu.setConnectionReady(true);
            menu.selectContracts();
            menu.setConnectionReady(false);
            AssertThat(menu.visible(), Equals(true));
            AssertThat(menu.selectContracts(), Equals(false));
            AssertThat(menu.resume(), Equals(false));
            menu.setConnectionReady(true);
            AssertThat(menu.contractsEnabled(), Equals(true));
            AssertThat(menu.resume(), Equals(true));
        });

        it("does not interrupt settings when a connection becomes ready or is lost", []() {
            MainMenuState menu;
            menu.settings();
            menu.setConnectionReady(true);
            AssertThat(menu.page() == MainMenuState::Page::Settings, Equals(true));
            menu.setConnectionReady(false);
            AssertThat(menu.page() == MainMenuState::Page::Settings, Equals(true));
            AssertThat(menu.selectContracts(), Equals(false));
        });

        it("allows local scenarios without a server and resumes after connection loss", []() {
            MainMenuState menu;
            menu.scenarios();
            AssertThat(menu.page() == MainMenuState::Page::Scenarios, Equals(true));
            menu.startScenario();
            AssertThat(menu.visible(), Equals(false));
            menu.setConnectionReady(false);
            AssertThat(menu.visible(), Equals(false));
            menu.show();
            AssertThat(menu.resume(), Equals(true));
            AssertThat(menu.selectContracts(), Equals(false));
            menu.setConnectionReady(true);
            AssertThat(menu.selectContracts(), Equals(true));
            menu.setConnectionReady(false);
            AssertThat(menu.visible(), Equals(true));
        });

        it("exposes the standalone test and legacy scenario IDs", []() {
            const std::array<int, 10> ids{{0,1,2,3,4,5,6,8,9,10}};
            for (std::size_t i = 0; i < ids.size(); ++i) {
                AssertThat(ScenarioMenuEntries[i].id, Equals(ids[i]));
                AssertThat(ScenarioMenuEntries[i].name[0] != '\0', Equals(true));
            }
        });
    });
});
