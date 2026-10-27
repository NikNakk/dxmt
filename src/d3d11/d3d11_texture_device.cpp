/*
 * Copyright 2026 Feifan He for CodeWeavers
 * Copyright 2026 Marc-Aurel Zent for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "com/com_pointer.hpp"
#include "d3d11_device.hpp"
#include "d3d11_enumerable.hpp"
#include "d3d11_view.hpp"
#include "dxmt_dynamic.hpp"
#include "dxmt_staging.hpp"
#include "dxmt_texture.hpp"
#include "d3d11_resource.hpp"
#include "dxmt_native_interop.h"
#include "util_win32_compat.h"

#include "util_env.hpp"

namespace dxmt {

#pragma region DeviceTexture

template <typename tag_texture>
class DeviceTexture : public TResourceBase<tag_texture, IMTLMinLODClampable> {
private:
  Rc<RenamableTexturePool> renamable_;
  float min_lod = 0.0;
  D3DKMT_HANDLE local_kmt_ = 0;
  D3DKMT_HANDLE global_kmt_ = 0;

  using SRVBase =
      TResourceViewBase<tag_shader_resource_view<DeviceTexture<tag_texture>>>;
  class TextureSRV : public SRVBase {
  public:
    TextureSRV(const TextureViewDescriptor &descriptor,
               const tag_shader_resource_view<>::DESC1 *pDesc,
               DeviceTexture *pResource, MTLD3D11Device *pDevice)
        : SRVBase(pDesc, pResource, pDevice) {
      this->texture_ = pResource->texture_.ptr();
      this->view_id_ = this->texture_->createView(descriptor);
      this->subset_ = ResourceSubsetState(
        &descriptor,
        this->texture_->miplevelCount(),
        this->texture_->arrayLength()
      );
    }

  };

  using UAVBase =
      TResourceViewBase<tag_unordered_access_view<DeviceTexture<tag_texture>>>;
  class TextureUAV : public UAVBase {
  public:
    TextureUAV(const TextureViewDescriptor &descriptor,
               const tag_unordered_access_view<>::DESC1 *pDesc,
               DeviceTexture *pResource, MTLD3D11Device *pDevice)
        : UAVBase(pDesc, pResource, pDevice) {
      this->texture_ = pResource->texture_.ptr();
      this->view_id_ = this->texture_->createView(descriptor);
      this->subset_ = ResourceSubsetState(
        &descriptor,
        this->texture_->miplevelCount(),
        this->texture_->arrayLength()
      );
    }
  };

  using RTVBase =
      TResourceViewBase<tag_render_target_view<DeviceTexture<tag_texture>>>;
  class TextureRTV : public RTVBase {
  public:
    TextureRTV(
        const TextureViewDescriptor &descriptor, WMTPixelFormat view_format, const tag_render_target_view<>::DESC1 *pDesc,
        DeviceTexture *pResource, MTLD3D11Device *pDevice, const MTL_RENDER_PASS_ATTACHMENT_DESC &mtl_rtv_desc
    ) :
        RTVBase(pDesc, pResource, pDevice) {
      this->texture_ = pResource->texture_.ptr();
      this->view_id_ = this->texture_->createView(descriptor);
      this->format_ = view_format;
      this->pass_desc_ = mtl_rtv_desc;
      this->subset_ = ResourceSubsetState(
        &descriptor,
        this->texture_->miplevelCount(),
        this->texture_->arrayLength()
      );
    }
  };

  using DSVBase =
      TResourceViewBase<tag_depth_stencil_view<DeviceTexture<tag_texture>>>;
  class TextureDSV : public DSVBase {
  public:
    TextureDSV(
        const TextureViewDescriptor &descriptor, WMTPixelFormat view_format, const tag_depth_stencil_view<>::DESC1 *pDesc,
        DeviceTexture *pResource, MTLD3D11Device *pDevice, const MTL_RENDER_PASS_ATTACHMENT_DESC &attachment_desc
    ) :
        DSVBase(pDesc, pResource, pDevice) {
      this->texture_ = pResource->texture_.ptr();
      this->view_id_ = this->texture_->createView(descriptor);
      this->format_ = view_format;
      this->pass_desc_ = attachment_desc;
      this->renamable_ = pResource->renamable_.ptr();
      this->readonly_flags_ = this->desc.Flags & 0b11;
      this->subset_ = ResourceSubsetState(
        &descriptor,
        this->texture_->miplevelCount(),
        this->texture_->arrayLength(),
        this->readonly_flags_
      );
    }
  };

public:
  DeviceTexture(const tag_texture::DESC1 *pDesc, Rc<Texture> &&u_texture, MTLD3D11Device *pDevice) :
      TResourceBase<tag_texture, IMTLMinLODClampable>(*pDesc, pDevice) {
        this->texture_ = std::move(u_texture);
      }

  DeviceTexture(
      const tag_texture::DESC1 *pDesc, Rc<Texture> &&u_texture, Rc<RenamableTexturePool> &&renamable,
      MTLD3D11Device *pDevice
  ) :
      TResourceBase<tag_texture, IMTLMinLODClampable>(*pDesc, pDevice),
      renamable_(std::move(renamable)) {
        this->texture_ = std::move(u_texture);
      }

  DeviceTexture(
      const tag_texture::DESC1 *pDesc, Rc<Texture> &&u_texture, Rc<KeyedMutex> &&keyed_mutex,
      MTLD3D11Device *pDevice
  ) :
      TResourceBase<tag_texture, IMTLMinLODClampable>(*pDesc, pDevice) {
        this->texture_ = std::move(u_texture);
        this->keyed_mutex_ = std::move(keyed_mutex);
      }

  DeviceTexture(
      const tag_texture::DESC1 *pDesc, Rc<Texture> &&u_texture, D3DKMT_HANDLE localHandle, D3DKMT_HANDLE globalHandle,
      Rc<KeyedMutex> && keyed_mutex, MTLD3D11Device *pDevice
  ) :
      TResourceBase<tag_texture, IMTLMinLODClampable>(*pDesc, pDevice),
      local_kmt_(localHandle), global_kmt_(globalHandle) {
        this->texture_ = std::move(u_texture);
        this->keyed_mutex_ = std::move(keyed_mutex);
      }

  ~DeviceTexture() {
    if (local_kmt_) {
      D3DKMT_DESTROYALLOCATION destroy = {};
      destroy.hDevice = this->m_parent->GetLocalD3DKMT();
      destroy.hResource = local_kmt_;
      D3DKMTDestroyAllocation(&destroy);
    }
  }

  Rc<StagingResource> staging(UINT) final { return nullptr; }
  Rc<DynamicBuffer> dynamicBuffer(UINT*, UINT*) final { return {}; }
  Rc<DynamicLinearTexture> dynamicLinearTexture(UINT*, UINT*) final { return {}; };
  Rc<DynamicBuffer> dynamicTexture(UINT , UINT *, UINT *) final { return {}; };

  HRESULT STDMETHODCALLTYPE CreateRenderTargetView(const D3D11_RENDER_TARGET_VIEW_DESC1 *pDesc,
                                 ID3D11RenderTargetView1 **ppView) override {
    D3D11_RENDER_TARGET_VIEW_DESC1 finalDesc;
    if (FAILED(ExtractEntireResourceViewDescription(&this->desc, pDesc,
                                                    &finalDesc))) {
      return E_INVALIDARG;
    }
    TextureViewDescriptor descriptor;
    uint32_t arraySize;
    if constexpr (std::is_same_v<typename tag_texture::DESC1, D3D11_TEXTURE3D_DESC1>) {
      arraySize = this->desc.Depth;
    } else {
      arraySize = this->desc.ArraySize;
    }
    MTL_RENDER_PASS_ATTACHMENT_DESC attachment_desc;
    if (FAILED(InitializeAndNormalizeViewDescriptor(
            this->m_parent, this->desc.MipLevels, arraySize, this->texture_.ptr(), finalDesc,
            attachment_desc, descriptor
        ))) {
      return E_FAIL;
    }
    if (!ppView) {
      return S_FALSE;
    }
    *ppView = ref(new TextureRTV(descriptor, descriptor.format, &finalDesc, this, this->m_parent, attachment_desc));
    return S_OK;
  };

  HRESULT STDMETHODCALLTYPE CreateDepthStencilView(const D3D11_DEPTH_STENCIL_VIEW_DESC *pDesc,
                                 ID3D11DepthStencilView **ppView) override {
    D3D11_DEPTH_STENCIL_VIEW_DESC finalDesc;
    if (FAILED(ExtractEntireResourceViewDescription(&this->desc, pDesc,
                                                    &finalDesc))) {
      return E_INVALIDARG;
    }
    TextureViewDescriptor descriptor;
    uint32_t arraySize;
    if constexpr (std::is_same_v<typename tag_texture::DESC1, D3D11_TEXTURE3D_DESC1>) {
      arraySize = this->desc.Depth;
    } else {
      arraySize = this->desc.ArraySize;
    }
    MTL_RENDER_PASS_ATTACHMENT_DESC attachment_desc;
    if (FAILED(InitializeAndNormalizeViewDescriptor(
            this->m_parent, this->desc.MipLevels, arraySize, this->texture_.ptr(), finalDesc,
            attachment_desc, descriptor
        ))) {
      return E_FAIL;
    }
    if (!ppView) {
      return S_FALSE;
    }
    *ppView = ref(new TextureDSV(descriptor, descriptor.format, &finalDesc, this, this->m_parent, attachment_desc));
    return S_OK;
  };

  HRESULT
  STDMETHODCALLTYPE
  CreateShaderResourceView(const D3D11_SHADER_RESOURCE_VIEW_DESC1 *pDesc,
                           ID3D11ShaderResourceView1 **ppView) override {
    D3D11_SHADER_RESOURCE_VIEW_DESC1 finalDesc;
    if (FAILED(ExtractEntireResourceViewDescription(&this->desc, pDesc,
                                                    &finalDesc))) {
      ERR("DeviceTexture: Failed to create SRV descriptor");
      return E_INVALIDARG;
    }
    TextureViewDescriptor descriptor;
    uint32_t arraySize;
    if constexpr (std::is_same_v<typename tag_texture::DESC1, D3D11_TEXTURE3D_DESC1>) {
      arraySize = this->desc.Depth;
    } else {
      arraySize = this->desc.ArraySize;
    }
    if (FAILED(InitializeAndNormalizeViewDescriptor(
            this->m_parent, this->desc.MipLevels, arraySize, this->texture_.ptr(), finalDesc, descriptor
        ))) {
      ERR("DeviceTexture: Failed to create texture SRV");
      return E_FAIL;
    }
    if (!ppView) {
      return S_FALSE;
    }
    *ppView = ref(new TextureSRV(descriptor, &finalDesc, this, this->m_parent));
    return S_OK;
  };

  HRESULT
  STDMETHODCALLTYPE
  CreateUnorderedAccessView(const D3D11_UNORDERED_ACCESS_VIEW_DESC1 *pDesc,
                            ID3D11UnorderedAccessView1 **ppView) override {
    D3D11_UNORDERED_ACCESS_VIEW_DESC1 finalDesc;
    if (FAILED(ExtractEntireResourceViewDescription(&this->desc, pDesc,
                                                    &finalDesc))) {
      return E_INVALIDARG;
    }
    TextureViewDescriptor descriptor;
    uint32_t arraySize;
    if constexpr (std::is_same_v<typename tag_texture::DESC1, D3D11_TEXTURE3D_DESC1>) {
      arraySize = this->desc.Depth;
    } else {
      arraySize = this->desc.ArraySize;
    }
    if (FAILED(InitializeAndNormalizeViewDescriptor(
            this->m_parent, this->desc.MipLevels, arraySize, this->texture_.ptr(), finalDesc, descriptor
        ))) {
      ERR("DeviceTexture: Failed to create texture UAV");
      return E_FAIL;
    }
    if (!ppView) {
      return S_FALSE;
    }
    *ppView = ref(new TextureUAV(descriptor, &finalDesc, this, this->m_parent));
    return S_OK;
  };

  virtual HRESULT
  GetSharedHandle(HANDLE *pSharedHandle) override {
    if (pSharedHandle == nullptr || (this->desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_NTHANDLE)) {
      return E_INVALIDARG;
    }

    if (!(this->desc.MiscFlags & (D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX))) {
      *pSharedHandle = NULL;
      return S_OK;
    }

    if (!global_kmt_) {
      return E_INVALIDARG;
    }

    *pSharedHandle = reinterpret_cast<HANDLE>(global_kmt_);
    DEBUG("share: GetSharedHandle ", tag_texture::debug_name, " -> 0x", str::format(std::hex, global_kmt_));
    return S_OK;
  }

  virtual HRESULT
  CreateSharedHandle(const SECURITY_ATTRIBUTES *Attributes, DWORD Access, const WCHAR *pName, HANDLE *pNTHandle)
      override {
    InitReturnPtr(pNTHandle);
    HRESULT hr = CreateSharedResourceNtHandle(local_kmt_, Attributes, Access, pName, pNTHandle);
    DEBUG("share: CreateSharedHandle ", tag_texture::debug_name, " global 0x", str::format(std::hex, global_kmt_), " -> ",
          pNTHandle ? *pNTHandle : nullptr);
    return hr;
  }

  /* Publish this texture's existing Metal storage for other processes, as
   * creation with D3D11_RESOURCE_MISC_SHARED would. See IDXMTNativeDevice3. */
  HRESULT
  ExportShared(HANDLE *pSharedHandle) {
    if (global_kmt_) {
      *pSharedHandle = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(global_kmt_));
      return S_OK;
    }
    if (this->desc.MiscFlags & (D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE |
                                D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX))
      return E_INVALIDARG; // created shared: use GetSharedHandle/CreateSharedHandle
    if (!(this->m_parent->GetLocalD3DKMT() & 0xc0000000)) {
      ERR("ExportShared: Invalid device handle");
      return E_FAIL;
    }
    TextureAllocation *allocation = this->texture_->current();
    mach_port_t mach_port = allocation ? MTLTexture_createSharedMachPort(allocation->texture().handle) : 0;
    if (!mach_port) {
      WARN("ExportShared: texture storage is not shareable");
      return DXGI_ERROR_UNSUPPORTED;
    }
    SharedResourceData runtimeData = {};
    MakeUniqueSharedName(runtimeData.mach_port_name);
    if (!WMTBootstrapRegister(runtimeData.mach_port_name, mach_port)) {
      ERR("ExportShared: Failed to register mach port");
      return E_FAIL;
    }
    typename tag_texture::DESC1 shared_desc = this->desc;
    shared_desc.MiscFlags |= D3D11_RESOURCE_MISC_SHARED;
    runtimeData.dimension = tag_texture::dimension;
    memcpy(&runtimeData.desc, &shared_desc, sizeof(shared_desc));

    D3DKMT_CREATEALLOCATION create = {};
    create.hDevice = this->m_parent->GetLocalD3DKMT();
    create.pPrivateRuntimeData = &runtimeData;
    create.PrivateRuntimeDataSize = sizeof(runtimeData);
    create.Flags.StandardAllocation = 1;
    create.NumAllocations = 1;
    D3DDDI_ALLOCATIONINFO2 allocationInfo = {};
    create.pAllocationInfo2 = &allocationInfo;
    D3DKMT_CREATESTANDARDALLOCATION standardAllocation = {};
    create.pStandardAllocation = &standardAllocation;
    standardAllocation.Type = D3DKMT_STANDARDALLOCATIONTYPE_EXISTINGHEAP;
    create.Flags.ExistingSysMem = 1;
    D3DDDI_ALLOCATIONINFO systemMem;
    allocationInfo.pSystemMem = &systemMem;
    create.Flags.CreateResource = 1;
    create.Flags.CreateShared = 1;
    if (D3DKMTCreateAllocation2(&create)) {
      ERR("ExportShared: Failed to create D3DKMT resource");
      return E_FAIL;
    }
    local_kmt_ = create.hResource;
    global_kmt_ = create.hGlobalShare;
    this->desc.MiscFlags |= D3D11_RESOURCE_MISC_SHARED;
    *pSharedHandle = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(global_kmt_));
    return S_OK;
  }

  void SetMinLOD(float MinLod) override { min_lod = MinLod; }

  float GetMinLOD() override { return min_lod; }
};

template <typename tag>
HRESULT CreateDeviceTextureInternal(MTLD3D11Device *pDevice,
                                    const typename tag::DESC1 *pDesc,
                                    const D3D11_SUBRESOURCE_DATA *pInitialData,
                                    typename tag::COM_IMPL **ppTexture) {
  WMTTextureInfo info;
  typename tag::DESC1 finalDesc;
  if (FAILED(CreateMTLTextureDescriptor(pDevice, pDesc, &finalDesc, &info))) {
    return E_INVALIDARG;
  }
  bool single_subresource = info.mipmap_level_count == 1 && info.array_length == 1 &&
                            !(finalDesc.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE);

  MTL_DXGI_FORMAT_DESC format_desc;
  MTLQueryDXGIFormat(pDevice->GetMTLDevice(), finalDesc.Format, format_desc);

  // Typeless textures get PixelFormatView usage so they can be viewed in any format of their
  // family, but the usage stops Apple GPUs compressing them, which costs bandwidth on every
  // render target and resolve. Most are only ever viewed in their storage format or its sRGB
  // twin, so leave the usage off and add it when a view first needs it (Texture::createView).
  // Only for GPU-only textures that are not shared and not depth/stencil (plane views).
  // DXMT_PIXEL_FORMAT_VIEW=always keeps the usage from the start.
  static const bool pfv_always = env::getEnvVar("DXMT_PIXEL_FORMAT_VIEW") == "always";
  auto shared_misc =
      D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
  bool pfv_on_demand = !pfv_always && (info.usage & WMTTextureUsagePixelFormatView) &&
                       (format_desc.Flag & MTL_DXGI_FORMAT_TYPELESS) &&
                       !(format_desc.Flag & (MTL_DXGI_FORMAT_DEPTH_PLANER | MTL_DXGI_FORMAT_STENCIL_PLANER)) &&
                       !finalDesc.CPUAccessFlags && !(finalDesc.MiscFlags & shared_misc) &&
                       !(finalDesc.BindFlags & D3D11_BIND_DEPTH_STENCIL) &&
                       (finalDesc.Usage == D3D11_USAGE_DEFAULT || finalDesc.Usage == D3D11_USAGE_IMMUTABLE);
  if (pfv_on_demand)
    info.usage = (WMTTextureUsage)(info.usage & ~WMTTextureUsagePixelFormatView);
  auto texture = Rc<Texture>(new Texture(info, pDevice->GetMTLDevice()));
  if (pfv_on_demand)
    texture->setPixelFormatViewUpgradeQueue(&pDevice->GetDXMTDevice().queue());

  auto &initializer = pDevice->GetDXMTDevice().queue().initializer;

  auto initialize = [&](Rc<TextureAllocation> &&allocation) {
    texture->rename(std::move(allocation));
    if (!pInitialData) {
      for (auto sub : EnumerateSubresources(finalDesc)) {
        initializer.initWithZero(texture.ptr(), texture->current(), sub.ArraySlice, sub.MipLevel);
      }
    } else {
      for (auto sub : EnumerateSubresources(finalDesc)) {
        auto &data = pInitialData[sub.SubresourceId];
        initializer.initWithData(
            texture.ptr(), texture->current(), sub.ArraySlice, sub.MipLevel, data.pSysMem, data.SysMemPitch,
            data.SysMemSlicePitch, format_desc.Flag
        );
      }
    }
  };

  auto shared_flag =
      D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
  if (finalDesc.MiscFlags & shared_flag) {
    if (!(pDevice->GetLocalD3DKMT() & 0xc0000000)) {
      ERR("DeviceTexture: Invalid device handle");
      return E_FAIL;
    }
    // use a dedicated path for now, because there are other works for private storage mode

    Flags<TextureAllocationFlag> flags;
    flags.set(TextureAllocationFlag::GpuPrivate);
    if (finalDesc.Usage == D3D11_USAGE_IMMUTABLE)
      flags.set(TextureAllocationFlag::GpuReadonly);
    if (!(finalDesc.BindFlags & (D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET | D3D11_BIND_DEPTH_STENCIL)))
      flags.set(TextureAllocationFlag::ShaderReadonly);
    flags.set(TextureAllocationFlag::Shared);
    auto allocation = texture->allocate(flags);

    mach_port_t mach_port = allocation->machPort;
    if (!mach_port) {
      ERR("DeviceTexture: Failed to get mach port for shared texture");
      return E_FAIL;
    }
    SharedResourceData runtimeData;
    MakeUniqueSharedName(runtimeData.mach_port_name);
    if (!WMTBootstrapRegister(runtimeData.mach_port_name, mach_port)) {
      ERR("DeviceTexture: Failed to register mach port for shared texture");
      return E_FAIL;
    }
    runtimeData.dimension = tag::dimension;
    memcpy(&runtimeData.desc, pDesc, sizeof(typename tag::DESC1));

    Rc<KeyedMutex> keyed_mutex = {};
    if (finalDesc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX) {
      keyed_mutex = KeyedMutex::create(pDevice->GetMTLDevice());
    }
    runtimeData.mutex_handle = keyed_mutex ? keyed_mutex->globalHandle() : 0;

    D3DKMT_CREATEALLOCATION create = {};
    create.hDevice = pDevice->GetLocalD3DKMT();
    create.pPrivateRuntimeData = &runtimeData;
    create.PrivateRuntimeDataSize = sizeof(runtimeData);
    create.Flags.StandardAllocation = 1;
    create.NumAllocations = 1;
    D3DDDI_ALLOCATIONINFO2 allocationInfo = {};
    create.pAllocationInfo2 = &allocationInfo;
    D3DKMT_CREATESTANDARDALLOCATION standardAllocation = {};
    create.pStandardAllocation = &standardAllocation;
    standardAllocation.Type = D3DKMT_STANDARDALLOCATIONTYPE_EXISTINGHEAP;
    create.Flags.ExistingSysMem = 1;
    D3DDDI_ALLOCATIONINFO systemMem;
    allocationInfo.pSystemMem = &systemMem;
    create.Flags.CreateResource = 1;
    create.Flags.CreateShared = 1;
    create.Flags.NtSecuritySharing = !!(finalDesc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_NTHANDLE);
    if (D3DKMTCreateAllocation2(&create)) {
      ERR("DeviceTexture: Failed to create D3DKMT for shared texture");
      return E_FAIL;
    }

    DEBUG("share: created ", tag::debug_name, " ", info.width, "x", info.height, " format ", finalDesc.Format,
          " bind 0x", str::format(std::hex, finalDesc.BindFlags), " misc 0x", str::format(std::hex, finalDesc.MiscFlags),
          " global 0x", str::format(std::hex, create.hGlobalShare), " mutex 0x",
          str::format(std::hex, runtimeData.mutex_handle), " initial data ", pInitialData != nullptr);
    initialize(std::move(allocation));
    *ppTexture = reinterpret_cast<typename tag::COM_IMPL *>(
        ref(new DeviceTexture<tag>(&finalDesc, std::move(texture), create.hResource,
                                   create.hGlobalShare, std::move(keyed_mutex), pDevice))
    );

    /*
     * Publish DXMT's existing MTLSharedTextureHandle bootstrap registration.
     * The payload is metadata only: the shared Metal storage is not copied.
     */
    (*ppTexture)->SetPrivateData(DXMT_GUID_SHARED_TEXTURE_BOOTSTRAP_NAME,
                                 sizeof(runtimeData.mach_port_name), runtimeData.mach_port_name);
    return S_OK;
  }

  Flags<TextureAllocationFlag> flags;
  flags.set(finalDesc.CPUAccessFlags ? TextureAllocationFlag::GpuManaged : TextureAllocationFlag::GpuPrivate);
  if (!(finalDesc.BindFlags & (D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET | D3D11_BIND_DEPTH_STENCIL)))
    flags.set(TextureAllocationFlag::ShaderReadonly);
  if (finalDesc.Usage == D3D11_USAGE_IMMUTABLE)
    flags.set(TextureAllocationFlag::GpuReadonly);
  if (single_subresource && (finalDesc.BindFlags & D3D11_BIND_DEPTH_STENCIL)) {
    Rc<RenamableTexturePool> renamable = new RenamableTexturePool(texture.ptr(), 32, flags);
    initialize(renamable->getNext(0));
    *ppTexture = reinterpret_cast<typename tag::COM_IMPL *>(
        ref(new DeviceTexture<tag>(&finalDesc, std::move(texture), std::move(renamable), pDevice)));
  } else {
    initialize(texture->allocate(flags));
    *ppTexture = reinterpret_cast<typename tag::COM_IMPL *>(
        ref(new DeviceTexture<tag>(&finalDesc, std::move(texture), pDevice)));
  }
  return S_OK;
}

HRESULT
CreateDeviceTexture1D(MTLD3D11Device *pDevice,
                      const D3D11_TEXTURE1D_DESC *pDesc,
                      const D3D11_SUBRESOURCE_DATA *pInitialData,
                      ID3D11Texture1D **ppTexture) {
  return CreateDeviceTextureInternal<tag_texture_1d>(pDevice, pDesc,
                                                     pInitialData, ppTexture);
}

HRESULT
CreateDeviceTexture2D(MTLD3D11Device *pDevice,
                      const D3D11_TEXTURE2D_DESC1 *pDesc,
                      const D3D11_SUBRESOURCE_DATA *pInitialData,
                      ID3D11Texture2D1 **ppTexture) {
  return CreateDeviceTextureInternal<tag_texture_2d>(pDevice, pDesc,
                                                     pInitialData, ppTexture);
}

HRESULT
CreateDeviceTexture3D(MTLD3D11Device *pDevice,
                      const D3D11_TEXTURE3D_DESC1 *pDesc,
                      const D3D11_SUBRESOURCE_DATA *pInitialData,
                      ID3D11Texture3D1 **ppTexture) {
  return CreateDeviceTextureInternal<tag_texture_3d>(pDevice, pDesc,
                                                     pInitialData, ppTexture);
}

template <typename tag>
HRESULT
ImportSharedTextureInternal(
    MTLD3D11Device *pDevice, const typename tag::DESC1 *pDescUnchecked, mach_port_t MachPort,
    D3DKMT_HANDLE hSharedKeyedMutex, D3DKMT_HANDLE hLocal, D3DKMT_HANDLE hGlobal, REFIID riid, void **ppTexture
) {
  WMTTextureInfo info;
  typename tag::DESC1 finalDesc;
  if (FAILED(CreateMTLTextureDescriptor(pDevice, pDescUnchecked, &finalDesc, &info))) {
    DestroySharedResourceHandle(pDevice, hLocal);
    return E_INVALIDARG;
  }

  auto texture = Rc<Texture>(new Texture(info, pDevice->GetMTLDevice()));
  auto allocation = texture->import(MachPort);
  if (!allocation) {
    DestroySharedResourceHandle(pDevice, hLocal);
    return E_FAIL;
  }
  texture->rename(std::move(allocation));

  Rc<KeyedMutex> keyed_mutex;
  if (hSharedKeyedMutex & 0xc0000000)
    keyed_mutex = KeyedMutex::import(pDevice->GetMTLDevice(), hSharedKeyedMutex);

  DEBUG("share: imported ", tag::debug_name, " ", info.width, "x", info.height, " format ", finalDesc.Format,
        " bind 0x", str::format(std::hex, finalDesc.BindFlags), " misc 0x", str::format(std::hex, finalDesc.MiscFlags),
        " global 0x", str::format(std::hex, hGlobal), " mutex 0x", str::format(std::hex, hSharedKeyedMutex),
        " as ", str::format(riid));
  Com<DeviceTexture<tag>> device_texture =
      ref(new DeviceTexture<tag>(&finalDesc, std::move(texture), hLocal, hGlobal, std::move(keyed_mutex), pDevice));
  HRESULT hr = device_texture->QueryInterface(riid, ppTexture);
  if (FAILED(hr))
    WARN("ImportSharedTexture: no interface ", str::format(riid));
  return hr;
}

/*
 * Finish importing a D3DKMT shared resource. Ownership of the opened local
 * handle (hLocal) passes to the importer, and then to the imported resource,
 * which keeps the shared object alive for as long as it exists, as on Windows.
 */
static HRESULT
ImportSharedResource(
    MTLD3D11Device *pDevice, SharedResourceData &runtimeData, D3DKMT_HANDLE hLocal, D3DKMT_HANDLE hGlobal,
    REFIID riid, void **ppResource
) {
  mach_port_t mach_port;
  if (!WMTBootstrapLookUp(runtimeData.mach_port_name, &mach_port)) {
    ERR("ImportSharedResource: Failed to look up mach port");
    DestroySharedResourceHandle(pDevice, hLocal);
    return E_INVALIDARG;
  }
  switch (runtimeData.dimension) {
  case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
    return ImportSharedTextureInternal<tag_texture_1d>(
        pDevice, &runtimeData.desc.desc1d, mach_port, runtimeData.mutex_handle, hLocal, hGlobal, riid, ppResource
    );
  case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
    return ImportSharedTextureInternal<tag_texture_2d>(
        pDevice, &runtimeData.desc.desc2d, mach_port, runtimeData.mutex_handle, hLocal, hGlobal, riid, ppResource
    );
  case D3D11_RESOURCE_DIMENSION_TEXTURE3D:
    return ImportSharedTextureInternal<tag_texture_3d>(
        pDevice, &runtimeData.desc.desc3d, mach_port, runtimeData.mutex_handle, hLocal, hGlobal, riid, ppResource
    );
  case D3D11_RESOURCE_DIMENSION_BUFFER:
    return ImportSharedBuffer(pDevice, &runtimeData.desc.desc_buffer, mach_port, hLocal, hGlobal, riid, ppResource);
  default:
    ERR("ImportSharedResource: Unsupported resource dimension");
    DestroySharedResourceHandle(pDevice, hLocal);
    return E_INVALIDARG;
  }
}

HRESULT
ImportSharedTexture(MTLD3D11Device *pDevice, HANDLE hResource, REFIID riid, void **ppTexture) {
  InitReturnPtr(ppTexture);

  if (!(reinterpret_cast<uintptr_t>(hResource) & 0xc0000000)) {
    WARN("ImportSharedTexture: Invalid shared handle type");
    return E_INVALIDARG;
  }

  if (ppTexture == nullptr)
    return S_FALSE;

  struct SharedResourceData runtimeData;

  D3DKMT_QUERYRESOURCEINFO query = {};
  query.hDevice = pDevice->GetLocalD3DKMT();
  query.hGlobalShare = reinterpret_cast<uintptr_t>(hResource);
  query.pPrivateRuntimeData = &runtimeData;
  query.PrivateRuntimeDataSize = sizeof(runtimeData);

  if (NTSTATUS status = D3DKMTQueryResourceInfo(&query)) {
    WARN("ImportSharedTexture: Failed to query resource: ", hResource, " device: ", query.hDevice, " status: ",
         str::format(std::hex, status));
    return E_INVALIDARG;
  }

  if (query.PrivateRuntimeDataSize != sizeof(runtimeData)) {
    WARN("ImportSharedTexture: Unexpected size: ", query.PrivateRuntimeDataSize);
    return E_INVALIDARG;
  } 

  D3DDDI_OPENALLOCATIONINFO2 alloc = {};
  D3DKMT_OPENRESOURCE open = {};
  open.hDevice = pDevice->GetLocalD3DKMT();
  open.hGlobalShare = reinterpret_cast<uintptr_t>(hResource);
  open.NumAllocations = 1;
  open.pOpenAllocationInfo2 = &alloc;
  open.pPrivateRuntimeData = &runtimeData;
  open.PrivateRuntimeDataSize = query.PrivateRuntimeDataSize;

  if (D3DKMTOpenResource2(&open)) {
    WARN("ImportSharedTexture: Failed to open resource: ", hResource);
    return E_INVALIDARG;
  }

  return ImportSharedResource(
      pDevice, runtimeData, open.hResource, static_cast<D3DKMT_HANDLE>(reinterpret_cast<uintptr_t>(hResource)), riid,
      ppTexture
  );
}

HRESULT
ExportSharedTexture(MTLD3D11Device *pDevice, ID3D11Texture2D *pTexture, HANDLE *pHandle) {
  if (!pHandle)
    return E_POINTER;
  *pHandle = nullptr;
  if (!pTexture)
    return E_INVALIDARG;
  Com<ID3D11Device> owner;
  pTexture->GetDevice(&owner);
  Com<ID3D11Device> self;
  pDevice->QueryInterface(__uuidof(ID3D11Device), reinterpret_cast<void **>(&self));
  D3D11_TEXTURE2D_DESC desc;
  pTexture->GetDesc(&desc);
  // Only DEFAULT textures are DeviceTexture objects.
  if (owner.ptr() != self.ptr() || desc.Usage != D3D11_USAGE_DEFAULT)
    return E_INVALIDARG;
  // DXMT hands out its resources' D3D11 interfaces as reinterpreted
  // D3D11ResourceCommon pointers (see CreateDeviceTextureInternal).
  auto *texture = static_cast<DeviceTexture<tag_texture_2d> *>(reinterpret_cast<D3D11ResourceCommon *>(pTexture));
  return texture->ExportShared(pHandle);
}

HRESULT
ImportNativeSharedTexture(MTLD3D11Device *device, const char *name,
                          const D3D11_TEXTURE2D_DESC *desc, ID3D11Texture2D **out) {
  if (!out) return E_POINTER;
  *out = nullptr;
  if (!name || !name[0] || strnlen(name, 128) == 128 || !desc ||
      desc->Usage != D3D11_USAGE_DEFAULT || desc->CPUAccessFlags || desc->MiscFlags ||
      desc->MipLevels != 1 || desc->SampleDesc.Count != 1 || desc->SampleDesc.Quality ||
      !desc->Width || !desc->Height || !desc->ArraySize || !desc->BindFlags ||
      (desc->BindFlags & ~(D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE)))
    return E_INVALIDARG;
  D3D11_TEXTURE2D_DESC1 requested = {};
  requested.Width = desc->Width;
  requested.Height = desc->Height;
  requested.MipLevels = desc->MipLevels;
  requested.ArraySize = desc->ArraySize;
  requested.Format = desc->Format;
  requested.SampleDesc = desc->SampleDesc;
  requested.Usage = desc->Usage;
  requested.BindFlags = desc->BindFlags;
  requested.TextureLayout = D3D11_TEXTURE_LAYOUT_UNDEFINED;
  D3D11_TEXTURE2D_DESC1 final_desc;
  WMTTextureInfo info;
  if (FAILED(CreateMTLTextureDescriptor(device, &requested, &final_desc, &info))) return E_INVALIDARG;
  auto texture = Rc<Texture>(new Texture(info, device->GetMTLDevice()));
  auto allocation = texture->importNative(name);
  if (!allocation) return E_INVALIDARG;
  texture->rename(std::move(allocation));
  Com<DeviceTexture<tag_texture_2d>> resource = ref(new DeviceTexture<tag_texture_2d>(
      &final_desc, std::move(texture), device));
  return resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void **>(out));
}

HRESULT
ImportNativeTextureObject(MTLD3D11Device *device, uint64_t object,
                          const D3D11_TEXTURE2D_DESC *desc, ID3D11Texture2D **out) {
  if (!out) return E_POINTER;
  *out = nullptr;
  if (!object || !desc ||
      desc->Usage != D3D11_USAGE_DEFAULT || desc->CPUAccessFlags || desc->MiscFlags ||
      desc->MipLevels != 1 || desc->SampleDesc.Count != 1 || desc->SampleDesc.Quality ||
      !desc->Width || !desc->Height || !desc->ArraySize || !desc->BindFlags ||
      (desc->BindFlags & ~(D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE)))
    return E_INVALIDARG;
  D3D11_TEXTURE2D_DESC1 requested = {};
  requested.Width = desc->Width;
  requested.Height = desc->Height;
  requested.MipLevels = desc->MipLevels;
  requested.ArraySize = desc->ArraySize;
  requested.Format = desc->Format;
  requested.SampleDesc = desc->SampleDesc;
  requested.Usage = desc->Usage;
  requested.BindFlags = desc->BindFlags;
  requested.TextureLayout = D3D11_TEXTURE_LAYOUT_UNDEFINED;
  D3D11_TEXTURE2D_DESC1 final_desc;
  WMTTextureInfo info;
  if (FAILED(CreateMTLTextureDescriptor(device, &requested, &final_desc, &info))) return E_INVALIDARG;
  auto texture = Rc<Texture>(new Texture(info, device->GetMTLDevice()));
  auto allocation = texture->importNativeObject(object);
  if (!allocation) return E_INVALIDARG;
  texture->rename(std::move(allocation));
  Com<DeviceTexture<tag_texture_2d>> resource = ref(new DeviceTexture<tag_texture_2d>(
      &final_desc, std::move(texture), device));
  return resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void **>(out));
}

HRESULT
ImportSharedTextureFromNtHandle(MTLD3D11Device *pDevice, HANDLE hResource, REFIID riid, void **ppTexture) {
  InitReturnPtr(ppTexture);

  if (reinterpret_cast<uintptr_t>(hResource) & 0xc0000000) {
    WARN("ImportSharedTextureFromNtHandle: Invalid shared handle type");
    return E_INVALIDARG;
  }

  if (ppTexture == nullptr)
    return S_FALSE;

  struct SharedResourceData runtimeData;

  D3DKMT_QUERYRESOURCEINFOFROMNTHANDLE query = {};
  query.hDevice = pDevice->GetLocalD3DKMT();
  query.hNtHandle = hResource;
  query.pPrivateRuntimeData = &runtimeData;
  query.PrivateRuntimeDataSize = sizeof(runtimeData);

  if (D3DKMTQueryResourceInfoFromNtHandle(&query)) {
    WARN(str::format("ImportSharedTextureFromNtHandle: Failed to query resource: ", hResource));
    return E_INVALIDARG;
  }
  
  if (query.PrivateRuntimeDataSize != sizeof(runtimeData)) {
    WARN(str::format("ImportSharedTextureFromNtHandle: Unexpected size: ", query.PrivateRuntimeDataSize));
    return E_INVALIDARG;
  }

  D3DDDI_OPENALLOCATIONINFO2 alloc = {};
  D3DKMT_OPENRESOURCEFROMNTHANDLE open = {};
  char dummy;

  open.hDevice = pDevice->GetLocalD3DKMT();
  open.hNtHandle = hResource;
  open.NumAllocations = 1;
  open.pOpenAllocationInfo2 = &alloc;
  open.pPrivateRuntimeData = &runtimeData;
  open.PrivateRuntimeDataSize = query.PrivateRuntimeDataSize;
  open.pTotalPrivateDriverDataBuffer = &dummy;
  open.TotalPrivateDriverDataBufferSize = 0;

  if (D3DKMTOpenResourceFromNtHandle(&open)) {
    WARN(str::format("ImportSharedTextureFromNtHandle: Failed to open resource: ", hResource));
    return E_INVALIDARG;
  }

  if (open.hSyncObject) {
    WARN(str::format("ImportSharedTextureFromNtHandle: Ignoring bundled sync object"));
    D3DKMT_DESTROYSYNCHRONIZATIONOBJECT destroySync = {};
    destroySync.hSyncObject = open.hSyncObject;
    D3DKMTDestroySynchronizationObject(&destroySync);
  }
  if (open.hKeyedMutex) {
    WARN(str::format("ImportSharedTextureFromNtHandle: Ignoring bundled keyed mutex"));
    D3DKMT_DESTROYKEYEDMUTEX destroyMutex = {};
    destroyMutex.hKeyedMutex = open.hKeyedMutex;
    D3DKMTDestroyKeyedMutex(&destroyMutex);
  }

  DEBUG("share: OpenSharedResource1 ", hResource);
  return ImportSharedResource(pDevice, runtimeData, open.hResource, 0, riid, ppTexture);
}

HRESULT
ImportSharedTextureByName(
    MTLD3D11Device *pDevice, LPCWSTR lpName, DWORD dwDesiredAccess, REFIID riid, void **ppTexture
) {
  D3DKMT_OPENNTHANDLEFROMNAME openFromName = {};
  openFromName.dwDesiredAccess = dwDesiredAccess;

  OBJECT_ATTRIBUTES attr = {};
  attr.Length = sizeof(attr);

  WCHAR buffer[MAX_PATH];
  UNICODE_STRING name_str;
  DWORD session, len, name_len = wcslen(lpName);

  ProcessIdToSessionId(GetCurrentProcessId(), &session);
  len = swprintf(buffer, ARRAYSIZE(buffer), L"\\Sessions\\%u\\BaseNamedObjects\\", session);
  memcpy(buffer + len, lpName, (name_len + 1) * sizeof(WCHAR));
  name_str.MaximumLength = name_str.Length = (len + name_len) * sizeof(WCHAR);
  name_str.MaximumLength += sizeof(WCHAR);
  name_str.Buffer = buffer;

  attr.ObjectName = &name_str;
  attr.Attributes = OBJ_CASE_INSENSITIVE;
  openFromName.pObjAttrib = &attr;

  if (D3DKMTOpenNtHandleFromName(&openFromName)) {
    WARN(str::format("ImportSharedTextureByName: Failed to open NT handle from name: ", lpName));
    return E_INVALIDARG;
  }

  HRESULT res = ImportSharedTextureFromNtHandle(pDevice, openFromName.hNtHandle, riid, ppTexture);
  CloseHandle(openFromName.hNtHandle);
  return res;
}

#pragma endregion

} // namespace dxmt
