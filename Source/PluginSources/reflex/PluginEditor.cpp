#include "PluginEditor.h"
using namespace rfx;

juce::Font uiFont(float height, bool bold) {
#if JUCE_MAJOR_VERSION >= 8
  return juce::Font(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
#else
  return juce::Font(height, bold ? juce::Font::bold : juce::Font::plain);
#endif
}

// ============================================================ look & feel
ReflexLook::ReflexLook() {
#if JUCE_WINDOWS
  setDefaultSansSerifTypefaceName("Segoe UI");
#endif
  setTheme(Aero);
}

void ReflexLook::setTheme(int t) {
  theme = t == Metal ? Metal : Aero;
  if (metal()) {   // brushed green metal: bright but calmer than before
    bg = juce::Colour(0xff236f3e);
    plateTop = juce::Colour(0xff62c27d); plateBottom = juce::Colour(0xff2b8048);
    text = juce::Colour(0xfff3fff2); caption = juce::Colour(0xffe6fbe6);
    accent = juce::Colour(0xffbdf07e); accentDim = juce::Colour(0xff35603a);
    lcd = juce::Colour(0xff0a2213); lcdText = juce::Colour(0xffc6f5a4);
    gelTop = juce::Colour(0xffdcf5a8); gelBottom = juce::Colour(0xff6f9f42);
    edge = juce::Colour(0xff10301c);
  } else {         // glossy aero
    bg = juce::Colour(0xff2e9e4a);
    plateTop = juce::Colour(0xfff2fff6); plateBottom = juce::Colour(0xffc6efd4);
    text = juce::Colour(0xff0f4d2b); caption = juce::Colour(0xff1d5e3a);
    accent = juce::Colour(0xff3fd45f); accentDim = juce::Colour(0xffa9d8b8);
    lcd = juce::Colour(0xfff7fffa); lcdText = juce::Colour(0xff0f4d2b);
    gelTop = juce::Colour(0xff9df27a); gelBottom = juce::Colour(0xff17923f);
    edge = juce::Colour(0xff6fbf8a);
  }
  const auto popupBg = metal() ? lcd : juce::Colour(0xfff6fff9);
  setColour(juce::ComboBox::backgroundColourId, lcd);
  setColour(juce::ComboBox::textColourId, metal() ? lcdText : text);
  setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
  setColour(juce::ComboBox::arrowColourId, metal() ? accent : juce::Colours::white);
  setColour(juce::PopupMenu::backgroundColourId, popupBg);
  setColour(juce::PopupMenu::textColourId, text);
  setColour(juce::PopupMenu::headerTextColourId, metal() ? accent : juce::Colour(0xff1c8a45));
  setColour(juce::PopupMenu::highlightedBackgroundColourId, metal() ? juce::Colour(0xff3a9a58) : accent);
  setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
  setColour(juce::TextButton::buttonColourId, plateBottom);
  setColour(juce::TextButton::textColourOffId, text);
  setColour(juce::TextButton::textColourOnId, text);
  setColour(juce::Label::textColourId, text);
  setColour(juce::ToggleButton::textColourId, text);
  setColour(juce::TooltipWindow::backgroundColourId, popupBg);
  setColour(juce::TooltipWindow::textColourId, metal() ? lcdText : text);
  setColour(juce::TooltipWindow::outlineColourId, metal() ? plateTop : edge);
}

ReflexLook& ReflexLook::of(juce::Component& c) {
  if (auto* l = dynamic_cast<ReflexLook*>(&c.getLookAndFeel())) return *l;
  // (Aquanode: let go when JUCE shuts down, not when the DLL unloads)
  struct Holder : public juce::DeletedAtShutdown {
    ReflexLook object;
    ~Holder() override { slot() = nullptr; }
    static Holder*& slot() { static Holder* h = nullptr; return h; }
  };
  auto*& h = Holder::slot();
  if (h == nullptr) h = new Holder();
  return h->object;
}

void ReflexLook::drawCaption(juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, juce::Justification j) const {
  if (metal()) { g.setColour(juce::Colours::black.withAlpha(0.5f)); g.drawText(t, r.translated(0, 1.f), j); }
  else         { g.setColour(juce::Colours::white.withAlpha(0.75f)); g.drawText(t, r.translated(0, 1.f), j); }
  g.setColour(caption); g.drawText(t, r, j);
}

// ---------------------------------------------------------------- shared painting helpers
// glossy highlight over the upper part of a rounded shape
static void glossCap(juce::Graphics& g, juce::Rectangle<float> r, float radius, float strength = 0.75f, float portion = 0.5f) {
  auto top = r.reduced(1.5f).withHeight(r.getHeight() * portion);
  g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(strength), 0, top.getY(),
                                         juce::Colours::white.withAlpha(strength * 0.12f), 0, top.getBottom(), false));
  g.fillRoundedRectangle(top, juce::jmax(0.f, radius - 1.5f));
}

// round gel bubble (aero)
static void gelPill(juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::Colour top, juce::Colour bottom) {
  g.setGradientFill(juce::ColourGradient(top, 0, r.getY(), bottom, 0, r.getBottom(), false));
  g.fillRoundedRectangle(r, radius);
  juce::ColourGradient glow(top.brighter(0.4f).withAlpha(0.55f), r.getCentreX(), r.getBottom(),
                            top.withAlpha(0.f), r.getCentreX(), r.getBottom() - r.getHeight() * 0.7f, true);
  g.setGradientFill(glow); g.fillRoundedRectangle(r, radius);
  glossCap(g, r, radius, 0.8f, 0.48f);
  g.setColour(bottom.darker(0.35f).withAlpha(0.8f)); g.drawRoundedRectangle(r, radius, 1.f);
}

// brushed metal plate (metal)
static juce::Image metalPlate(int w, int h, float radius, juce::Colour top, juce::Colour bottom, uint32_t seed) {
  juce::Image img(juce::Image::ARGB, juce::jmax(1, w), juce::jmax(1, h), true);
  juce::Graphics g(img);
  auto r = juce::Rectangle<float>(0, 0, (float)w, (float)h).reduced(0.5f);
  juce::Path shape; shape.addRoundedRectangle(r, radius);
  g.saveState();
  g.reduceClipRegion(shape);
  juce::ColourGradient grad(top, 0, 0, bottom, 0, (float)h, false);
  grad.addColour(0.5, top.interpolatedWith(bottom, 0.35f));
  g.setGradientFill(grad);
  g.fillAll();
  juce::Random rnd((juce::int64)seed);                     // brushed grain
  for (int y = 0; y < h; ++y) {
    const float a = rnd.nextFloat() * 0.06f;
    g.setColour((rnd.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha(a));
    const float x0 = rnd.nextFloat() * w * 0.3f, len = w * (0.5f + rnd.nextFloat() * 0.7f);
    g.drawHorizontalLine(y, x0, juce::jmin((float)w, x0 + len));
  }
  g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.20f), 0, 0,       // metallic sheen
                                         juce::Colours::white.withAlpha(0.0f), 0, h * 0.42f, false));
  g.fillAll();
  g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.0f), w * 0.55f, 0,
                                         juce::Colours::white.withAlpha(0.07f), (float)w, h * 0.3f, false));
  g.fillAll();
  g.restoreState();
  g.setColour(juce::Colours::white.withAlpha(0.35f)); g.strokePath(shape, juce::PathStrokeType(1.0f));
  g.setColour(juce::Colours::black.withAlpha(0.5f));
  g.drawRoundedRectangle(r.translated(0, 1.0f).reduced(0.5f), radius, 1.2f);
  return img;
}

// frosted glass panel with a curved sheen (aero)
static juce::Image glassPlate(int w, int h, float radius, juce::Colour top, juce::Colour bottom, juce::Colour edge) {
  juce::Image img(juce::Image::ARGB, juce::jmax(1, w), juce::jmax(1, h), true);
  juce::Graphics g(img);
  auto r = juce::Rectangle<float>(0, 0, (float)w, (float)h).reduced(5.f, 4.f).withTrimmedBottom(2.f);
  juce::Path shape; shape.addRoundedRectangle(r, radius);
  juce::DropShadow(juce::Colour(0x40063a1a), 9, { 0, 3 }).drawForPath(g, shape);
  g.setGradientFill(juce::ColourGradient(top.withAlpha(0.93f), 0, r.getY(), bottom.withAlpha(0.86f), 0, r.getBottom(), false));
  g.fillPath(shape);
  g.saveState();
  g.reduceClipRegion(shape);
  juce::Path sheen;
  const float sh = juce::jmin(60.f, r.getHeight() * 0.42f);
  sheen.startNewSubPath(r.getX(), r.getY());
  sheen.lineTo(r.getRight(), r.getY());
  sheen.lineTo(r.getRight(), r.getY() + sh * 0.55f);
  sheen.quadraticTo(r.getCentreX(), r.getY() + sh * 1.25f, r.getX(), r.getY() + sh * 0.7f);
  sheen.closeSubPath();
  g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.75f), 0, r.getY(),
                                         juce::Colours::white.withAlpha(0.15f), 0, r.getY() + sh, false));
  g.fillPath(sheen);
  g.setGradientFill(juce::ColourGradient(juce::Colour(0x0055e0c8), 0, r.getBottom() - r.getHeight() * 0.35f,
                                         juce::Colour(0x3055e0c8), 0, r.getBottom(), false));
  g.fillPath(shape);
  g.restoreState();
  g.setColour(juce::Colours::white.withAlpha(0.95f)); g.strokePath(shape, juce::PathStrokeType(1.6f));
  g.setColour(edge.withAlpha(0.45f));
  g.drawRoundedRectangle(r.expanded(0.8f), radius + 0.8f, 0.8f);
  return img;
}

juce::Image ReflexLook::makePanel(int w, int h, uint32_t seed) const {
  return metal() ? metalPlate(w, h, 10.f, plateTop, plateBottom, seed) : glassPlate(w, h, 18.f, plateTop, plateBottom, edge);
}

// ---------------------------------------------------------------- knobs
void ReflexLook::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) {
  auto area = juce::Rectangle<float>((float)x, (float)y, (float)w, (float)h);
  const float d = juce::jmin(area.getWidth(), area.getHeight());
  const auto c = area.getCentre();
  const bool bipolar = s.getProperties()["bipolar"];
  const float ang = a0 + pos * (a1 - a0);
  const float from = bipolar ? (a0 + a1) * 0.5f : a0;

  if (metal()) {   // chrome knob with a ring of LED dots
    const float ringR = d * 0.5f - 3.f;
    const float lo = juce::jmin(from, ang), hi = juce::jmax(from, ang);
    const int dots = 25;
    for (int i = 0; i < dots; ++i) {
      const float t = a0 + (a1 - a0) * (float)i / (dots - 1);
      const bool lit = s.isEnabled() && t >= lo - 1e-3f && t <= hi + 1e-3f && (hi - lo > 0.02f || std::abs(t - from) < 0.05f);
      const juce::Point<float> p(c.x + ringR * std::sin(t), c.y - ringR * std::cos(t));
      auto dot = juce::Rectangle<float>(3.6f, 3.6f).withCentre(p);
      if (lit) {
        g.setColour(accent.withAlpha(0.25f)); g.fillEllipse(dot.expanded(2.f));
        g.setColour(accent); g.fillEllipse(dot);
      } else {
        g.setColour(juce::Colours::black.withAlpha(0.4f)); g.fillEllipse(dot);
        g.setColour(juce::Colours::white.withAlpha(0.12f)); g.fillEllipse(dot.reduced(1.1f).translated(-0.3f, -0.3f));
      }
    }
    const float kr = ringR - 7.f;
    auto body = juce::Rectangle<float>(kr * 2.f, kr * 2.f).withCentre(c);
    g.setColour(juce::Colours::black.withAlpha(0.4f));
    g.fillEllipse(body.translated(0, 2.5f).expanded(1.5f));
    juce::ColourGradient rim(juce::Colour(0xff17462a), body.getX(), body.getY(), juce::Colour(0xffa9e2b8), body.getRight(), body.getBottom(), false);
    g.setGradientFill(rim); g.fillEllipse(body);
    auto dome = body.reduced(kr * 0.12f);
    juce::ColourGradient chrome(juce::Colour(0xfff4fff4), dome.getX() + dome.getWidth() * 0.3f, dome.getY() + dome.getHeight() * 0.25f,
                                juce::Colour(0xff4a7f5a), dome.getRight(), dome.getBottom(), true);
    chrome.addColour(0.35, juce::Colour(0xffcbe6d0));
    chrome.addColour(0.7, juce::Colour(0xff7bab8a));
    g.setGradientFill(chrome); g.fillEllipse(dome);
    g.setColour(juce::Colours::black.withAlpha(0.35f)); g.drawEllipse(dome, 0.8f);
    g.setColour(juce::Colours::white.withAlpha(0.10f));
    for (float rr = 0.35f; rr < 0.95f; rr += 0.2f) g.drawEllipse(dome.reduced(dome.getWidth() * (1.f - rr) * 0.5f), 0.6f);
    const juce::Point<float> p0(c.x + kr * 0.35f * std::sin(ang), c.y - kr * 0.35f * std::cos(ang));
    const juce::Point<float> p1(c.x + kr * 0.82f * std::sin(ang), c.y - kr * 0.82f * std::cos(ang));
    g.setColour(juce::Colour(0xff12321d)); g.drawLine(juce::Line<float>(p0, p1), 3.0f);
    auto tip = juce::Rectangle<float>(4.5f, 4.5f).withCentre(p1);
    g.setColour(accent.withAlpha(0.3f)); g.fillEllipse(tip.expanded(2.f));
    g.setColour(accent); g.fillEllipse(tip);
    return;
  }

  // aero: glossy gel orb inside a soft, rounded value arc
  const float arcR = d * 0.5f - 5.f;
  const juce::PathStrokeType round(6.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
  juce::Path track; track.addCentredArc(c.x, c.y, arcR, arcR, 0.f, a0, a1, true);
  g.setColour(juce::Colour(0x2a0f4d2b)); g.strokePath(track, juce::PathStrokeType(7.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
  g.setColour(juce::Colours::white.withAlpha(0.7f)); g.strokePath(track, juce::PathStrokeType(4.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
  if (std::abs(ang - from) > 0.01f && s.isEnabled()) {
    juce::Path val; val.addCentredArc(c.x, c.y, arcR, arcR, 0.f, juce::jmin(from, ang), juce::jmax(from, ang), true);
    g.setColour(juce::Colour(0x3355e0a0)); g.strokePath(val, juce::PathStrokeType(11.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff35c8c0), c.x - arcR, c.y + arcR, juce::Colour(0xff5fe04a), c.x + arcR, c.y - arcR, false));
    g.strokePath(val, round);
    g.setColour(juce::Colours::white.withAlpha(0.55f));
    g.strokePath(val, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                 juce::AffineTransform::translation(0, -1.2f));
  }
  const float kr = arcR - 8.5f;
  auto orb = juce::Rectangle<float>(kr * 2.f, kr * 2.f).withCentre(c);
  juce::Path orbPath; orbPath.addEllipse(orb);
  juce::DropShadow(juce::Colour(0x55063a1a), 6, { 0, 3 }).drawForPath(g, orbPath);
  juce::ColourGradient body(juce::Colour(0xffc4ff8f), c.x, orb.getBottom() - kr * 0.25f,
                            juce::Colour(0xff137a36), c.x, orb.getY() - kr * 0.2f, true);
  body.addColour(0.55, juce::Colour(0xff3fc25a));
  g.setGradientFill(body); g.fillEllipse(orb);
  auto hi = juce::Rectangle<float>(orb.getX() + kr * 0.3f, orb.getY() + kr * 0.08f, kr * 1.4f, kr * 0.95f);
  g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.92f), 0, hi.getY(),
                                         juce::Colours::white.withAlpha(0.08f), 0, hi.getBottom(), false));
  g.fillEllipse(hi);
  g.setColour(juce::Colour(0xff0d5a28).withAlpha(0.7f)); g.drawEllipse(orb, 1.f);
  g.setColour(juce::Colours::white.withAlpha(0.35f)); g.drawEllipse(orb.reduced(1.2f), 0.8f);
  const juce::Point<float> p(c.x + kr * 0.64f * std::sin(ang), c.y - kr * 0.64f * std::cos(ang));
  auto pearl = juce::Rectangle<float>(6.5f, 6.5f).withCentre(p);
  g.setColour(juce::Colour(0x66063a1a)); g.fillEllipse(pearl.translated(0, 1.f).expanded(0.8f));
  g.setGradientFill(juce::ColourGradient(juce::Colours::white, pearl.getX(), pearl.getY(), juce::Colour(0xffcfe9d6), pearl.getRight(), pearl.getBottom(), false));
  g.fillEllipse(pearl);
}

// mutate strength bar
void ReflexLook::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider&) {
  auto r = juce::Rectangle<float>((float)x, (float)y + h * 0.5f - 7.f, (float)w, 14.f);
  if (metal()) {
    g.setColour(lcd); g.fillRoundedRectangle(r, 4.f);
    g.setColour(juce::Colours::black.withAlpha(0.6f)); g.drawRoundedRectangle(r, 4.f, 1.f);
    auto fill = r.reduced(2.f).withRight(pos);
    g.setColour(accent.withAlpha(0.22f)); g.fillRoundedRectangle(fill.expanded(1.5f), 3.f);
    g.setColour(accent); g.fillRoundedRectangle(fill, 3.f);
    return;
  }
  g.setGradientFill(juce::ColourGradient(juce::Colour(0xffd3e8db), 0, r.getY(), juce::Colours::white, 0, r.getBottom(), false));
  g.fillRoundedRectangle(r, 7.f);
  g.setColour(edge); g.drawRoundedRectangle(r, 7.f, 1.f);
  auto fill = r.reduced(1.5f).withRight(juce::jmax(r.getX() + 12.f, pos));
  gelPill(g, fill, 5.5f, gelTop, gelBottom);
  auto thumb = juce::Rectangle<float>(20.f, 20.f).withCentre({ juce::jlimit(r.getX() + 10.f, r.getRight() - 10.f, pos), r.getCentreY() });
  juce::Path tp; tp.addEllipse(thumb);
  juce::DropShadow(juce::Colour(0x55063a1a), 4, { 0, 2 }).drawForPath(g, tp);
  g.setGradientFill(juce::ColourGradient(juce::Colours::white, 0, thumb.getY(), juce::Colour(0xffc5e6d0), 0, thumb.getBottom(), false));
  g.fillEllipse(thumb);
  g.setColour(edge.darker(0.2f)); g.drawEllipse(thumb, 1.f);
  glossCap(g, thumb, 10.f, 0.8f, 0.5f);
}

void ReflexLook::drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& b) {
  if (metal()) {
    auto r = juce::Rectangle<float>(0, 0, (float)w, (float)h).reduced(0.5f);
    g.setColour(lcd); g.fillRoundedRectangle(r, 4.f);
    g.setGradientFill(juce::ColourGradient(juce::Colours::black.withAlpha(0.5f), 0, 0, juce::Colours::transparentBlack, 0, h * 0.4f, false));
    g.fillRoundedRectangle(r, 4.f);
    g.setColour(juce::Colours::black.withAlpha(0.65f)); g.drawRoundedRectangle(r, 4.f, 1.f);
    g.setColour(juce::Colours::white.withAlpha(0.25f)); g.drawHorizontalLine(h - 1, 4.f, w - 4.f);
    juce::Path arrow; const float ax = w - 12.f, ay = h * 0.5f;
    arrow.addTriangle(ax - 4.5f, ay - 2.f, ax + 4.5f, ay - 2.f, ax, ay + 3.5f);
    g.setColour(b.isEnabled() ? accent : accentDim); g.fillPath(arrow);
    return;
  }
  auto r = juce::Rectangle<float>(0, 0, (float)w, (float)h).reduced(1.f);
  const float rad = r.getHeight() * 0.5f;
  g.setGradientFill(juce::ColourGradient(juce::Colours::white, 0, r.getY(), juce::Colour(0xffdff4e6), 0, r.getBottom(), false));
  g.fillRoundedRectangle(r, rad);
  glossCap(g, r, rad, 0.7f, 0.45f);
  g.setColour(b.hasKeyboardFocus(true) ? accent : edge); g.drawRoundedRectangle(r, rad, 1.f);
  auto bub = juce::Rectangle<float>(r.getHeight() - 6.f, r.getHeight() - 6.f).withCentre({ r.getRight() - rad, r.getCentreY() });
  gelPill(g, bub, bub.getHeight() * 0.5f, b.isEnabled() ? gelTop : accentDim, b.isEnabled() ? gelBottom : accentDim.darker(0.3f));
  juce::Path chev; const float cx = bub.getCentreX(), cy = bub.getCentreY() + 1.f;
  chev.startNewSubPath(cx - 3.5f, cy - 2.f); chev.lineTo(cx, cy + 1.8f); chev.lineTo(cx + 3.5f, cy - 2.f);
  g.setColour(juce::Colours::white);
  g.strokePath(chev, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void ReflexLook::drawPopupMenuBackground(juce::Graphics& g, int w, int h) {
  if (metal()) { g.fillAll(lcd); g.setColour(plateTop); g.drawRect(0, 0, w, h); return; }
  g.setGradientFill(juce::ColourGradient(juce::Colours::white, 0, 0, juce::Colour(0xffe2f6ea), 0, (float)h, false));
  g.fillAll();
  g.setColour(edge); g.drawRect(0, 0, w, h);
}

void ReflexLook::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                                   bool isTicked, bool hasSubMenu, const juce::String& text_, const juce::String& shortcut,
                                   const juce::Drawable* icon, const juce::Colour* textColour) {
  if (metal() || !isHighlighted || !isActive || isSeparator) {
    LookAndFeel_V4::drawPopupMenuItem(g, area, isSeparator, isActive, isHighlighted, isTicked, hasSubMenu, text_, shortcut, icon, textColour);
    return;
  }
  gelPill(g, area.toFloat().reduced(3.f, 1.f), (area.getHeight() - 2.f) * 0.5f, gelTop, gelBottom);
  const juce::Colour white = juce::Colours::white;
  LookAndFeel_V4::drawPopupMenuItem(g, area, isSeparator, isActive, false, isTicked, hasSubMenu, text_, shortcut, icon, &white);
}

void ReflexLook::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) {
  const bool hot = b.getProperties()["hot"];
  if (metal()) {
    auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    juce::Colour t = hot ? gelTop : juce::Colour(0xffb2e6bf);
    juce::Colour bt = hot ? gelBottom : juce::Colour(0xff3a8f55);
    if (over) { t = t.brighter(0.12f); bt = bt.brighter(0.12f); }
    if (down) std::swap(t, bt);
    g.setGradientFill(juce::ColourGradient(t, 0, r.getY(), bt, 0, r.getBottom(), false));
    g.fillRoundedRectangle(r, 5.f);
    g.setColour(juce::Colours::white.withAlpha(0.45f)); g.drawHorizontalLine((int)r.getY() + 1, r.getX() + 4.f, r.getRight() - 4.f);
    g.setColour(juce::Colours::black.withAlpha(0.6f)); g.drawRoundedRectangle(r, 5.f, 1.f);
    return;
  }
  auto r = b.getLocalBounds().toFloat().reduced(1.f, 1.5f);
  const float rad = r.getHeight() * 0.5f;
  juce::Path p; p.addRoundedRectangle(r, rad);
  if (!down) juce::DropShadow(juce::Colour(0x38063a1a), 4, { 0, 2 }).drawForPath(g, p);
  juce::Colour t = hot ? gelTop : juce::Colours::white;
  juce::Colour bt = hot ? gelBottom : juce::Colour(0xffc9ecd5);
  if (over) { t = t.brighter(0.12f); bt = bt.brighter(0.12f); }
  if (down) { t = t.darker(0.12f); bt = bt.darker(0.12f); }
  if (hot) gelPill(g, r, rad, t, bt);
  else {
    g.setGradientFill(juce::ColourGradient(t, 0, r.getY(), bt, 0, r.getBottom(), false));
    g.fillRoundedRectangle(r, rad);
    glossCap(g, r, rad, 0.85f, 0.48f);
    g.setColour(edge); g.drawRoundedRectangle(r, rad, 1.f);
  }
}

void ReflexLook::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) {
  g.setFont(getTextButtonFont(b, b.getHeight()));
  const bool hot = b.getProperties()["hot"];
  auto r = b.getLocalBounds();
  if (metal()) {
    g.setColour(juce::Colours::white.withAlpha(0.35f)); g.drawText(b.getButtonText(), r.translated(0, 1), juce::Justification::centred);
    g.setColour(hot ? juce::Colour(0xff22400c) : juce::Colour(0xff0b2a15));
  } else if (hot) {
    g.setColour(juce::Colour(0x800a4a20)); g.drawText(b.getButtonText(), r.translated(0, 1), juce::Justification::centred);
    g.setColour(juce::Colours::white);
  } else g.setColour(text);
  g.drawText(b.getButtonText(), r, juce::Justification::centred);
}

void ReflexLook::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool over, bool) {
  auto r = b.getLocalBounds().toFloat();
  const bool on = b.getToggleState();
  if (metal()) {   // LCD pill with a glowing LED
    auto pill = juce::Rectangle<float>(36.f, 16.f).withCentre({ r.getCentreX(), 15.f });
    g.setColour(lcd); g.fillRoundedRectangle(pill, 8.f);
    g.setColour(juce::Colours::black.withAlpha(0.65f)); g.drawRoundedRectangle(pill, 8.f, 1.f);
    if (on) {
      auto led = pill.reduced(2.5f);
      g.setColour(accent.withAlpha(0.28f)); g.fillRoundedRectangle(led.expanded(3.f), 9.f);
      g.setColour(accent); g.fillRoundedRectangle(led, 6.f);
    } else if (over) {
      g.setColour(accent.withAlpha(0.18f)); g.fillRoundedRectangle(pill.reduced(2.5f), 6.f);
    }
  } else {         // rounded slide switch with a pearl thumb
    auto pill = juce::Rectangle<float>(42.f, 20.f).withCentre({ r.getCentreX(), 15.f });
    if (on) gelPill(g, pill, 10.f, over ? gelTop.brighter(0.1f) : gelTop, gelBottom);
    else {
      g.setGradientFill(juce::ColourGradient(juce::Colour(0xffc8dccf), 0, pill.getY(), juce::Colour(0xfff4fbf6), 0, pill.getBottom(), false));
      g.fillRoundedRectangle(pill, 10.f);
      g.setColour(over ? accent : edge); g.drawRoundedRectangle(pill, 10.f, 1.f);
    }
    auto thumb = juce::Rectangle<float>(18.f, 18.f).withCentre({ on ? pill.getRight() - 10.f : pill.getX() + 10.f, pill.getCentreY() });
    juce::Path tp; tp.addEllipse(thumb);
    juce::DropShadow(juce::Colour(0x50063a1a), 3, { 0, 1 }).drawForPath(g, tp);
    g.setGradientFill(juce::ColourGradient(juce::Colours::white, 0, thumb.getY(), juce::Colour(0xffd0eadb), 0, thumb.getBottom(), false));
    g.fillEllipse(thumb);
    g.setColour(juce::Colour(0xff7fae90)); g.drawEllipse(thumb, 0.9f);
  }
  g.setFont(uiFont(12.5f, true));
  drawCaption(g, b.getButtonText(), r.withTrimmedTop(29.f).removeFromTop(16.f), juce::Justification::centredTop);
}

// ============================================================ controls
Knob::Knob(ReflexProcessor& p, int idx, const juce::String& cap) : caption(cap) {
  slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
  slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
  slider.setMouseDragSensitivity(220);
  const bool bip = (idx >= EGPWM && idx <= EGDet) || (idx >= EG2Res && idx <= EG2Ptc) || idx == AMPpan || idx == VelCut || idx == KeyCut || idx == VelVol
                   || idx == OSC1sm || idx == OSC2sm || idx == OSC3sm
                   || ((idx >= LFO1De && idx <= LFO3Fl) && idx != LFO2frq && idx != LFO3frq && idx != LFO1trg && idx != LFO2trg);
  slider.getProperties().set("bipolar", bip);
  addAndMakeVisible(slider);
  value.setJustificationType(juce::Justification::centred);
  value.setFont(uiFont(12.5f, true));
  value.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
  value.setInterceptsMouseClicks(false, false);
  addAndMakeVisible(value);
  auto* param = p.apvts.getParameter(ReflexProcessor::paramID(idx));
  att = std::make_unique<juce::SliderParameterAttachment>(*static_cast<juce::RangedAudioParameter*>(param), slider);
  auto upd = [this, param] { value.setText(param->getCurrentValueAsText(), juce::dontSendNotification); };
  slider.onValueChange = upd; upd();
  slider.setDoubleClickReturnValue(true, (double)param->getDefaultValue());
  slider.setTooltip(param->getName(64));
}
void Knob::lookAndFeelChanged() { value.setColour(juce::Label::textColourId, ReflexLook::of(*this).lcdText); }
void Knob::resized() {
  auto r = getLocalBounds();
  r.removeFromTop(16);
  value.setBounds(r.removeFromBottom(18).reduced(9, 0));
  r.removeFromBottom(1);
  slider.setBounds(r);
}
void Knob::paint(juce::Graphics& g) {
  auto& L = ReflexLook::of(*this);
  g.setFont(uiFont(12.5f, true));
  L.drawCaption(g, caption, getLocalBounds().removeFromTop(16).toFloat(), juce::Justification::centred);
  auto v = value.getBounds().toFloat();
  if (L.metal()) {   // small LCD read-out
    g.setColour(L.lcd.withAlpha(0.88f)); g.fillRect(v.withTrimmedTop(1.f));
    return;
  }
  v = v.reduced(0.5f);   // glass pill
  g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.95f), 0, v.getY(), juce::Colour(0xe0dcf3e4), 0, v.getBottom(), false));
  g.fillRoundedRectangle(v, v.getHeight() * 0.5f);
  g.setColour(L.edge.withAlpha(0.8f)); g.drawRoundedRectangle(v, v.getHeight() * 0.5f, 0.9f);
}

Choice::Choice(ReflexProcessor& p, int idx, const juce::String& cap, const juce::StringArray& items, int nSteps)
  : caption(cap), n(nSteps > 0 ? nSteps : items.size()) {
  box.addItemList(items, 1);
  box.setJustificationType(juce::Justification::centred);
  addAndMakeVisible(box);
  auto* param = p.apvts.getParameter(ReflexProcessor::paramID(idx));
  // two kinds of stepped parameters: "rounded" (items = steps + 1, index = round(v * steps)) and "binned" (index = floor(v * items))
  rounded = items.size() == n + 1;
  att = std::make_unique<juce::ParameterAttachment>(*param, [this, items](float v) {
    const int i = rounded ? (int)std::lround(v * n) : (int)(v * n + 1e-4f);
    box.setSelectedItemIndex(juce::jlimit(0, items.size() - 1, i), juce::dontSendNotification);
  });
  box.onChange = [this] {
    const int i = box.getSelectedItemIndex();
    if (i >= 0) att->setValueAsCompleteGesture(rounded ? (float)i / (float)n : (i + 0.5f) / n);
  };
  att->sendInitialUpdate();
}
void Choice::resized() { auto r = getLocalBounds(); r.removeFromTop(18); box.setBounds(r.removeFromTop(28).reduced(2, 0)); }
void Choice::paint(juce::Graphics& g) {
  g.setFont(uiFont(12.5f, true));
  ReflexLook::of(*this).drawCaption(g, caption, getLocalBounds().removeFromTop(16).toFloat(), juce::Justification::centred);
}

Switch::Switch(ReflexProcessor& p, int idx, const juce::String& cap) {
  button.setButtonText(cap);
  addAndMakeVisible(button);
  auto* param = p.apvts.getParameter(ReflexProcessor::paramID(idx));
  att = std::make_unique<juce::ParameterAttachment>(*param, [this](float v) { button.setToggleState(v >= 0.5f, juce::dontSendNotification); });
  button.onClick = [this] { att->setValueAsCompleteGesture(button.getToggleState() ? 1.f : 0.f); };
  att->sendInitialUpdate();
}
void Switch::resized() { button.setBounds(getLocalBounds().withTrimmedTop(20)); }

GateSteps::GateSteps(ReflexProcessor& p, int idx) {
  auto* param = p.apvts.getParameter(ReflexProcessor::paramID(idx));
  att = std::make_unique<juce::ParameterAttachment>(*param, [this](float v) { mask = Map::tgPattern(v); repaint(); });
  att->sendInitialUpdate();
}
int GateSteps::stepAt(juce::Point<float> p) const { const float w = getWidth() / 16.f; return juce::jlimit(0, 15, (int)(p.x / w)); }
void GateSteps::setStep(int s, bool on) {
  const unsigned bit = 1u << s;
  const unsigned m = on ? (mask | bit) : (mask & ~bit);
  if (m != mask) { mask = m; att->setValueAsCompleteGesture((float)(mask / 65535.0)); repaint(); }
}
void GateSteps::mouseDown(const juce::MouseEvent& e) {
  const int s = stepAt(e.position); const bool cur = (mask >> s) & 1u; dragValue = cur ? 0 : 1; setStep(s, dragValue);
}
void GateSteps::mouseDrag(const juce::MouseEvent& e) { if (dragValue >= 0) setStep(stepAt(e.position), dragValue); }
void GateSteps::paint(juce::Graphics& g) {
  auto& L = ReflexLook::of(*this);
  const float w = getWidth() / 16.f;
  for (int s = 0; s < 16; ++s) {
    const bool on = (mask >> s) & 1u;
    if (L.metal()) {
      auto r = juce::Rectangle<float>(s * w + 2.f, 0.f, w - 4.f, (float)getHeight());
      g.setColour(L.lcd); g.fillRoundedRectangle(r, 4.f);
      g.setColour(juce::Colours::black.withAlpha(0.6f)); g.drawRoundedRectangle(r, 4.f, 1.f);
      auto led = r.reduced(3.f);
      if (on) {
        g.setColour(L.accent.withAlpha(0.28f)); g.fillRoundedRectangle(led.expanded(2.f), 4.f);
        g.setColour(L.accent); g.fillRoundedRectangle(led, 3.f);
      } else {
        g.setColour(L.accentDim.withAlpha(s % 4 == 0 ? 0.9f : 0.5f)); g.fillRoundedRectangle(led, 3.f);
      }
      continue;
    }
    auto r = juce::Rectangle<float>(s * w + 2.f, 1.f, w - 4.f, (float)getHeight() - 3.f);
    const float rad = juce::jmin(r.getWidth(), r.getHeight()) * 0.5f;
    if (on) gelPill(g, r, rad, L.gelTop, L.gelBottom);
    else {
      g.setGradientFill(juce::ColourGradient(juce::Colour(s % 4 == 0 ? 0xffcfe6d7 : 0xffdcefe3), 0, r.getY(), juce::Colours::white, 0, r.getBottom(), false));
      g.fillRoundedRectangle(r, rad);
      g.setColour(L.edge); g.drawRoundedRectangle(r, rad, 1.f);
    }
  }
}

void Section::paint(juce::Graphics& g) {
  auto& L = ReflexLook::of(*this);
  if (plate.getWidth() != getWidth() || plate.getHeight() != getHeight() || plateTheme != L.theme) {
    plate = L.makePanel(getWidth(), getHeight(), (uint32_t)title.hashCode());
    plateTheme = L.theme;
  }
  g.drawImageAt(plate, 0, 0);
  const auto t = title.toUpperCase();
  if (L.metal()) {   // engraved title and two screws
    g.setFont(uiFont(14.f, true));
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    auto tr = getLocalBounds().toFloat().reduced(14.f, 7.f).removeFromTop(18.f);
    g.drawText(t, tr.translated(0, 1.f), juce::Justification::centredLeft);
    g.setColour(juce::Colours::white); g.drawText(t, tr, juce::Justification::centredLeft);
    for (auto pt : { juce::Point<float>(getWidth() - 11.f, 11.f), juce::Point<float>(getWidth() - 11.f, getHeight() - 11.f) }) {
      auto sc = juce::Rectangle<float>(7.f, 7.f).withCentre(pt);
      g.setGradientFill(juce::ColourGradient(juce::Colour(0xfff0fff0), sc.getX(), sc.getY(), juce::Colour(0xff285e38), sc.getRight(), sc.getBottom(), false));
      g.fillEllipse(sc);
      g.setColour(juce::Colours::black.withAlpha(0.5f)); g.drawLine(sc.getX() + 1.f, sc.getCentreY(), sc.getRight() - 1.f, sc.getCentreY(), 0.9f);
    }
    return;
  }
  // title on a little gel tab
  g.setFont(uiFont(13.5f, true));
#if JUCE_MAJOR_VERSION >= 8
  const float tw = juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), t) + 26.f;
#else
  const float tw = g.getCurrentFont().getStringWidthFloat(t) + 26.f;
#endif
  auto tab = juce::Rectangle<float>(14.f, 9.f, tw, 21.f);
  gelPill(g, tab, 10.5f, L.gelTop, L.gelBottom);
  g.setColour(juce::Colour(0x700a4a20)); g.drawText(t, tab.translated(0, 1.f), juce::Justification::centred);
  g.setColour(juce::Colours::white); g.drawText(t, tab, juce::Justification::centred);
}

// ---------------------------------------------------------------- backdrops
static juce::Image aeroBackdrop(int w, int h) {   // sky-to-meadow with light ribbons and bubbles
  juce::Image img(juce::Image::RGB, w, h, false);
  juce::Graphics g(img);
  juce::ColourGradient sky(juce::Colour(0xff62c6f2), 0, 0, juce::Colour(0xff2f9f4c), 0, (float)h, false);
  sky.addColour(0.22, juce::Colour(0xff9fe2f5));
  sky.addColour(0.48, juce::Colour(0xff8fe39a));
  sky.addColour(0.75, juce::Colour(0xff4cc062));
  g.setGradientFill(sky); g.fillAll();
  g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.7f), w * 0.86f, h * 0.02f,
                                         juce::Colours::white.withAlpha(0.f), w * 0.86f + h * 0.45f, h * 0.02f, true));
  g.fillAll();
  for (int k = 0; k < 3; ++k) {
    const float y0 = h * (0.3f + 0.2f * k), amp = h * (0.10f + 0.04f * k), th = 26.f + 18.f * k;
    juce::Path rib;
    rib.startNewSubPath(-20.f, y0);
    rib.cubicTo(w * 0.3f, y0 - amp, w * 0.6f, y0 + amp, w + 20.f, y0 - amp * 0.5f);
    rib.lineTo(w + 20.f, y0 - amp * 0.5f + th);
    rib.cubicTo(w * 0.6f, y0 + amp + th * 0.4f, w * 0.3f, y0 - amp + th * 1.4f, -20.f, y0 + th);
    rib.closeSubPath();
    g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.f), 0, 0, juce::Colours::white.withAlpha(0.32f - 0.07f * k), w * 0.5f, 0, false));
    g.fillPath(rib);
  }
  juce::Random rnd(2007);
  for (int i = 0; i < 70; ++i) {
    const float r = 4.f + std::pow(rnd.nextFloat(), 2.2f) * 46.f;
    const float x = rnd.nextFloat() * w, y = rnd.nextFloat() * h;
    auto b = juce::Rectangle<float>(r * 2.f, r * 2.f).withCentre({ x, y });
    juce::ColourGradient fill(juce::Colours::white.withAlpha(0.02f), x, y, juce::Colours::white.withAlpha(0.32f), x + r, y, true);
    g.setGradientFill(fill); g.fillEllipse(b);
    g.setColour(juce::Colours::white.withAlpha(0.45f)); g.drawEllipse(b, 1.f);
    auto spec = juce::Rectangle<float>(r * 0.7f, r * 0.45f).withCentre({ x - r * 0.35f, y - r * 0.45f });
    g.setColour(juce::Colours::white.withAlpha(0.6f)); g.fillEllipse(spec);
  }
  return img;
}

static juce::Image metalBackdrop(int w, int h, juce::Colour bottom) {   // brushed green
  juce::Image img(juce::Image::RGB, w, h, false);
  juce::Graphics g(img);
  juce::ColourGradient grad(juce::Colour(0xff3f9c5c), 0, 0, bottom, 0, (float)h, false);
  grad.addColour(0.45, juce::Colour(0xff2f8549));
  g.setGradientFill(grad);
  g.fillAll();
  juce::Random rnd(99);
  for (int y = 0; y < h; y += 2) {
    g.setColour(juce::Colours::white.withAlpha(0.012f + 0.03f * rnd.nextFloat()));
    g.fillRect(0, y, w, 1);
  }
  return img;
}

void Canvas::paint(juce::Graphics& g) {
  auto& L = ReflexLook::of(*this);
  if (background.getWidth() != getWidth() || background.getHeight() != getHeight() || cacheTheme != L.theme) {
    background = L.metal() ? metalBackdrop(getWidth(), getHeight(), L.bg) : aeroBackdrop(getWidth(), getHeight());
    headerPlate = L.metal() ? metalPlate(getWidth() - 16, 104, 10.f, juce::Colour(0xff7fd497), juce::Colour(0xff2f8a4f), 7u)
                            : glassPlate(getWidth() - 8, 112, 26.f, juce::Colour(0xfff8fffb), juce::Colour(0xffcdeefa), L.edge);
    cacheTheme = L.theme;
  }
  g.drawImageAt(background, 0, 0);

  if (L.metal()) {
    g.drawImageAt(headerPlate, 8, 6);
    auto logo = juce::Rectangle<float>(26.f, 12.f, 260.f, 50.f);
    g.setFont(uiFont(36.f, true));
    g.setColour(juce::Colours::black.withAlpha(0.45f)); g.drawText("REFLEX", logo.translated(0, 2.f), juce::Justification::centredLeft);
    g.setColour(juce::Colours::white); g.drawText("REFLEX", logo, juce::Justification::centredLeft);
    g.setFont(uiFont(13.f, true));
    L.drawCaption(g, "VIRTUAL ANALOG SYNTH", juce::Rectangle<float>(28.f, 58.f, 260.f, 16.f), juce::Justification::centredLeft);
    return;
  }

  g.drawImageAt(headerPlate, 4, 2);
  // logo: glossy gel lettering with a white outline
  juce::GlyphArrangement ga;
  ga.addLineOfText(uiFont(50.f, true), "reflex", 32.f, 62.f);
  juce::Path logo; ga.createPath(logo);
  auto lb = logo.getBounds();
  g.setColour(juce::Colour(0x40063a1a)); g.fillPath(logo, juce::AffineTransform::translation(0, 3.f));
  g.setColour(juce::Colours::white); g.strokePath(logo, juce::PathStrokeType(5.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
  g.setGradientFill(juce::ColourGradient(juce::Colour(0xff8ff05c), 0, lb.getY(), juce::Colour(0xff128a3c), 0, lb.getBottom(), false));
  g.fillPath(logo);
  g.saveState();
  g.reduceClipRegion(logo);
  g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.75f), 0, lb.getY(), juce::Colours::white.withAlpha(0.1f), 0, lb.getCentreY(), false));
  g.fillRect(lb.withHeight(lb.getHeight() * 0.48f));
  g.restoreState();
  g.setFont(uiFont(14.f, true));
  g.setColour(juce::Colours::white.withAlpha(0.75f));
  g.drawText("virtual analog synth", juce::Rectangle<float>(36.f, 71.f, 260.f, 18.f), juce::Justification::centredLeft);
  g.setColour(juce::Colour(0xff1c7a8a));
  g.drawText("virtual analog synth", juce::Rectangle<float>(36.f, 70.f, 260.f, 18.f), juce::Justification::centredLeft);
}

// clicking the logo switches between the two looks
void Canvas::mouseDown(const juce::MouseEvent& e) {
  if (logoArea().contains(e.getPosition()) && onLogoClick) onLogoClick();
}
void Canvas::mouseMove(const juce::MouseEvent& e) {
  setMouseCursor(logoArea().contains(e.getPosition()) ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

// ============================================================ editor
static juce::StringArray waveNames() { juce::StringArray a; for (int i = 0; i < 32; ++i) a.add(waveName(i)); return a; }
static juce::StringArray filterNames() { juce::StringArray a; for (int i = 0; i < 13; ++i) a.add(filterName(i)); return a; }
static juce::StringArray syncNames(int n) {
  juce::StringArray a;
  for (int i = 0; i < n; ++i) a.add(n == 27 ? rfx::delayName(i) : rfx::rateName(i));
  return a;
}

Knob* ReflexEditor::knob(const juce::String& sec, int idx, const juce::String& cap) {
  auto* k = new Knob(proc, idx, cap); controls.add(k); canvas.addAndMakeVisible(k); sectionControls[sec].push_back(k); return k;
}
Choice* ReflexEditor::choice(const juce::String& sec, int idx, const juce::String& cap, const juce::StringArray& items, int n) {
  auto* c = new Choice(proc, idx, cap, items, n); controls.add(c); canvas.addAndMakeVisible(c); sectionControls[sec].push_back(c); return c;
}
Switch* ReflexEditor::sw(const juce::String& sec, int idx, const juce::String& cap) {
  auto* s = new Switch(proc, idx, cap); controls.add(s); canvas.addAndMakeVisible(s); sectionControls[sec].push_back(s); return s;
}

ReflexEditor::ReflexEditor(ReflexProcessor& p) : AudioProcessorEditor(&p), proc(p) {
  look.setTheme(proc.uiTheme);
  setLookAndFeel(&look);
  addAndMakeVisible(canvas);
  canvas.setBounds(0, 0, BaseW, BaseH);

  const char* secs[] = { "Oscillators", "Unison", "Filter", "Amp", "Envelope 1", "Envelope 2", "LFOs", "Arpeggiator", "Trance Gate", "Delay", "Reverb", "Global" };
  for (auto* s : secs) { auto* sc = new Section(s); sections.add(sc); canvas.addAndMakeVisible(sc); sc->toBack(); }

  auto W = waveNames();
  choice("Oscillators", OSC1, "Osc 1", W); choice("Oscillators", OSC2, "Osc 2", W); choice("Oscillators", OSC3, "Osc 3", W);
  knob("Oscillators", OSC1sm, "Semi"); knob("Oscillators", OSC2sm, "Semi"); knob("Oscillators", OSC3sm, "Semi");
  knob("Oscillators", OSC1vl, "Level"); knob("Oscillators", OSC2vl, "Level"); knob("Oscillators", OSC3vl, "Level");

  knob("Unison", OSCfat, "Fat"); knob("Unison", FATvcs, "Voices");
  knob("Unison", OSCage, "Drift"); sw("Unison", OSCtrg, "Retrig");

  choice("Filter", FLTtp, "Type", filterNames());
  knob("Filter", FLTf, "Cutoff"); knob("Filter", FLTQ, "Reso");
  knob("Filter", VelCut, "Vel"); knob("Filter", KeyCut, "Key");

  knob("Amp", AMPdrv, "Drive"); knob("Amp", AMPvol, "Volume"); knob("Amp", dummy6, "Master");
  knob("Amp", AMPpan, "Pan"); knob("Amp", AMPspd, "Spread"); knob("Amp", VelVol, "Vel");

  knob("Envelope 1", Attack, "Attack"); knob("Envelope 1", Decay, "Decay"); knob("Envelope 1", Sustain, "Sustain"); knob("Envelope 1", Release, "Release");
  knob("Envelope 1", EGVol, "> Vol"); knob("Envelope 1", EGPWM, "> PWM"); knob("Envelope 1", EGFlt, "> Cutoff"); knob("Envelope 1", EGDet, "> Detune");

  knob("Envelope 2", Attack2, "Attack"); knob("Envelope 2", Decay2, "Decay"); knob("Envelope 2", Sustain2, "Sustain"); knob("Envelope 2", Release2, "Release");
  knob("Envelope 2", EG2Res, "> Reso"); knob("Envelope 2", EG2PWM, "> PWM"); knob("Envelope 2", EG2Flt, "> Cutoff"); knob("Envelope 2", EG2Ptc, "> Pitch");

  for (int l = 0; l < 3; ++l) {
    const juce::String n = "LFO " + juce::String(l + 1);
    knob("LFOs", LFO1frq + 5 * l, n + " Rate"); knob("LFOs", LFO1De + 5 * l, "> Detune"); knob("LFOs", LFO1PW + 5 * l, "> PWM");
    knob("LFOs", LFO1Fl + 5 * l, "> Cutoff"); sw("LFOs", LFO1trg + 5 * l, "Retrig");
  }

  sw("Arpeggiator", ARPsta, "On"); choice("Arpeggiator", ARPmod, "Mode", { "Up", "Down", "Up/Dn", "Order", "Random" });
  choice("Arpeggiator", ARPtme, "Speed", syncNames(21), 20);
  choice("Arpeggiator", ARPgte, "Gate", syncNames(21)); choice("Arpeggiator", ARPoct, "Octaves", { "1", "2", "3", "4" }); sw("Arpeggiator", ARPtrg, "Retrig");

  sw("Trance Gate", TGstat, "On"); choice("Trance Gate", TGspd, "Speed", syncNames(21), 20);
  knob("Trance Gate", TGcont, "Contour"); knob("Trance Gate", TGmix, "Mix"); sw("Trance Gate", TGstr, "Stereo");
  gate = new GateSteps(proc, TGpat); controls.add(gate); canvas.addAndMakeVisible(gate);

  sw("Delay", DLYsta, "On"); choice("Delay", DLYmod, "Mode", { "Mono", "Stereo", "PingPong", "Cross" });
  choice("Delay", DLYtme, "Time", syncNames(27), 26);
  knob("Delay", DLYfbk, "Feedback"); knob("Delay", DLYdmp, "Damp"); knob("Delay", DLYmix, "Mix");

  sw("Reverb", REVsta, "On"); knob("Reverb", REVpdl, "Predelay"); knob("Reverb", REVroo, "Room");
  knob("Reverb", REVdmp, "Damp"); knob("Reverb", REVwdt, "Width"); knob("Reverb", REVmix, "Mix");

  knob("Global", Voices, "Poly"); sw("Global", GLDst, "Glide"); knob("Global", GLDspd, "Glide T");
  knob("Global", PBrng, "Bend"); knob("Global", VIBspd, "Vib Rate"); knob("Global", VIBdpt, "Vib Depth");
  sw("Global", dummy13, "Mono"); sw("Global", dummy18, "Clean EQ");

  // ---- preset bar ----
  for (auto* c : std::initializer_list<juce::Component*> { &presetBox, &prevBtn, &nextBtn, &loadBtn, &saveBtn, &saveBankBtn,
                                                          &randomType, &randomBtn, &mutateBtn, &variation, &variationLabel, &randomLabel, &zoomBox })
    canvas.addAndMakeVisible(c);
  presetBox.setTooltip("Presets of the loaded bank");
  presetBox.onChange = [this] { const int i = presetBox.getSelectedItemIndex(); if (i >= 0 && i != proc.getCurrentProgram()) proc.setCurrentProgram(i); };
  prevBtn.onClick = [this] { const int n = proc.getNumPrograms(); if (n) { proc.setCurrentProgram((proc.getCurrentProgram() + n - 1) % n); refreshPresetList(); } };
  nextBtn.onClick = [this] { const int n = proc.getNumPrograms(); if (n) { proc.setCurrentProgram((proc.getCurrentProgram() + 1) % n); refreshPresetList(); } };
  loadBtn.setTooltip("Load a Reflex .xml preset/bank or a VST2 .fxp/.fxb preset/bank");
  loadBtn.onClick = [this] {
    chooser = std::make_unique<juce::FileChooser>("Load preset or bank (.xml, .fxp, .fxb)", juce::File(), "*.fxb;*.fxp;*.xml");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
      auto f = fc.getResult(); if (!f.existsAsFile()) return;
      juce::String err; const bool ok = f.hasFileExtension("xml") ? proc.loadXmlPreset(f, err) : proc.loadFxbOrFxp(f, err);
      if (!ok) juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Reflex", err);
      refreshPresetList();
    });
  };
  auto saveAs = [this](bool bank, bool original) {
    const juce::String ext = original ? (bank ? "fxb" : "fxp") : "xml";
    chooser = std::make_unique<juce::FileChooser>(bank ? "Save bank" : "Save preset", juce::File(), "*." + ext);
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this, bank, original, ext](const juce::FileChooser& fc) {
      auto f = fc.getResult(); if (f == juce::File()) return;
      f = f.withFileExtension(ext);
      if (!bank) proc.currentPresetName = f.getFileNameWithoutExtension();
      const bool ok = bank ? (original ? proc.saveFxb(f) : proc.saveXmlBank(f)) : (original ? proc.saveFxp(f) : proc.saveXmlPreset(f));
      if (!ok) juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Reflex", "Could not write " + f.getFullPathName());
      refreshPresetList();
    });
  };
  auto menuFor = [this, saveAs](bool bank, juce::Button& b) {
    juce::PopupMenu m; m.setLookAndFeel(&look);
    m.addItem(1, bank ? "Bank as Reflex XML (.xml)" : "Preset as Reflex XML (.xml)");
    m.addItem(2, bank ? "Bank in VST2 format (.fxb)" : "Preset in VST2 format (.fxp)");
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&b), [saveAs, bank](int r) { if (r > 0) saveAs(bank, r == 2); });
  };
  saveBtn.onClick = [this, menuFor] { menuFor(false, saveBtn); };
  saveBankBtn.onClick = [this, menuFor] { menuFor(true, saveBankBtn); };

  // ---- randomizer ----
  {
    const auto& m = randomizer::menu();
    for (int i = 0; i < (int)m.size(); ++i) {
      if (m[(size_t)i].kind == randomizer::Kind::Mutate) { mutateIndex = i; continue; }   // has its own button
      if (!m[(size_t)i].heading.empty()) randomType.addSectionHeading(m[(size_t)i].heading);
      randomType.addItem(m[(size_t)i].name, i + 1);
    }
    if (proc.randomMenuIndex == mutateIndex) proc.randomMenuIndex = 0;
    randomType.setSelectedId(proc.randomMenuIndex + 1, juce::dontSendNotification);
    randomType.setTooltip("Kind of patch RANDOM creates");
    randomType.onChange = [this] { if (randomType.getSelectedId() > 0) proc.randomMenuIndex = randomType.getSelectedId() - 1; };
  }
  randomBtn.getProperties().set("hot", true);
  randomBtn.setTooltip("Create a new patch of the chosen kind");
  randomBtn.onClick = [this] { proc.randomize(juce::jmax(0, randomType.getSelectedId() - 1), (float)variation.getValue()); };
  mutateBtn.getProperties().set("hot", true);
  mutateBtn.setTooltip("Mutate the current sound by the Mutate Strength amount (pitch stays untouched)");
  mutateBtn.onClick = [this] { if (mutateIndex >= 0) { const int keep = proc.randomMenuIndex; proc.randomize(mutateIndex, (float)variation.getValue()); proc.randomMenuIndex = keep; } };
  variation.setSliderStyle(juce::Slider::LinearHorizontal);
  variation.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  variation.setRange(0.0, 1.0, 0.0);
  variation.setValue(proc.randomVariation, juce::dontSendNotification);
  variation.setTooltip("Mutate strength: how far MUTATE moves the current sound; also how far RANDOM strays from the preset it starts from (0 % = the original preset)");
  variation.onValueChange = [this] {
    proc.randomVariation = (float)variation.getValue();
    variationLabel.setText("MUTATE STRENGTH " + juce::String(juce::roundToInt(variation.getValue() * 100)) + "%", juce::dontSendNotification);
  };
  variation.onValueChange();
  for (auto* l : { &variationLabel, &randomLabel }) {
    l->setFont(uiFont(12.f, true)); l->setJustificationType(juce::Justification::centredLeft);
  }
  randomLabel.setText("RANDOM PRESET", juce::dontSendNotification);

  // ---- zoom ----
  const int zooms[] = { 50, 60, 70, 80, 90, 100, 115, 130, 150 };
  for (int i = 0; i < 9; ++i) zoomBox.addItem(juce::String(zooms[i]) + " %", zooms[i]);
  zoomBox.setTooltip("Window size (you can also drag the corner)");
  zoomBox.onChange = [this] { if (zoomBox.getSelectedId() > 0) setZoom(zoomBox.getSelectedId() / 100.f); };

  canvas.onLogoClick = [this] { applyTheme(look.metal() ? ReflexLook::Aero : ReflexLook::Metal); };
  applyTheme(proc.uiTheme);

  layoutCanvas();
  {
    juce::Component::SafePointer<ReflexEditor> safe(this);
    proc.onPresetListChanged = [safe] {
      juce::MessageManager::callAsync([safe] { if (auto* e = safe.getComponent()) e->refreshPresetList(); });
    };
  }
  refreshPresetList();

  setResizable(true, true);
  if (auto* c = getConstrainer()) {
    c->setFixedAspectRatio((double)BaseW / BaseH);
    c->setSizeLimits(BaseW / 2, BaseH / 2, BaseW * 2, BaseH * 2);
  }
  setSize(juce::roundToInt(BaseW * proc.uiScale), juce::roundToInt(BaseH * proc.uiScale));
}

ReflexEditor::~ReflexEditor() {
  proc.onPresetListChanged = nullptr;
  setLookAndFeel(nullptr);
}

void ReflexEditor::setZoom(float z) {
  setSize(juce::roundToInt(BaseW * z), juce::roundToInt(BaseH * z));
}

void ReflexEditor::refreshPresetList() {
  presetBox.clear(juce::dontSendNotification);
  for (int i = 0; i < proc.getNumPrograms(); ++i) presetBox.addItem(juce::String(i + 1).paddedLeft('0', 3) + "  " + proc.getProgramName(i), i + 1);
  presetBox.setSelectedItemIndex(proc.getCurrentProgram(), juce::dontSendNotification);
  // a randomized / renamed / loaded single preset shows its own name
  if (proc.currentPresetName.isNotEmpty() && proc.currentPresetName != proc.getProgramName(proc.getCurrentProgram()))
    presetBox.setText(proc.currentPresetName, juce::dontSendNotification);
}

void ReflexEditor::paint(juce::Graphics& g) { g.fillAll(look.bg); }

void ReflexEditor::applyTheme(int t) {
  look.setTheme(t);
  proc.uiTheme = look.theme;
  for (auto* l : { &variationLabel, &randomLabel }) l->setColour(juce::Label::textColourId, look.caption);
  sendLookAndFeelChange();
  repaint();
}

void ReflexEditor::resized() {
  const float s = (float)getWidth() / (float)BaseW;
  canvas.setTransform(juce::AffineTransform::scale(s));
  proc.uiScale = s;
  const int pct = juce::roundToInt(s * 100.f);
  zoomBox.setText(juce::String(pct) + " %", juce::dontSendNotification);
}

void ReflexEditor::layoutSection(const juce::String& name, juce::Rectangle<int> r, int cols) {
  for (auto* s : sections) if (s->title == name) s->setBounds(r);
  auto inner = r.reduced(12, 0).withTrimmedTop(32).withTrimmedBottom(8);
  auto& cs = sectionControls[name];
  const int cw = 86, ch = 98;
  for (size_t i = 0; i < cs.size(); ++i) {
    const int col = (int)i % cols, row = (int)i / cols;
    cs[i]->setBounds(inner.getX() + col * cw, inner.getY() + row * ch, cw, ch);
  }
}

void ReflexEditor::layoutCanvas() {
  // header: two rows inside the header plate
  auto head = juce::Rectangle<int>(8, 6, BaseW - 16, 104);
  auto row1 = head.withTrimmedLeft(300).withTrimmedRight(16).withTrimmedTop(12).withHeight(38);
  saveBankBtn.setBounds(row1.removeFromRight(112)); row1.removeFromRight(8);
  saveBtn.setBounds(row1.removeFromRight(120)); row1.removeFromRight(8);
  loadBtn.setBounds(row1.removeFromRight(96)); row1.removeFromRight(14);
  nextBtn.setBounds(row1.removeFromRight(36)); row1.removeFromRight(6);
  prevBtn.setBounds(row1.removeFromRight(36)); row1.removeFromRight(8);
  presetBox.setBounds(row1);

  auto row2 = head.withTrimmedLeft(300).withTrimmedRight(16).withTrimmedTop(58).withHeight(36);
  randomLabel.setBounds(row2.removeFromLeft(124));
  randomType.setBounds(row2.removeFromLeft(220)); row2.removeFromLeft(10);
  randomBtn.setBounds(row2.removeFromLeft(110)); row2.removeFromLeft(26);
  zoomBox.setBounds(row2.removeFromRight(100)); row2.removeFromRight(18);
  mutateBtn.setBounds(row2.removeFromRight(110)); row2.removeFromRight(10);
  variationLabel.setBounds(row2.removeFromLeft(160));
  variation.setBounds(row2.reduced(0, 4));

  auto r = juce::Rectangle<int>(0, 118, BaseW, BaseH - 118).reduced(12, 0);
  const int g = 10, ch = 98, top = 40;
  auto rowA = r.removeFromTop(top + 3 * ch); r.removeFromTop(g);
  auto rowB = r.removeFromTop(top + 3 * ch); r.removeFromTop(g);
  auto rowC = r.removeFromTop(top + 2 * ch + 4);
  auto take = [&](juce::Rectangle<int>& row, int cols) { auto s = row.removeFromLeft(cols * 86 + 24); row.removeFromLeft(g); return s; };
  layoutSection("Oscillators", take(rowA, 3), 3);
  layoutSection("Unison", take(rowA, 2), 2);
  layoutSection("Filter", take(rowA, 3), 3);
  layoutSection("Amp", take(rowA, 3), 3);
  layoutSection("Global", rowA, 3);

  layoutSection("Envelope 1", take(rowB, 4), 4);
  layoutSection("Envelope 2", take(rowB, 4), 4);
  layoutSection("LFOs", rowB, 5);

  layoutSection("Arpeggiator", take(rowC, 3), 3);
  auto tg = take(rowC, 5);
  layoutSection("Trance Gate", tg, 5);
  if (gate) gate->setBounds(tg.reduced(16, 0).withTrimmedTop(32 + ch + 18).withHeight(40));
  layoutSection("Delay", take(rowC, 3), 3);
  layoutSection("Reverb", rowC, 3);
}
