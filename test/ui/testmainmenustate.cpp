#include <bandit/bandit.h>

#include "ui/mainmenustate.h"

using namespace bandit;

go_bandit([]() {
    describe("Main menu connection gating", []() {
        it("blocks starting and resuming until the server is ready", []() {
            MainMenuState menu;
            AssertThat(menu.start(), Equals(false));
            AssertThat(menu.resume(), Equals(false));
            AssertThat(menu.visible(), Equals(true));
            menu.settings();
            AssertThat(menu.page() == MainMenuState::Page::Settings, Equals(true));
            menu.show();
            menu.setConnectionReady(true);
            AssertThat(menu.resume(), Equals(false));
            AssertThat(menu.start(), Equals(true));
            AssertThat(menu.visible(), Equals(false));
        });

        it("resumes the existing session without starting it again", []() {
            MainMenuState menu;
            menu.setConnectionReady(true);
            AssertThat(menu.start(), Equals(true));
            menu.show();
            AssertThat(menu.start(), Equals(false));
            menu.settings();
            menu.show();
            AssertThat(menu.resume(), Equals(true));
            AssertThat(menu.visible(), Equals(false));
        });

        it("returns to the menu on connection loss and blocks session actions", []() {
            MainMenuState menu;
            menu.setConnectionReady(true);
            menu.start();
            menu.setConnectionReady(false);
            AssertThat(menu.visible(), Equals(true));
            AssertThat(menu.start(), Equals(false));
            AssertThat(menu.resume(), Equals(false));
            menu.setConnectionReady(true);
            AssertThat(menu.startEnabled(), Equals(false));
            AssertThat(menu.resume(), Equals(true));
        });

        it("does not interrupt settings when a connection becomes ready or is lost", []() {
            MainMenuState menu;
            menu.settings();
            menu.setConnectionReady(true);
            AssertThat(menu.page() == MainMenuState::Page::Settings, Equals(true));
            menu.setConnectionReady(false);
            AssertThat(menu.page() == MainMenuState::Page::Settings, Equals(true));
            AssertThat(menu.start(), Equals(false));
        });
    });
});
