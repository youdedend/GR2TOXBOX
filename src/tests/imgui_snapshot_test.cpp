// SPDX-License-Identifier: GPL-2.0-or-later
// Real ImGui objects + the production Capture implementation, without a GPU.
#include "imgui/imgui_texture.h"
#include "graphics/gr2port/backend.h"
#include "graphics/gr2port_d3d12/imgui_snapshot.h"
#include <SDL3/SDL_mouse.h>
#include <cassert>
#include <cstdlib>
#include <thread>
#include <atomic>
void assert_fail_debug_msg(const char*) { std::abort(); }
extern "C" void SDL_WarpMouseInWindow(SDL_Window*,float,float) {}
int main() {
    using namespace Gr2Port::ImGuiRelay;
    ImGui::Texture texture;
    unsigned char rgba[4]={1,2,3,255};
    KeepPixels(&texture,rgba,1,1);
    auto* context=ImGui::CreateContext();
    {
        ImDrawList list(nullptr);
        list.VtxBuffer.push_back({{4,5},{0,0},0xff123456});
        list.IdxBuffer.push_back(0);
        ImDrawCmd command;command.ElemCount=1;command.TexRef=ImTextureRef(&texture);
        command.ClipRect=ImVec4(1,2,30,40);list.CmdBuffer.push_back(command);
        ImDrawData data;data.DisplayPos=ImVec2(2,3);data.DisplaySize=ImVec2(100,50);
        data.FramebufferScale=ImVec2(2,2);data.CmdLists.push_back(&list);
        Capture(data);const auto old=Latest();
        assert(old&&old->commands.size()==1&&old->vertices.size()==1&&old->indices.size()==1);
        assert(old->commands[0].clip[2]==30&&old->position[0]==2&&old->size[1]==50);
        assert(old->framebuffer_scale[1]==2);
        // Overwrite/free all mutable producer objects and destroy/reuse the ID.
        list.VtxBuffer[0].pos.x=999;list.IdxBuffer[0]=99;list.CmdBuffer[0].ClipRect.x=500;
        rgba[0]=200;Forget(&texture);KeepPixels(&texture,rgba,1,1);
        assert(old->vertices[0].position[0]==4&&old->indices[0]==0);
        assert(old->commands[0].texture->rgba[0]==1&&old->commands[0].clip[0]==1);
        assert(old->commands[0].texture->rgba.size()==4);
        std::atomic<bool> done{false};
        std::thread consumer([&] {
            while(!done) {const auto snapshot=Latest(); if(snapshot) assert(snapshot->vertices.size()==1);}
        });
        for(int i=0;i<100;++i) Capture(data);
        done=true;consumer.join();
    }
    ResetSnapshots();assert(!Latest());ImGui::DestroyContext(context);
}
