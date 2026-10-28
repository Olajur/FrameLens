// FrameLens — a small custom-drawn UI toolkit for the app windows (Direct2D, themed).
//
// A FormWindow hosts a list of controls laid out in DIPs (absolute rectangles). Controls read and
// write their values through callbacks, so a window never holds a copy of the settings; it just
// repaints. Keyboard: Tab / Shift+Tab move focus, Space / Enter activate, arrows adjust sliders
// and dropdowns, Esc closes an open list (or calls onEscape). Dropdown lists open in a popup that
// never takes activation from the window.
// SPDX-License-Identifier: MIT
#pragma once
#include "Draw.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace fl::ui
{
    class FormWindow;

    // What a control draws with, plus text helpers that apply the theme (fonts, capitals).
    struct Painter
    {
        ID2D1RenderTarget* rt = nullptr;
        ID2D1SolidColorBrush* brush = nullptr;
        Fonts* fonts = nullptr;
        const Theme* th = nullptr;
        std::wstring uiFont, displayFont, iconFont;
        double timeSec = 0.0;

        void Text(const std::wstring& s, const D2D1_RECT_F& r, float size, DWRITE_FONT_WEIGHT weight, uint32_t rgb,
                  DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING, bool display = false, bool wrap = false,
                  float alpha = 1.f, DWRITE_PARAGRAPH_ALIGNMENT para = DWRITE_PARAGRAPH_ALIGNMENT_CENTER) const;
        void Icon(const wchar_t* glyph, const D2D1_RECT_F& r, float size, uint32_t rgb, float alpha = 1.f) const;
        float TextWidth(const std::wstring& s, float size, DWRITE_FONT_WEIGHT weight, bool display = false) const;
        void FocusRing(const D2D1_RECT_F& r, float radius) const;
        std::wstring Caps(const std::wstring& s) const;   // capitals when the theme asks for them
    };

    class Control
    {
    public:
        virtual ~Control() = default;
        D2D1_RECT_F rect{};
        std::function<bool()> enabledFn;    // null = always enabled
        FormWindow* owner = nullptr;

        bool Enabled() const { return !enabledFn || enabledFn(); }
        virtual bool Focusable() const { return false; }
        virtual void Draw(const Painter& p, bool hot, bool pressed, bool focused) = 0;
        virtual void OnPress(D2D1_POINT_2F) {}
        virtual void OnDrag(D2D1_POINT_2F) {}
        virtual void OnClick(D2D1_POINT_2F) {}
        virtual bool OnKey(UINT) { return false; }
        // While true, every key (Tab, Esc and Alt combinations too) goes to OnKey.
        virtual bool CapturesKeys() const { return false; }
        virtual void OnBlur() {}            // focus moved elsewhere, or the window was deactivated
    };

    // Static or live text.
    class Label : public Control
    {
    public:
        enum class Role { Text, Dim, Faint, Accent, Heading, Title };
        std::function<std::wstring()> text;
        Role role = Role::Text;
        float size = 13.f;
        bool wrap = false;
        DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING;
        std::function<uint32_t()> color;    // optional override (0 = role colour)
        void Draw(const Painter& p, bool, bool, bool) override;
    };

    // A themed card with an optional title in its top-left corner.
    class Card : public Control
    {
    public:
        std::wstring title;
        void Draw(const Painter& p, bool, bool, bool) override;
    };

    // App header band: product name, a subtitle, and the style's decoration (glass, rain...).
    class Header : public Control
    {
    public:
        std::wstring title, subtitle;
        void Draw(const Painter& p, bool, bool, bool) override;
    };

    class Button : public Control
    {
    public:
        enum class Kind { Normal, Primary, Quiet, Icon };
        std::function<std::wstring()> text;   // for Icon: a Segoe MDL2 / Fluent glyph
        Kind kind = Kind::Normal;
        std::function<bool()> highlighted;    // drawn as Primary while true (the current choice in a row)
        std::wstring tooltip;
        std::function<void()> onClick;
        bool Focusable() const override { return true; }
        void Draw(const Painter& p, bool hot, bool pressed, bool focused) override;
        void OnClick(D2D1_POINT_2F) override;
        bool OnKey(UINT vk) override;
    };

    // On/off: a switch (Minimal 2026) or a checkbox, with its text and an optional faint note.
    class Toggle : public Control
    {
    public:
        std::wstring text;
        std::function<std::wstring()> note;
        std::function<bool()> get;
        std::function<void(bool)> set;
        bool Focusable() const override { return true; }
        void Draw(const Painter& p, bool hot, bool pressed, bool focused) override;
        void OnClick(D2D1_POINT_2F) override;
        bool OnKey(UINT vk) override;
    };

    class Slider : public Control
    {
    public:
        std::wstring label;
        float labelWidth = 72.f;
        int min = 0, max = 100, step = 1;
        std::function<int()> get;
        std::function<void(int)> set;
        std::function<std::wstring(int)> format;
        bool Focusable() const override { return true; }
        void Draw(const Painter& p, bool hot, bool pressed, bool focused) override;
        void OnPress(D2D1_POINT_2F pt) override { OnDrag(pt); }
        void OnDrag(D2D1_POINT_2F pt) override;
        bool OnKey(UINT vk) override;

    private:
        D2D1_RECT_F Track() const;
        void SetValue(int v);
    };

    // A field showing the current choice; opens a list of items.
    class Dropdown : public Control
    {
    public:
        std::wstring label;
        float labelWidth = 72.f;
        std::function<std::wstring()> display;                 // text in the closed field
        std::function<std::vector<std::wstring>()> items;      // asked each time the list opens
        std::function<int()> selected;                         // index in items(), -1 = none
        std::function<void(int)> onSelect;
        bool Focusable() const override { return true; }
        void Draw(const Painter& p, bool hot, bool pressed, bool focused) override;
        void OnClick(D2D1_POINT_2F) override;
        bool OnKey(UINT vk) override;
        D2D1_RECT_F Field() const;
    };

    // A colour choice: label, swatch (opens the Windows colour picker), "Style colour" or #RRGGBB,
    // an optional warning note, and a reset button while an own colour is set.
    class ColorPicker : public Control
    {
    public:
        std::wstring label;
        float labelWidth = 92.f;
        std::function<int()> get;               // own colour 0xRRGGBB, or -1 = the style's
        std::function<uint32_t()> effective;    // what the swatch shows
        std::function<void(int)> set;           // final choice (OK, reset)
        std::function<void(int)> preview;       // live while the colour picker is open; not saved
        std::function<std::wstring()> note;     // e.g. "low contrast"
        bool Focusable() const override { return true; }
        void Draw(const Painter& p, bool hot, bool pressed, bool focused) override;
        void OnClick(D2D1_POINT_2F pt) override;
        bool OnKey(UINT vk) override;

    private:
        static UINT_PTR CALLBACK PickerHook(HWND dlg, UINT msg, WPARAM w, LPARAM l);
        D2D1_RECT_F Swatch() const;
        D2D1_RECT_F ResetBox() const;
        void Pick();
    };

    // A keyboard shortcut. Click it (or Enter), then press the keys: Esc cancels, Backspace / Delete
    // clears it. Packed as modifiers << 16 | virtual key (modifiers as for RegisterHotKey); 0 = none.
    // A key needs Alt, Ctrl, Shift or Win with it, except F1-F24.
    class HotkeyBox : public Control
    {
    public:
        std::wstring label;
        float labelWidth = 92.f;
        std::function<uint32_t()> get;
        std::function<void(uint32_t)> set;
        std::function<void(bool)> recording;     // the app pauses its hotkeys while keys are recorded
        std::function<std::wstring()> note;      // e.g. "used by another app"
        bool Focusable() const override { return true; }
        void Draw(const Painter& p, bool hot, bool pressed, bool focused) override;
        void OnClick(D2D1_POINT_2F) override;
        bool OnKey(UINT vk) override;
        bool CapturesKeys() const override { return recording_; }
        void OnBlur() override { Stop(); }
        D2D1_RECT_F Field() const;

    private:
        void Start();
        void Stop();
        bool recording_ = false;
        bool needModifier_ = false;   // the last key pressed had no modifier
    };

    // "Alt + 1", "Ctrl + Shift + F9"; L"None" for 0.
    std::wstring HotkeyText(uint32_t packed);

    // "Label   value" row for status information; the value may wrap to a second line.
    class InfoRow : public Control
    {
    public:
        std::wstring label;
        float labelWidth = 120.f;
        std::function<std::wstring()> value;
        std::function<uint32_t()> color;    // 0 = text colour
        void Draw(const Painter& p, bool, bool, bool) override;
    };

    class PopupList;

    class FormWindow
    {
    public:
        struct Options
        {
            const wchar_t* className = L"FrameLensForm";
            const wchar_t* title = L"FrameLens";
            DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
            DWORD exStyle = 0;
            HWND owner = nullptr;
            float widthDip = 560.f, heightDip = 420.f;
            HICON iconBig = nullptr, iconSmall = nullptr;
        };

        FormWindow();
        ~FormWindow();
        FormWindow(const FormWindow&) = delete;
        FormWindow& operator=(const FormWindow&) = delete;

        bool Create(HINSTANCE inst, const Options& o);
        HWND Hwnd() const noexcept { return hwnd_; }
        const Theme& CurrentTheme() const noexcept { return *theme_; }
        void SetTheme(const Theme& t);
        void SetClientSizeDip(float w, float h);
        void Invalidate();

        template<class T> T& Add()
        {
            auto c = std::make_unique<T>();
            c->owner = this;
            T& ref = *c;
            controls_.push_back(std::move(c));
            return ref;
        }
        void ClearControls();

        void OpenList(Dropdown* d);
        void CloseList();

        // Draws the controls into a PNG instead of a window (layout previews; no HWND needed).
        HRESULT RenderToPng(const wchar_t* path, float widthDip, float heightDip, float dpi, double timeSec);

        // App hooks. `message` sees every message first; return true when handled.
        std::function<bool(UINT, WPARAM, LPARAM, LRESULT&)> message;
        std::function<void()> onEscape;
        std::function<void()> onDestroy;

        // Windows' app mode (Settings > Personalization > Colors).
        static bool SystemPrefersDark();

    private:
        static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l);
        LRESULT Handle(UINT m, WPARAM w, LPARAM l);
        void Paint();
        void PaintTo(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, float w, float h, double timeSec);
        bool EnsureTarget();
        void ApplyTitleBar();
        D2D1_POINT_2F ToDip(LPARAM l) const;
        int HitTest(D2D1_POINT_2F p) const;
        void MoveFocus(int dir);
        void SetFocusIndex(int i);
        float Dpi() const;

        HWND hwnd_ = nullptr;
        const Theme* theme_;
        std::vector<std::unique_ptr<Control>> controls_;
        int hot_ = -1, pressed_ = -1, focus_ = -1;
        bool keyboardFocus_ = false;
        bool swallowSysKeys_ = false;   // a recorded Alt combination: keep Alt's release from opening the system menu
        Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> target_;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
        Fonts fonts_;
        std::wstring uiFont_, displayFont_, iconFont_;
        std::unique_ptr<PopupList> list_;
        ULONGLONG start_ = 0;
        friend class PopupList;
    };
}
