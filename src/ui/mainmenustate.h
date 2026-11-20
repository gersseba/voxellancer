#pragma once

class MainMenuState {
public:
    enum class Page { Main, Settings, Playing };

    void setConnectionReady(bool ready) {
        m_ready = ready;
        if (!ready && m_page == Page::Playing) {
            m_page = Page::Main;
        }
    }
    bool startEnabled() const { return m_ready && !m_started; }
    bool resumeEnabled() const { return m_ready && m_started; }
    bool sessionStarted() const { return m_started; }
    Page page() const { return m_page; }
    bool visible() const { return m_page != Page::Playing; }

    bool start() {
        if (!startEnabled()) return false;
        m_started = true;
        m_page = Page::Playing;
        return true;
    }
    bool resume() {
        if (!resumeEnabled()) return false;
        m_page = Page::Playing;
        return true;
    }
    void show() { m_page = Page::Main; }
    void settings() { m_page = Page::Settings; }

private:
    bool m_ready = false;
    bool m_started = false;
    Page m_page = Page::Main;
};
