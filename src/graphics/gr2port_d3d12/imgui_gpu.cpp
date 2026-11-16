// SPDX-License-Identifier: GPL-2.0-or-later
#include "graphics/gr2port_d3d12/imgui_gpu.h"
#include "imgui_relay_vert.h"
#include "imgui_relay_frag.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include "uwp/blackbox_log.h"

namespace Gr2Port::D3D12 {
namespace {
bool Buffer(Device& d, u64 bytes, ComPtr<ID3D12Resource>& out) {
    if(!bytes || bytes>64ull*1024*1024) return false;
    D3D12_HEAP_PROPERTIES heap{}; heap.Type=D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC desc{}; desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width=bytes; desc.Height=1; desc.DepthOrArraySize=1; desc.MipLevels=1;
    desc.SampleDesc.Count=1; desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return SUCCEEDED(d.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(out.GetAddressOf())));
}
bool CopyCpu(ID3D12Resource* resource,const void* bytes,size_t count) {
    void* mapped=nullptr; D3D12_RANGE read{0,0};
    if(FAILED(resource->Map(0,&read,&mapped))) return false;
    std::memcpy(mapped,bytes,count); resource->Unmap(0,nullptr); return true;
}
}
bool ImGuiGpu::Create(Device& device,const RootSignature& root,BindlessHeap& heap) {
    device_=&device; root_=&root; heap_=&heap;
    GraphicsPipelineDesc desc; desc.vs=Shaders::imgui_relay_vert_span;
    desc.ps=Shaders::imgui_relay_frag_span; desc.color_formats={&device.swapchain_format,1};
    desc.alpha_blend=true; desc.depth_write=false;
    if(!CreateGraphicsPipeline(device.device.Get(),root,desc,pipeline_)) return false;
    for(auto& slot:vertex_slots_) { slot=heap.AllocateSrv(); if(!slot) return false; }
    D3D12_SAMPLER_DESC sampler{}; sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.MaxAnisotropy=1; sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_NEVER;
    sampler.MaxLOD=D3D12_FLOAT32_MAX;
    heap.WriteSampler({1,true},sampler);
    return true;
}
bool ImGuiGpu::Record(CommandList& list,u32 slot,const std::shared_ptr<const ImGuiRelay::Snapshot>& snapshot) {
    Frame& frame=frames_[slot]; frame={}; ++serial_;
    // The slot fence has completed. Anything unused for a full ring is retired.
    for(auto it=textures_.begin();it!=textures_.end();) {
        if(serial_-it->second.last_use>=FramesInFlight) {
            free_slots_.push_back(it->second.srv); it=textures_.erase(it);
        } else ++it;
    }
    if(!snapshot || snapshot->commands.empty() || snapshot->size[0]<=0 || snapshot->size[1]<=0) return true;
    const auto& s=*snapshot;
    const auto vb=s.vertices.size()*sizeof(ImGuiRelay::Vertex), ib=s.indices.size()*sizeof(u32);
    if(!Buffer(*device_,vb,frame.vertices)||!Buffer(*device_,ib,frame.indices) ||
       !CopyCpu(frame.vertices.Get(),s.vertices.data(),vb)||!CopyCpu(frame.indices.Get(),s.indices.data(),ib)) return false;
    heap_->WriteBuffer(vertex_slots_[slot],frame.vertices.Get(),0,u32(vb/4),0);
    list.SetRootSignature(*root_);
    ID3D12DescriptorHeap* heaps[]={heap_->SrvHeap(),heap_->SamplerHeap()};
    list.list->SetDescriptorHeaps(2,heaps); list.SetDescriptorTables(*heap_); list.SetPipeline(pipeline_);
    const u32 arenas[5]={vertex_slots_[slot].index,0,0,0,0};
    list.list->SetGraphicsRoot32BitConstants(0,5,arenas,0);
    D3D12_INDEX_BUFFER_VIEW indices{}; indices.BufferLocation=frame.indices->GetGPUVirtualAddress();
    indices.SizeInBytes=u32(ib); indices.Format=DXGI_FORMAT_R32_UINT;
    list.SetIndexBuffer(indices); list.list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    for(const auto& draw:s.commands) {
        if(!draw.texture || draw.first_index>s.indices.size() || draw.count>s.indices.size()-draw.first_index) continue;
        const auto& image=*draw.texture;
        auto found=textures_.find(image.revision);
        if(found==textures_.end()) {
            Texture tex;
            D3D12_RESOURCE_DESC desc{}; desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width=image.width; desc.Height=image.height; desc.DepthOrArraySize=1; desc.MipLevels=1;
            desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count=1;
            D3D12_HEAP_PROPERTIES heap{}; heap.Type=D3D12_HEAP_TYPE_DEFAULT;
            if(FAILED(device_->device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,
                D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(tex.resource.GetAddressOf())))) return false;
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{}; UINT64 bytes=0;
            device_->device->GetCopyableFootprints(&desc,0,1,0,&footprint,nullptr,nullptr,&bytes);
            ComPtr<ID3D12Resource> upload; if(!Buffer(*device_,bytes,upload)) return false;
            void* mapped=nullptr; D3D12_RANGE read{0,0};
            if(FAILED(upload->Map(0,&read,&mapped))) return false;
            for(u32 y=0;y<image.height;++y) std::memcpy(static_cast<u8*>(mapped)+footprint.Offset+size_t(y)*footprint.Footprint.RowPitch,
                image.rgba.data()+size_t(y)*image.width*4,size_t(image.width)*4);
            upload->Unmap(0,nullptr);
            D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource=tex.resource.Get(); dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource=upload.Get(); src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint=footprint; list.CopyBuffer(dst,src);
            list.Transition(tex.resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            frame.uploads.push_back(std::move(upload));
            if(free_slots_.empty()) tex.srv=heap_->AllocateSrv();
            else { tex.srv=free_slots_.back(); free_slots_.pop_back(); }
            if(!tex.srv) return false;
            D3D12_SHADER_RESOURCE_VIEW_DESC view{}; view.Format=desc.Format;
            view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D; view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            view.Texture2D.MipLevels=1; heap_->WriteTexture(tex.srv,tex.resource.Get(),view);
            found=textures_.emplace(image.revision,std::move(tex)).first;
        }
        found->second.last_use=serial_;
        const float sx=float(device_->swapchain_width)/s.size[0], sy=float(device_->swapchain_height)/s.size[1];
        const auto clampx=[&](float x){return LONG(std::clamp((x-s.position[0])*sx,0.f,float(device_->swapchain_width)));};
        const auto clampy=[&](float y){return LONG(std::clamp((y-s.position[1])*sy,0.f,float(device_->swapchain_height)));};
        if(!std::isfinite(draw.clip[0])||!std::isfinite(draw.clip[1])||!std::isfinite(draw.clip[2])||!std::isfinite(draw.clip[3])) continue;
        const D3D12_RECT clip{clampx(draw.clip[0]),clampy(draw.clip[1]),clampx(draw.clip[2]),clampy(draw.clip[3])};
        if(clip.left>=clip.right||clip.top>=clip.bottom) continue;
        struct Constants {u32 arena,offset;float scale[2],translate[2];u32 texture,pad[3];};
        const Constants constants{0,0,{2.f/s.size[0],-2.f/s.size[1]},
            {-1.f-s.position[0]*2.f/s.size[0],1.f+s.position[1]*2.f/s.size[1]},found->second.srv.index,{0,0,0}};
        list.SetDrawConstants(&constants,sizeof(constants)); list.SetScissor(clip);
        list.list->DrawIndexedInstanced(draw.count,1,draw.first_index,INT(draw.vertex_offset),0);
    }
    return true;
}
void ImGuiGpu::Destroy() {
    frames_={}; textures_.clear(); free_slots_.clear(); pipeline_={}; device_=nullptr; root_=nullptr; heap_=nullptr;
}
}
