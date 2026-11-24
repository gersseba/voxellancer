#pragma once

class MainMenuState {
public:
    enum class Page { Main, Scenarios, Settings, Playing };
    enum class Session { None, Contracts, Scenario };

    void setConnectionReady(bool ready) {
        m_ready = ready;
        if (!ready && m_page == Page::Playing && m_session == Session::Contracts) {
            m_page = Page::Main;
        }
    }
    bool contractsEnabled() const { return m_ready; }
    bool resumeEnabled() const {
        return m_session == Session::Scenario ||
            (m_session == Session::Contracts && m_ready);
    }
    bool scenarioActive() const { return m_session == Session::Scenario; }
    Page page() const { return m_page; }
    bool visible() const { return m_page != Page::Playing; }

    bool selectContracts() {
        if (!m_ready) return false;
        m_session = Session::Contracts;
        m_page = Page::Playing;
        return true;
    }
    void startScenario() {
        m_session = Session::Scenario;
        m_page = Page::Playing;
    }
    bool resume() {
        if (!resumeEnabled()) return false;
        m_page = Page::Playing;
        return true;
    }
    void show() { m_page = Page::Main; }
    void scenarios() { m_page = Page::Scenarios; }
    void settings() { m_page = Page::Settings; }

private:
    bool m_ready = false;
    Session m_session = Session::None;
    Page m_page = Page::Main;
};
