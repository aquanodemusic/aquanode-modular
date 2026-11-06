#pragma once

// The sidebar (SidebarComponent).

#include "SidebarHeader.h"

//==============================================================================
// Left sidebar: black, section titles + module names from the factory.
class SidebarComponent : public juce::Component,
                         private juce::Timer
{
public:
    // typeId is a module type, or a patch generator's id (Preset Generation
    // rows); openMenu is true when a generator row's arrow was tapped (rows
    // with a menu only)
    explicit SidebarComponent (std::function<void (const juce::String& typeId, bool openMenu)> onModuleClicked);
    ~SidebarComponent() override;

    void paint (juce::Graphics& g) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;

    // the "With Mutation" toggle beside the Preset Generation title
    void setWithMutation (bool on)   { withMutation = on; repaint(); }
    bool getWithMutation() const     { return withMutation; }
    std::function<void (bool)> onWithMutationChanged;


    // the search at the top: with text, the list is every module (and plugin)
    // that matches it, whatever is folded
    void setSearch (const juce::String& text) { search = text.trim(); hideTooltip(); buildRows(); repaint(); }

    // the colour of the sidebar's plain text (dark on a light sidebar)
    static juce::Colour textColour();

    // the colours changed: the rows (which keep theirs) are built anew
    void refreshColours() { buildRows(); repaint(); }

    // which category headers are open (all folded by default)
    void setExpandedHeaders (const juce::StringArray& open) { expandedHeaders = open; buildRows(); repaint(); }
    std::function<void (const juce::StringArray&)> onExpandedChanged;

    static constexpr int sidebarWidth = 220;
    static constexpr int scrollBarWidth = 12;   // reserved so the Viewport's scrollbar never covers text

    // the Modulation table, at the very end after a line (not in search results)
    void setFooter (juce::Component* c) { footer = c; if (c != nullptr) addChildComponent (c); buildRows(); repaint(); }
    void footerResized() { buildRows(); repaint(); }
    int layoutWidth() const;   // the rows' width: what the sidebar's Viewport shows, less its scrollbar

private:
    struct Row
    {
        juce::Rectangle<int> area;
        juce::String text;
        juce::String typeId;       // empty for section titles
        juce::Colour colour;
        bool isGenerator { false };
        bool hasMenu { false };    // generator rows: an arrow opens a menu
        bool centreFullWidth { false };   // the Random row: name centred on the whole row
        bool isToggle { false };          // the With Mutation toggle
        bool isPlugin { false };          // a Plugins row: name = GUI version, pill = patch version
        bool pluginHasPatch { false };
        juce::String headerKey;           // a category header that folds its list
        bool isSubLabel { false };        // a group's name inside a category ("Envelopes")
        juce::String detail;              // dim text at the right (search results: the group)
    };

    // the two pills at the right end of a Plugins row
    static juce::Rectangle<int> pluginGuiPillArea (const Row& row)
    {
        return row.area.withLeft (row.area.getRight() - 84).withWidth (34).reduced (0, 2);
    }
    static juce::Rectangle<int> pluginPatchPillArea (const Row& row)
    {
        return row.area.withLeft (row.area.getRight() - 46).reduced (0, 2);
    }

    // the arrow at the right end of a generator row, which opens its menu
    static juce::Rectangle<int> generatorMenuArea (const Row& row)
    {
        return row.area.withLeft (row.area.getRight() - 30);
    }

    void buildRows();
    void buildSearchRows (int y);
    juce::Component* footer { nullptr };
    int footerLineY { -1 };
    int footerLineWidth { 0 };

    //=== hover tooltip ======================================================
    static constexpr int hoverDelayMs = 1500;   // a deliberate rest, not a twitch
    static constexpr int hoverSlackPx = 3;      // hand tremor must not restart the wait

    void timerCallback() override;
    void showTooltipFor (const Row& row);
    void hideTooltip();
    const Row* rowAt (juce::Point<int> pos) const;

    std::vector<Row> rows;
    std::function<void (const juce::String&, bool)> moduleClicked;
    bool withMutation { false };
    juce::StringArray expandedHeaders;
    juce::String search;

    // touch: the first tap ("typeId|part") and when it came
    static constexpr int doubleTapMs = 450;
    juce::String pendingTapKey;
    juce::uint32 pendingTapMs { 0 };

    std::unique_ptr<SidebarTooltip> tooltip;
    juce::String hoveredTypeId;
    juce::Point<int> hoverPos;
    juce::uint32 hoverStartMs { 0 };
};
