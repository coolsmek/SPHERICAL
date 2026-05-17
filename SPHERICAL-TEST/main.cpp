#include <iostream>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <array>
#include <fstream>
#include <string>
#include <vector>
#include <SDL3/SDL.h>

#include "SPHERICAL.h"
#include "ui_style_scope.h"
#include "ui_theme.h"

#ifndef SPHERICAL_APP_VERSION
#define SPHERICAL_APP_VERSION "dev"
#endif

namespace {
    constexpr const char* kWorkspaceContainerTitle = "Workspace Container Demo";

    std::atomic_bool g_shouldQuit = false;
    
    // App-owned UI state
    struct AppUIState {
        float colorR = 0.5f;
        float colorG = 0.5f;
        float colorB = 0.5f;
        int clickCounter = 0;
        char textInput[128] = "";
        bool isLoadingProject = false;
        int projectsLoaded = 0;
        std::chrono::high_resolution_clock::time_point loadStartTime;
        std::array<double, 60> frameTimes = {};
        size_t frameIndex = 0;
        std::chrono::high_resolution_clock::time_point lastFrameTime;
        double frameTime = 0.0;
        // example radio button
        int selectedMode = 0;
        
        //New Button - toggleable
        bool newButtonToggled = false;
        
        //use alternate font
        bool altFont = false;
        
        double GetFPS() const {
            double totalMs = 0;
            for (double t : frameTimes) {
                totalMs += t;
            }
            double avgMs = totalMs / frameTimes.size();
            return avgMs > 0 ? 1000.0 / avgMs : 0.0;
        }

        void UpdateFrameTime() {
            const auto now = std::chrono::high_resolution_clock::now();
            if (lastFrameTime.time_since_epoch().count() == 0) {
                lastFrameTime = now;
                return;
            }

            frameTime = std::chrono::duration<double, std::milli>(now - lastFrameTime).count();
            lastFrameTime = now;
            frameTimes[frameIndex] = frameTime;
            frameIndex = (frameIndex + 1) % frameTimes.size();
        }
    };

    struct AppStyleOverrides {
        float buttonHeight = 30.0f;
        float buttonWidth = 200.0f;
        float buttonCornerRadius = 0.0f;
        float buttonBorderThickness = 1.0f;
        Spherical::UIColor buttonBackgroundColor{0.92f, 0.92f, 0.92f, 1.0f};
        Spherical::UIColor buttonHoverBackgroundColor{0.82f, 0.82f, 0.82f, 1.0f};
        Spherical::UIColor buttonClickedBackgroundColor{0.72f, 0.72f, 0.72f, 1.0f};
        Spherical::UIVec2 buttonPadding{12.0f, 7.0f};
        Spherical::UIColor buttonHighlightColor{1.0f, 1.0f, 1.0f, 1.0f};
        Spherical::UIColor buttonShadowColor{0.35f, 0.35f, 0.35f, 1.0f};
        int buttonStyleMode = static_cast<int>(Spherical::ButtonStyle::Embossed);
    };

    AppUIState g_appUI;
    AppStyleOverrides g_styleOverrides;
    AppTheme::ThemePresetId g_selectedThemePreset = AppTheme::ThemePresetId::ClassicLight;
    AppTheme::UIStyle g_activeStyle;

    void EditColorRgb(Spherical::UIPainter& ui, const char* title, Spherical::UIColor& color) {
        ui.label(title);
        ui.slider_float("R:", &color.r, 0.0f, 1.0f, 0.01f);
        ui.slider_float("G:", &color.g, 0.0f, 1.0f, 0.01f);
        ui.slider_float("B:", &color.b, 0.0f, 1.0f, 0.01f);

        char colorLabel[96];
        snprintf(colorLabel, sizeof(colorLabel), "RGB: (%.2f, %.2f, %.2f)", color.r, color.g, color.b);
        ui.label(colorLabel);
    }

    Spherical::ButtonStyle GetSelectedButtonStyle() {
        return g_styleOverrides.buttonStyleMode == static_cast<int>(Spherical::ButtonStyle::Embossed)
            ? Spherical::ButtonStyle::Embossed
            : Spherical::ButtonStyle::Flat;
    }

    void RebuildActiveStyle() {
        g_activeStyle = AppTheme::GetThemePreset(g_selectedThemePreset).style;
        g_activeStyle.buttonBackgroundColor = g_styleOverrides.buttonBackgroundColor;
        g_activeStyle.buttonHoverBackgroundColor = g_styleOverrides.buttonHoverBackgroundColor;
        g_activeStyle.buttonClickedBackgroundColor = g_styleOverrides.buttonClickedBackgroundColor;
        g_activeStyle.buttonHeight = g_styleOverrides.buttonHeight;
        g_activeStyle.buttonWidth = g_styleOverrides.buttonWidth;
        g_activeStyle.buttonCornerRadius = g_styleOverrides.buttonCornerRadius;
        g_activeStyle.buttonBorderThickness = g_styleOverrides.buttonBorderThickness;
        g_activeStyle.buttonPadding = g_styleOverrides.buttonPadding;
        g_activeStyle.buttonStyle = GetSelectedButtonStyle();
        g_activeStyle.buttonHighlightColor = g_styleOverrides.buttonHighlightColor;
        g_activeStyle.buttonShadowColor = g_styleOverrides.buttonShadowColor;
    }
//bool altFont = false;
    
    std::string ResolveAppFontPath()
    {
        std::vector<std::string> fontCandidates;
        
        if (!g_appUI.altFont) {
            fontCandidates= {
            "SPHERICAL-TEST/fonts/Arimo/Arimo-Regular.ttf",
            "fonts/Arimo/Arimo-Regular.ttf"
        };

        if (const char* basePathRaw = SDL_GetBasePath()) {
            const std::string basePath(basePathRaw);
            fontCandidates.insert(fontCandidates.begin(), {
                basePath + "fonts/Arimo/Arimo-Regular.ttf",
                basePath + "../fonts/Arimo/Arimo-Regular.ttf",
                basePath + "../../../SPHERICAL-TEST/fonts/Arimo/Arimo-Regular.ttf"
            });
        }
    } else {
        fontCandidates= {
                    "SPHERICAL-TEST/fonts/Inconsolata/Inconsolata_SemiExpanded-Light.ttf",
                    "fonts/Inconsolata/Inconsolata_SemiExpanded-Light.ttf"
                };

                if (const char* basePathRaw = SDL_GetBasePath()) {
                    const std::string basePath(basePathRaw);
                    fontCandidates.insert(fontCandidates.begin(), {
                        basePath + "fonts/Inconsolata/Inconsolata_SemiExpanded-Light.ttf",
                        basePath + "../fonts/Inconsolata/Inconsolata_SemiExpanded-Light.ttf",
                        basePath + "../../../SPHERICAL-TEST/fonts/Inconsolata/Inconsolata_SemiExpanded-Light.ttf"
                    });
                }
            }
        for (const std::string& candidate : fontCandidates) {
            std::ifstream file(candidate.c_str(), std::ios::binary);
            if (file.good()) {
                return candidate;
            }
        }

        return {};
    }
    

    bool SDLCALL EventWatch(void* userdata, SDL_Event* event) {
        (void)userdata;
        if (event == nullptr) {
            return true;
        }
        if (event->type == SDL_EVENT_QUIT || event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            g_shouldQuit.store(true);
        }
        return true;
    }
}

int main(int /*argc*/, char* /*argv*/[]) {
    std::cout << "=== SPHERICAL Interactive Control Panel Demo ===" << std::endl;

    SDL_SetHint("SDL_VIDEODRIVER", "windows");
    
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "Failed to initialize SDL3: " << SDL_GetError() << std::endl;
        return 1;
    }

    SDL_AddEventWatch(EventWatch, nullptr);

    SDL_Window* window = SDL_CreateWindow(
        "SPHERICAL Control Panel",
        1920,
        1080,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE
    );

    if (window == nullptr) {
        std::cerr << "Failed to create SDL window: " << SDL_GetError() << std::endl;
        SDL_RemoveEventWatch(EventWatch, nullptr);
        SDL_Quit();
        return 1;
    }

    // Register the UI build callback BEFORE initialization
    Spherical::RegisterUI([](Spherical::UIPainter& ui) {
        RebuildActiveStyle();
        AppTheme::ScopedUIStyle panelScope(ui, g_activeStyle);
                
        // New Panel Start
        if (ui.begin_panel("Control Panel", 20, 20, 500, 600)) {
            const Spherical::UIRect panelBounds = ui.get_current_panel_bounds();
            const Spherical::UIRect panelContentBounds = ui.get_current_panel_content_bounds();
            const float responsiveButtonWidth = std::clamp(panelContentBounds.w * 0.42f, 180.0f, 360.0f);
            
            ui.push_font(Spherical::FontStyle::Title);
            ui.label("SPHERICAL Interactive Control Panel");
            ui.pop_font();
            ui.spacing();

            if (ui.begin_panel_subsection("Overview")) {
                ui.label("This uses the regular font style.");
                ui.spacing();
                ui.push_button_width(responsiveButtonWidth);
                if (ui.button(g_appUI.newButtonToggled ? "New Button (ON)" : "New Button (OFF)")) {
                    g_appUI.newButtonToggled = !g_appUI.newButtonToggled;
                }
                ui.pop_button_width();

                char status_label[64];
                snprintf(status_label, sizeof(status_label), "New Button State: %s",
                    g_appUI.newButtonToggled ? "ON" : "OFF");
                ui.label(status_label);
            }
            ui.end_panel_subsection();

            if (ui.begin_panel_subsection("Modes & Metrics")) {
                if (ui.radio_button("Mode A", &g_appUI.selectedMode, 0)) {
                    // changed to Mode A
                }
                if (ui.radio_button("Mode B", &g_appUI.selectedMode, 1)) {
                    // changed to Mode B
                }
                if (ui.radio_button("Mode C", &g_appUI.selectedMode, 2)) {
                    // changed to Mode C
                }

                ui.spacing();

                if (ui.begin_panel_subsection("Telemetry")) {
                    {
                        char fps_label[64];
                        snprintf(fps_label, sizeof(fps_label), "FPS: %.1f", g_appUI.GetFPS());
                        ui.label(fps_label);
                    }

                    {
                        float mouseX = 0.0f, mouseY = 0.0f;
                        SDL_GetMouseState(&mouseX, &mouseY);
                        char mouse_label[128];
                        snprintf(mouse_label, sizeof(mouse_label), "Mouse: (%d, %d)",
                                 static_cast<int>(mouseX), static_cast<int>(mouseY));
                        ui.label(mouse_label);
                    }

                    {
                        char window_label[64];
                        snprintf(window_label, sizeof(window_label), "Window: %ux%u",
                                 ui.get_framebuffer_width(), ui.get_framebuffer_height());
                        ui.label(window_label);
                    }

                    {
                        char panel_label[96];
                        snprintf(panel_label, sizeof(panel_label), "Panel: %.0fx%.0f", panelBounds.w, panelBounds.h);
                        ui.label(panel_label);
                    }
                }
                ui.end_panel_subsection();
            }
            ui.end_panel_subsection();

            if (ui.begin_panel_subsection("Color Controls")) {
                ui.slider_float("R:", &g_appUI.colorR, 0.0f, 1.0f, 0.01f);
                ui.slider_float("G:", &g_appUI.colorG, 0.0f, 1.0f, 0.01f);
                ui.slider_float("B:", &g_appUI.colorB, 0.0f, 1.0f, 0.01f);

                char color_label[64];
                snprintf(color_label, sizeof(color_label), "Color: (%.2f, %.2f, %.2f)",
                         g_appUI.colorR, g_appUI.colorG, g_appUI.colorB);
                ui.label(color_label);
            }
            ui.end_panel_subsection();

            if (ui.begin_panel_subsection("Theme Preset")) {
                ui.label("Choose a base theme preset.");

                int selectedTheme = static_cast<int>(g_selectedThemePreset);
                ui.radio_button("Classic Light", &selectedTheme, static_cast<int>(AppTheme::ThemePresetId::ClassicLight));
                ui.radio_button("Midnight Dark", &selectedTheme, static_cast<int>(AppTheme::ThemePresetId::MidnightDark));
                ui.radio_button("Deep Ocean Blue", &selectedTheme, static_cast<int>(AppTheme::ThemePresetId::DeepOceanBlue));
                ui.radio_button("Neo Green", &selectedTheme, static_cast<int>(AppTheme::ThemePresetId::NeoGreen));

                switch (static_cast<AppTheme::ThemePresetId>(selectedTheme)) {
                    case AppTheme::ThemePresetId::ClassicLight:
                        g_selectedThemePreset = AppTheme::ThemePresetId::ClassicLight;
                        break;
                    case AppTheme::ThemePresetId::MidnightDark:
                        g_selectedThemePreset = AppTheme::ThemePresetId::MidnightDark;
                        break;
                    case AppTheme::ThemePresetId::DeepOceanBlue:
                        g_selectedThemePreset = AppTheme::ThemePresetId::DeepOceanBlue;
                        break;
                    case AppTheme::ThemePresetId::NeoGreen:
                        g_selectedThemePreset = AppTheme::ThemePresetId::NeoGreen;
                        break;
                }
            }
            ui.end_panel_subsection();

            if (ui.begin_panel_subsection("Button Theme Colors")) {
                // Keep theme tuning scoped so section-level overrides can be added safely later.
                AppTheme::UIStyle sectionStyle = g_activeStyle;
                AppTheme::ScopedUIStyle sectionScope(ui, sectionStyle);

                if (ui.begin_panel_subsection("Geometry")) {
                    ui.label("Button Style");
                    ui.radio_button("Flat", &g_styleOverrides.buttonStyleMode, static_cast<int>(Spherical::ButtonStyle::Flat));
                    ui.radio_button("Embossed", &g_styleOverrides.buttonStyleMode, static_cast<int>(Spherical::ButtonStyle::Embossed));
                    ui.spacing();

                    ui.slider_float("Button Height", &g_styleOverrides.buttonHeight, 20.0f, 64.0f, 1.0f);
                    ui.slider_float("Button Width", &g_styleOverrides.buttonWidth, 0.0f, 360.0f, 1.0f);
                    ui.slider_float("Corner Radius", &g_styleOverrides.buttonCornerRadius, 0.0f, 16.0f, 0.5f);
                    ui.slider_float("Border Thickness", &g_styleOverrides.buttonBorderThickness, 0.0f, 6.0f, 0.5f);
                    ui.slider_float("Padding X", &g_styleOverrides.buttonPadding.x, 0.0f, 24.0f, 1.0f);
                    ui.slider_float("Padding Y", &g_styleOverrides.buttonPadding.y, 0.0f, 24.0f, 1.0f);

                    char widthLabel[96];
                    if (g_styleOverrides.buttonWidth <= 0.0f) {
                        snprintf(widthLabel, sizeof(widthLabel), "Button Width: Full row width");
                    } else {
                        snprintf(widthLabel, sizeof(widthLabel), "Button Width: %.0f px (centered)", g_styleOverrides.buttonWidth);
                    }
                    ui.label(widthLabel);
                }
                ui.end_panel_subsection();

                if (ui.begin_panel_subsection("Palette")) {
                    EditColorRgb(ui, "Button Normal Background", g_styleOverrides.buttonBackgroundColor);
                    ui.spacing();
                    EditColorRgb(ui, "Button Hover Background", g_styleOverrides.buttonHoverBackgroundColor);
                    ui.spacing();
                    EditColorRgb(ui, "Button Clicked Background", g_styleOverrides.buttonClickedBackgroundColor);
                    ui.spacing();
                    EditColorRgb(ui, "Button Bevel Highlight", g_styleOverrides.buttonHighlightColor);
                    ui.spacing();
                    EditColorRgb(ui, "Button Bevel Shadow", g_styleOverrides.buttonShadowColor);
                }
                ui.end_panel_subsection();
            }
            ui.end_panel_subsection();

            if (ui.begin_panel_subsection("Actions")) {
                ui.push_button_width(responsiveButtonWidth);
                if (ui.button("Click Me")) {
                    g_appUI.clickCounter++;
                }
                ui.pop_button_width();

                {
                    char click_label[64];
                    snprintf(click_label, sizeof(click_label), "Clicks: %d", g_appUI.clickCounter);
                    ui.label(click_label);
                }

                ui.spacing();

                ui.text_input("Text Input:", g_appUI.textInput, sizeof(g_appUI.textInput));

                {
                    char text_label[256];
                    snprintf(text_label, sizeof(text_label), "Captured: %s", g_appUI.textInput);
                    ui.label(text_label);
                }

                ui.spacing();

                ui.push_button_width(responsiveButtonWidth);
                if (ui.button(!g_appUI.isLoadingProject ? "Load Project" : "Loading...")) {
                    if (!g_appUI.isLoadingProject) {
                        g_appUI.isLoadingProject = true;
                        g_appUI.loadStartTime = std::chrono::high_resolution_clock::now();
                        // TODO: Trigger async task via Spherical::TaskRunner in future
                    }
                }
                ui.pop_button_width();

                {
                    char load_label[64];
                    if (g_appUI.isLoadingProject) {
                        const auto elapsed = std::chrono::duration<double>(
                            std::chrono::high_resolution_clock::now() - g_appUI.loadStartTime
                        ).count();
                        snprintf(load_label, sizeof(load_label), "Loading... (%.1fs)", elapsed);
                    } else {
                        snprintf(load_label, sizeof(load_label), "Projects loaded: %d", g_appUI.projectsLoaded);
                    }
                    ui.label(load_label);
                }
            }
            ui.end_panel_subsection();

            ui.spacing();
                        
        }
        ui.end_panel();  // Always call end_panel() — required even if begin_panel() returned false

        // Demo: Workspace Container (manages layout only; panels are rendered separately)
        if (ui.begin_workspace_container(kWorkspaceContainerTitle, 540, 20, 800, 600)) {
            // Container displays the docking visualization, info, etc.
            // Actual panels are rendered below, OUTSIDE this block
        }
        ui.end_workspace_container();

        // These panels can be dragged into the workspace container
        // They're rendered OUTSIDE the container block (not nested inside it)
        if (ui.begin_panel("Panel A", 20, 640, 250, 300)) {
            ui.label("Panel A");
            ui.label("Drag this panel into the");
            ui.label("Workspace Container!");
            ui.spacing();
            if (ui.button("Button in Panel A")) {
                std::cout << "Panel A button clicked" << std::endl;
            }
        }
        ui.end_panel();

        if (ui.begin_panel("Panel B", 290, 640, 250, 300)) {
            ui.label("Panel B");
            ui.label("This is another panel");
            ui.label("Try docking multiple");
            ui.label("panels in the container");
            ui.spacing();
            if (ui.button("Button in Panel B")) {
                std::cout << "Panel B button clicked" << std::endl;
            }
        }
        ui.end_panel();

        if (ui.begin_panel("Panel C", 560, 640, 250, 300)) {
            ui.label("Panel C");
            ui.label("Panels will snap to");
            ui.label("grid positions based on");
            ui.label("your drop zone");
            ui.spacing();
            if (ui.button("Button in Panel C")) {
                std::cout << "Panel C button clicked" << std::endl;
            }
        }
        ui.end_panel();
        
        if (ui.begin_panel("Panel D", 830, 640, 250, 300)) {
            ui.label("Panel D");
            ui.label("Panels will snap to");
            ui.label("grid positions based on");
            ui.label("your drop zone");
            ui.spacing();
            if (ui.button("Button in Panel D")) {
                std::cout << "Panel D button clicked" << std::endl;
            }
        }
        ui.end_panel();
        
        if (ui.begin_panel("Panel E", 1100, 640, 250, 300)) {
            ui.label("Panel E");
            ui.label("Panels will snap to");
            ui.label("grid positions based on");
            ui.label("your drop zone");
            ui.spacing();
            if (ui.button("Button in Panel E")) {
                std::cout << "Panel E button clicked" << std::endl;
            }
        }
        ui.end_panel();

        // Display workspace info in a separate debug panel
        if (ui.begin_panel("Workspace Info", 20, 960, 550, 250)) {
            int panelCount = ui.get_workspace_panel_count(kWorkspaceContainerTitle);
            Spherical::DockLayout layout = ui.get_workspace_dock_layout(kWorkspaceContainerTitle);
            const char* layoutStr = (layout == Spherical::DockLayout::SideBySide) ? "SideBySide" : "TopBottom";
            
            char workspace_label[128];
            snprintf(workspace_label, sizeof(workspace_label), 
                "Docked Panels: %d | Layout: %s", panelCount, layoutStr);
            ui.label(workspace_label);
            
            ui.label("");
            ui.label("Drag panels into the workspace container above.");
            ui.label("Click the red circle button on a docked panel to undock it.");
            ui.label("The remaining docked panels expand automatically.");
        }
        ui.end_panel();
    });

    const std::string fontPath = ResolveAppFontPath();
    if (fontPath.empty()) {
        std::cerr << "Warning: Arimo-Regular.ttf not found; SDK fallback font discovery will be used." << std::endl;
    } else {
        std::cout << "Using UI font: " << fontPath << std::endl;
    }

    Spherical::SphericalInitInfo initInfo{
        window,
        fontPath.empty() ? nullptr : fontPath.c_str(),
        Spherical::FontRenderMode::MSDF,
        0.0f,
        false,
        1,
        false
    };

    if (!Spherical::Init(initInfo)) {
        std::cerr << "Failed to initialize SPHERICAL SDK!" << std::endl;
        SDL_DestroyWindow(window);
        SDL_RemoveEventWatch(EventWatch, nullptr);
        SDL_Quit();
        return 1;
    }

    constexpr double kTargetFps = 200.0;
    constexpr double kTargetFrameMs = 1000.0 / kTargetFps;

    while (!g_shouldQuit.load()) {
        const auto frameStart = std::chrono::high_resolution_clock::now();

        g_appUI.UpdateFrameTime();
        Spherical::NewFrame();
        Spherical::Render();

        const auto frameEnd = std::chrono::high_resolution_clock::now();
        const double frameMs = std::chrono::duration<double, std::milli>(frameEnd - frameStart).count();
        if (frameMs < kTargetFrameMs) {
            SDL_Delay(static_cast<Uint32>(kTargetFrameMs - frameMs));
        }
    }

    Spherical::Shutdown();
    SDL_DestroyWindow(window);
    SDL_RemoveEventWatch(EventWatch, nullptr);
    SDL_Quit();

    std::cout << "✓ Application shutdown cleanly" << std::endl;
    return 0;
}
