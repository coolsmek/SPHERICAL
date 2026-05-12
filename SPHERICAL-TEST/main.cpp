#include <iostream>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <array>
#include <fstream>
#include <nuklear.h>
#include <string>
#include <vector>
#include <SDL3/SDL.h>

#include "SPHERICAL.h"

#ifndef SPHERICAL_APP_VERSION
#define SPHERICAL_APP_VERSION "dev"
#endif

namespace {
    std::atomic_bool g_shouldQuit = false;
    
    // App-owned UI state
    struct AppUIState {
        float colorR = 0.5f;
        float colorG = 0.5f;
        float colorB = 0.5f;
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

    AppUIState g_appUI;

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
        return g_appUI.buttonStyleMode == static_cast<int>(Spherical::ButtonStyle::Embossed)
            ? Spherical::ButtonStyle::Embossed
            : Spherical::ButtonStyle::Flat;
    }

    std::string ResolveAppFontPath() {
        std::vector<std::string> fontCandidates = {
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
        /*
        std::string ResolveAppFontPath() {
            std::vector<std::string> fontCandidates = {
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
            */

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
        1280,
        1300,
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
        
        ui.push_panel_body_color({1.0f, 1.0f, 1.0f, 1.0f});
        ui.push_panel_title_bar_color({0.45f, 0.45f, 0.45f, 1.0f});
        ui.push_panel_border_color({0.0f, 0.0f, 0.0f, 1.0f});
        ui.push_panel_title_text_color({1.0f, 1.0f, 1.0f, 1.0f});
        ui.push_text_color({0.05f, 0.05f, 0.05f, 1.0f});
        ui.push_radio_button_text_color({0.05f, 0.05f, 0.05f, 1.0f});
        ui.push_button_background_color(g_appUI.buttonBackgroundColor);
        ui.push_button_hover_background_color(g_appUI.buttonHoverBackgroundColor);
        ui.push_button_clicked_background_color(g_appUI.buttonClickedBackgroundColor);
        ui.push_button_height(g_appUI.buttonHeight);
        ui.push_button_width(g_appUI.buttonWidth);
        ui.push_button_corner_radius(g_appUI.buttonCornerRadius);
        ui.push_button_border_thickness(g_appUI.buttonBorderThickness);
        ui.push_button_padding(g_appUI.buttonPadding);
        ui.push_button_style(GetSelectedButtonStyle());
        ui.push_button_highlight_color(g_appUI.buttonHighlightColor);
        ui.push_button_shadow_color(g_appUI.buttonShadowColor);
        ui.push_button_border_color({0.0f, 0.0f, 0.0f, 1.0f});
        ui.push_button_text_color({0.0f, 0.0f, 0.0f, 1.0f});
        ui.push_text_input_background_color({1.0f, 1.0f, 1.0f, 1.0f});
        ui.push_text_input_text_color({0.0f, 0.0f, 0.0f, 1.0f});
                
        // New Panel Start
        if (ui.begin_panel("Control Panel", 20, 20, 650, 1250)) {
            
            ui.push_font(Spherical::FontStyle::Title);
            ui.label("SPHERICAL Interactive Control Panel");
            ui.pop_font();
            
            ui.label("This uses the regular font style.");
            ui.spacing();
            if (ui.button(g_appUI.newButtonToggled ? "New Button (ON)" : "New Button (OFF)")) {
                g_appUI.newButtonToggled = !g_appUI.newButtonToggled;
            }
            
            {
                char status_label[64];
                snprintf(status_label, sizeof(status_label), "New Button State: %s",
                    g_appUI.newButtonToggled ? "ON" : "OFF");
                ui.label(status_label);
            }
            
            ui.spacing();
            
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
            
            // FPS display
            {
                char fps_label[64];
                snprintf(fps_label, sizeof(fps_label), "FPS: %.1f", g_appUI.GetFPS());
                ui.label(fps_label);
            }

            // Mouse position
            {
                float mouseX = 0.0f, mouseY = 0.0f;
                SDL_GetMouseState(&mouseX, &mouseY);
                char mouse_label[128];
                snprintf(mouse_label, sizeof(mouse_label), "Mouse: (%d, %d)",
                         static_cast<int>(mouseX), static_cast<int>(mouseY));
                ui.label(mouse_label);
            }

            // Window size
            {
                char window_label[64];
                snprintf(window_label, sizeof(window_label), "Window: %ux%u",
                         ui.get_framebuffer_width(), ui.get_framebuffer_height());
                ui.label(window_label);
            }

            ui.spacing();

            // Color sliders
            ui.slider_float("R:", &g_appUI.colorR, 0.0f, 1.0f, 0.01f);
            ui.slider_float("G:", &g_appUI.colorG, 0.0f, 1.0f, 0.01f);
            ui.slider_float("B:", &g_appUI.colorB, 0.0f, 1.0f, 0.01f);

            {
                char color_label[64];
                snprintf(color_label, sizeof(color_label), "Color: (%.2f, %.2f, %.2f)",
                         g_appUI.colorR, g_appUI.colorG, g_appUI.colorB);
                ui.label(color_label);
            }

            ui.spacing();

            ui.push_font(Spherical::FontStyle::Title);
            ui.label("Button Theme Colors");
            ui.pop_font();

            ui.label("Button Style");
            ui.radio_button("Flat", &g_appUI.buttonStyleMode, static_cast<int>(Spherical::ButtonStyle::Flat));
            ui.radio_button("Embossed", &g_appUI.buttonStyleMode, static_cast<int>(Spherical::ButtonStyle::Embossed));
            ui.spacing();

            ui.slider_float("Button Height", &g_appUI.buttonHeight, 20.0f, 64.0f, 1.0f);
            ui.slider_float("Button Width", &g_appUI.buttonWidth, 0.0f, 360.0f, 1.0f);
            ui.slider_float("Corner Radius", &g_appUI.buttonCornerRadius, 0.0f, 16.0f, 0.5f);
            ui.slider_float("Border Thickness", &g_appUI.buttonBorderThickness, 0.0f, 6.0f, 0.5f);
            ui.slider_float("Padding X", &g_appUI.buttonPadding.x, 0.0f, 24.0f, 1.0f);
            ui.slider_float("Padding Y", &g_appUI.buttonPadding.y, 0.0f, 24.0f, 1.0f);

            {
                char widthLabel[96];
                if (g_appUI.buttonWidth <= 0.0f) {
                    snprintf(widthLabel, sizeof(widthLabel), "Button Width: Full row width");
                } else {
                    snprintf(widthLabel, sizeof(widthLabel), "Button Width: %.0f px (centered)", g_appUI.buttonWidth);
                }
                ui.label(widthLabel);
            }
            ui.spacing();

            EditColorRgb(ui, "Button Normal Background", g_appUI.buttonBackgroundColor);
            ui.spacing();
            EditColorRgb(ui, "Button Hover Background", g_appUI.buttonHoverBackgroundColor);
            ui.spacing();
            EditColorRgb(ui, "Button Clicked Background", g_appUI.buttonClickedBackgroundColor);
            ui.spacing();
            EditColorRgb(ui, "Button Bevel Highlight", g_appUI.buttonHighlightColor);
            ui.spacing();
            EditColorRgb(ui, "Button Bevel Shadow", g_appUI.buttonShadowColor);

            ui.spacing();

            // Click counter button
            if (ui.button("Click Me")) {
                g_appUI.clickCounter++;
            }

            {
                char click_label[64];
                snprintf(click_label, sizeof(click_label), "Clicks: %d", g_appUI.clickCounter);
                ui.label(click_label);
            }

            ui.spacing();

            // Text input
            ui.text_input("Text Input:", g_appUI.textInput, sizeof(g_appUI.textInput));

            {
                char text_label[256];
                snprintf(text_label, sizeof(text_label), "Captured: %s", g_appUI.textInput);
                ui.label(text_label);
            }

            ui.spacing();

            // Load project button
            if (ui.button(!g_appUI.isLoadingProject ? "Load Project" : "Loading...")) {
                if (!g_appUI.isLoadingProject) {
                    g_appUI.isLoadingProject = true;
                    g_appUI.loadStartTime = std::chrono::high_resolution_clock::now();
                    // TODO: Trigger async task via Spherical::TaskRunner in future
                }
            }

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
            
            ui.spacing();
                        
        }
        ui.end_panel();  // Always call end_panel() — required even if begin_panel() returned false
        
        ui.pop_text_input_text_color();
        ui.pop_text_input_background_color();
        ui.pop_button_text_color();
        ui.pop_button_border_color();
        ui.pop_button_shadow_color();
        ui.pop_button_highlight_color();
        ui.pop_button_style();
        ui.pop_button_padding();
        ui.pop_button_border_thickness();
        ui.pop_button_corner_radius();
        ui.pop_button_width();
        ui.pop_button_height();
        ui.pop_button_clicked_background_color();
        ui.pop_button_hover_background_color();
        ui.pop_button_background_color();
        ui.pop_radio_button_text_color();
        ui.pop_text_color();
        ui.pop_panel_title_text_color();
        ui.pop_panel_border_color();
        ui.pop_panel_title_bar_color();
        ui.pop_panel_body_color();
    });

    const std::string fontPath = ResolveAppFontPath();
    if (fontPath.empty()) {
        std::cerr << "Warning: Arimo-Regular.ttf not found; SDK fallback font discovery will be used." << std::endl;
    } else {
        std::cout << "Using UI font: " << fontPath << std::endl;
    }

    Spherical::SphericalInitInfo initInfo{};
    initInfo.window = window;
    initInfo.fontPath = fontPath.empty() ? nullptr : fontPath.c_str();
    initInfo.preferImmediatePresent = false;
    initInfo.framesInFlight = 1;
    initInfo.enableValidation = false;
    initInfo.fontRenderMode = Spherical::FontRenderMode::MSDF;
    initInfo.manualDpiScale = 0.0f; 

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
