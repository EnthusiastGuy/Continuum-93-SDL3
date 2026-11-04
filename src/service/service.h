// Service mode (F1): the built-in debugger/monitor drawn over the host window at host resolution.
// The only header main.cpp needs. Ports ServiceModule/Service.cs, ServiceState.cs, Controls/ServiceInput.cs,
// ServiceGraphics.cs, ServiceLayoutManager.cs and the service parts of Continuum.cs / InputMouse.cs.
#pragma once
#include <cstdint>
#include <filesystem>

union SDL_Event;
struct SDL_Window;

namespace c93 {
struct Machine;
class Video;
}

namespace c93::service {

void init(SDL_Window* window, Video& video, Machine& m, const std::filesystem::path& data);
void event(const SDL_Event& e);   // every SDL event (keys: F1/F5/F8/F9/F10/Enter, wheel, resizes)
void update();                    // once per frame, after the events
bool view();                      // the service view is on screen: draw() instead of Video::present
void draw();
// The guest's keyboard: false while service mode is open (key downs and typed text stay in the UI).
bool guestKeys();
// The guest's mouse while the service view is up (px, py: window pixels). -1: not handled, use the normal
// mapping; 0: keep the guest's last position and report no buttons; 1: x, y are the machine position.
int guestMouse(float px, float py, uint16_t& x, uint16_t& y);
void shutdown();                  // before the renderer goes

} // namespace c93::service
