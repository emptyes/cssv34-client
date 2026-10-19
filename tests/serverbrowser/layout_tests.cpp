#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

#define SETUP_PANEL(panel) (void)(panel)

struct Control
{
    bool selected = false;
    bool visible = true;
    int font = 0;
    bool IsSelected() const { return selected; }
    void SetVisible(bool state) { visible = state; }
    void SetFont(int value) { font = value; }
};

struct Scheme
{
    int scale = 1;
    int GetProportionalScaledValue(int value) const { return scale * value; }
};

namespace vgui
{
    static Scheme currentScheme;
    static Scheme *scheme() { return &currentScheme; }
}

struct CBaseGamesPage
{
    explicit CBaseGamesPage(const std::string &name) : pageName(name) {}
    Control quickCheck, filter, quickList, gameList, filterString;
    Control *m_pQuickListCheckButton = &quickCheck;
    Control *m_pFilter = &filter;
    Control *m_pQuickList = &quickList;
    Control *m_pGameList = &gameList;
    Control *m_pFilterString = &filterString;
    const char *m_pCustomResFilename = nullptr;
    bool m_bFiltersVisible = false;
    int m_hFont = 0;
    std::string pageName;
    bool visible = true;
    bool proportional = false;
    int x = 0, y = 28, wide = 624, tall = 278;
    int layouts = 0, gameFilters = 0, invalidations = 0, settings = 0, applications = 0;
    bool IsVisible() const { return visible; }
    void SetVisible(bool state) { visible = state; }
    bool IsProportional() const { return proportional; }
    void GetSize(int &w, int &h) const { w = wide; h = tall; }
    void GetBounds(int &px, int &py, int &w, int &h) const
    {
        px = x; py = y; GetSize(w, h);
    }
    void SetSize(int w, int h) { wide = w; tall = h; }
    void SetBounds(int px, int py, int w, int h)
    {
        x = px; y = py; SetSize(w, h);
    }
    void UpdateDerivedLayouts()
    {
        ++layouts;
        if (wide != vgui::scheme()->GetProportionalScaledValue(640) ||
            tall != vgui::scheme()->GetProportionalScaledValue(384))
            throw std::runtime_error("temporary layout size changed");
        if (pageName == "InternetGames")
        {
            SetBounds(0, 28, 624, 278);
            SetVisible(true);
        }
    }
    void UpdateGameFilter() { ++gameFilters; }
    void InvalidateLayout() { ++invalidations; }
    void UpdateFilterSettings() { ++settings; }
    void ApplyGameFilters() { ++applications; }
    void UpdateFilterAndQuickListVisibility();
};

#include "layout_under_test.inc"

static void Check(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

static void Verify(CBaseGamesPage &page)
{
    const bool visible = page.IsVisible();
    const std::array<int, 4> bounds = {{page.x, page.y, page.wide, page.tall}};
    const int calls = page.layouts;
    page.UpdateFilterAndQuickListVisibility();
    Check(page.IsVisible() == visible, "resource changed page visibility");
    Check(bounds == std::array<int, 4>{{page.x, page.y, page.wide, page.tall}},
          "resource changed page bounds");
    const bool quick = page.quickCheck.selected;
    Check(page.quickList.visible == quick, "quick list visibility");
    Check(page.gameList.visible == !quick, "server list visibility");
    Check(page.filter.visible == !quick, "filter button visibility");
    Check(page.filterString.visible == !quick, "filter summary visibility");
    Check(page.m_bFiltersVisible == (!quick && !page.m_pCustomResFilename && page.filter.selected),
          "filter area state");
    Check(!page.m_hFont || page.gameList.font == page.m_hFont, "font restoration");
    Check(page.layouts == calls + 1 && page.gameFilters == calls + 1 &&
          page.invalidations == calls + 1 && page.settings == calls + 1 &&
          page.applications == calls + 1, "update call count");
}

int main()
{
    const char *names[] = {"InternetGames", "FavoriteGames", "HistoryGames", "LanGames"};
    int passed = 0, failed = 0;
    for (const char *name : names)
        for (int flags = 0; flags < 64; ++flags)
            for (int size = 0; size < 3; ++size)
            {
                CBaseGamesPage page(name);
                page.visible = (flags & 1) != 0;
                page.quickCheck.selected = (flags & 2) != 0;
                page.filter.selected = (flags & 4) != 0;
                page.proportional = (flags & 8) != 0;
                vgui::currentScheme.scale = page.proportional ? 2 : 1;
                page.m_pCustomResFilename = (flags & 16) ? "custom.res" : nullptr;
                page.m_hFont = (flags & 32) ? 7 : 0;
                page.SetBounds(3 + size, 24 + size, 624 + size * 170, 278 + size * 300);
                try { Verify(page); ++passed; }
                catch (const std::exception &e)
                {
                    ++failed;
                    if (failed <= 4)
                        std::cerr << "FAIL " << name << " flags=" << flags << " size=" << size
                                  << ": " << e.what() << '\n';
                }
            }
    try
    {
        std::array<CBaseGamesPage, 4> pages = {{CBaseGamesPage(names[0]), CBaseGamesPage(names[1]),
                                             CBaseGamesPage(names[2]), CBaseGamesPage(names[3])}};
        vgui::currentScheme.scale = 1;
        for (int iteration = 0; iteration < 100; ++iteration)
        {
            const int active = iteration % 4;
            for (int index = 0; index < 4; ++index)
            {
                pages[index].SetVisible(index == active);
                pages[index].filter.selected = (iteration & 1) != 0;
                pages[index].quickCheck.selected = (iteration & 2) != 0;
                Verify(pages[index]);
            }
            int visible = 0;
            for (const auto &page : pages)
                visible += page.IsVisible() ? 1 : 0;
            Check(visible == 1, "multiple pages visible after filter reload");
            ++passed;
        }
    }
    catch (const std::exception &e)
    {
        ++failed;
        std::cerr << "FAIL tab cycle: " << e.what() << '\n';
    }
    std::cout << "Passed: " << passed << ", failed: " << failed << '\n';
    return failed ? 1 : 0;
}
