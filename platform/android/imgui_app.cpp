/* A Dear ImGui app: ImGui::* calls inside `frame`, in a BUI window (bindings/imgui). The kit's window
 * host is the activity's surface, so this is the code it would be on a desktop, plus one line:
 * a drag on the ImGui node is ImGui's (sliders, window moves), not a scroll.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "BUI_kit.h"
#include "bui_app.h"
#include "imgui.h"
#include "imgui_impl_bui.h"

static void frame(void *)
{
  static float value = 0.5f;
  static bool check = true;
  static char text[64] = "Dear ImGui";
  static bool demo = false;
  const ImGuiViewport *vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->WorkPos);
  ImGui::SetNextWindowSize(vp->WorkSize);
  ImGui::Begin("BUI + Dear ImGui", nullptr,
               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
  ImGui::TextUnformatted("The same ImGui code as on a desktop.");
  ImGui::SliderFloat("value", &value, 0.0f, 1.0f);
  ImGui::Checkbox("check", &check);
  ImGui::InputText("text", text, sizeof(text));
  if (ImGui::Button("Demo window")) {
    demo = !demo;
  }
  ImGui::SameLine();
  ImGui::Text("%.1f fps", ImGui::GetIO().Framerate);
  ImGui::End();
  if (demo) {
    ImGui::ShowDemoWindow(&demo);
  }
}

void bui_app_main(void)
{
  ImGui::CreateContext();
  ImGui_ImplBUI_App *app = ImGui_ImplBUI_CreateApp("Dear ImGui", 360, 720, frame, nullptr);
  BK_window_wm(ImGui_ImplBUI_AppWindow(app), "touch_drag", 1, 0);
  ImGui_ImplBUI_RunApp(app);
  ImGui_ImplBUI_DestroyApp(app);
  ImGui::DestroyContext();
}
