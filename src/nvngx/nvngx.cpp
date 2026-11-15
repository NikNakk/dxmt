#include "com/com_pointer.hpp"
#include "d3d11.h"
#include "nvngx.hpp"
#include "log/log.hpp"
#include "util_env.hpp"
#include "nvngx_feature.hpp"
#include "nvngx_parameter.hpp"
#include "../d3d11/d3d11_interfaces.hpp"
#include "../dxgi/dxgi_interfaces.h"
#include <cmath>
#include <atomic>

namespace dxmt {
Logger Logger::s_instance("nvngx.log");

static bool ngx_trace_enabled() {
  static const bool enabled = env::getEnvVar("DXMT_NGX_TRACE") == "1";
  return enabled;
}
static void ngx_trace(const std::string &message) {
  if (ngx_trace_enabled()) Logger::info("NGX trace: " + message);
}

// Opt-in split between NGX core and its SR feature, for tools which hook the
// feature DLL. The identical implementation is installed as nvngx_dlss.dll.
// Keep the module resident: feature handles and third-party trampolines outlive
// individual calls. Never forward again from the feature module itself.
static bool ngx_is_feature_module = false;
static bool ngx_forward_feature() {
  return !ngx_is_feature_module && env::getEnvVar("DXMT_NGX_CHEEKY") == "1";
}
template<typename Function> static Function ngx_feature_export(const char *name) {
  static HMODULE feature_module = LoadLibraryW(L"nvngx_dlss.dll");
  return feature_module ? reinterpret_cast<Function>(GetProcAddress(feature_module, name)) : nullptr;
}


NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_Init_Ext(
    unsigned long long id, const wchar_t *path, ID3D11Device *device, unsigned int sdk_version, const void *feature_info
) {
  ngx_trace("NVSDK_NGX_D3D11_Init_Ext");
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_Init(
    unsigned long long id, const wchar_t *path, ID3D11Device *device, const void *feature_info, unsigned int sdk_version
) {
  ngx_trace("NVSDK_NGX_D3D11_Init");
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_Init_ProjectID(
    const char *project, unsigned int engine_type, const char *engine_version, const wchar_t *path,
    ID3D11Device *device, unsigned int sdk_version, const void *feature_info
) {
  ngx_trace("NVSDK_NGX_D3D11_Init_ProjectID");
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_Init_with_ProjectID(
    const char *project, unsigned int engine_type, const char *engine_version, const wchar_t *path,
    ID3D11Device *device, const void *feature_info, unsigned int sdk_version
) {
  ngx_trace("NVSDK_NGX_D3D11_Init_with_ProjectID");
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_CreateFeature(
    ID3D11DeviceContext *context, unsigned int feature, NVNGXParameter *params, unsigned int **out_handle
) {
  if (ngx_forward_feature()) {
    auto fn = ngx_feature_export<decltype(&NVSDK_NGX_D3D11_CreateFeature)>("NVSDK_NGX_D3D11_CreateFeature");
    ngx_trace("Core -> feature CreateFeature");
    return fn ? fn(context, feature, params, out_handle) : NVNGX_RESULT_FAIL;
  }
  ngx_trace(str::format("CreateFeature feature=", feature, " context=", context));
  if (!context || !params || !out_handle) return NVNGX_RESULT_INVALID_PARAMETER;
  auto parameters = params;
  Com<IMTLD3D11ContextExt1> pCtxExt = nullptr;
  if (FAILED(context->QueryInterface(IID_PPV_ARGS(&pCtxExt))))
    return NVNGX_RESULT_INVALID_PARAMETER;
  if (feature == NVNGX_FEATURE_SUPERSAMPLING) {
    BOOL feature_supported = false;
    if (FAILED(pCtxExt->CheckFeatureSupport(MTL_FEATURE_METALFX_TEMPORAL_SCALER, &feature_supported, sizeof(feature_supported))))
      return NVNGX_RESULT_FEATURE_NOT_SUPPORTED;
    if (!feature_supported)
      return NVNGX_RESULT_FEATURE_NOT_SUPPORTED;

    auto dlss = std::make_unique<DLSSFeature>();
    dlss->feature = NVNGX_FEATURE_SUPERSAMPLING;
    if (NVNGX_FAILED(parameters->Get(NVNGX_Parameter_Width, &dlss->width)))
      return NVNGX_RESULT_INVALID_PARAMETER;
    if (NVNGX_FAILED(parameters->Get(NVNGX_Parameter_Height, &dlss->height)))
      return NVNGX_RESULT_INVALID_PARAMETER;
    if (NVNGX_FAILED(parameters->Get(NVNGX_Parameter_OutWidth, &dlss->target_width)))
      return NVNGX_RESULT_INVALID_PARAMETER;
    if (NVNGX_FAILED(parameters->Get(NVNGX_Parameter_OutHeight, &dlss->target_height)))
      return NVNGX_RESULT_INVALID_PARAMETER;
    if (NVNGX_FAILED(parameters->Get(NVNGX_Parameter_PerfQualityValue, &dlss->quality)))
      dlss->quality = 0;
    if (NVNGX_FAILED(parameters->Get(NVNGX_Parameter_DLSS_Feature_Create_Flags, &dlss->flag)))
      dlss->flag = 0;
    if (NVNGX_FAILED(parameters->Get("DLSS.Enable.Output.Subrects", &dlss->enable_output_subrects)))
      dlss->enable_output_subrects = 0;

    ngx_trace(str::format("CreateFeature input=", dlss->width, "x", dlss->height,
        " output=", dlss->target_width, "x", dlss->target_height, " flags=", dlss->flag,
        " output_subrects=", dlss->enable_output_subrects));
    *out_handle = &dlss.release()->handle;
    return NVNGX_RESULT_OK;
  }

  ERR("NVSDK_NGX_D3D11_CreateFeature: feature ", feature, " not handled");
  return NVNGX_RESULT_FAIL;
}

struct NGXRegion {
  ID3D11Texture2D *source;
  UINT x=0, y=0, width=0, height=0, storage_width=0, storage_height=0;
  D3D11_TEXTURE2D_DESC desc={};
  bool copy=false;
};
static unsigned ngx_origin(NVNGXParameter *parameters, const char *name) {
  unsigned value=0; parameters->Get(name,&value); return value;
}
static bool ngx_validate_region(NGXRegion &r) {
  if (!r.source || !r.width || !r.height || !r.storage_width || !r.storage_height) return false;
  r.source->GetDesc(&r.desc);
  if (r.desc.SampleDesc.Count!=1 || r.desc.ArraySize!=1 || r.desc.MipLevels!=1 ||
      r.desc.Usage!=D3D11_USAGE_DEFAULT || r.width>r.desc.Width || r.height>r.desc.Height ||
      r.x>r.desc.Width-r.width || r.y>r.desc.Height-r.height ||
      r.width>r.storage_width || r.height>r.storage_height) return false;
  r.copy=r.x || r.y || r.desc.Width!=r.storage_width || r.desc.Height!=r.storage_height;
  return true;
}
static HRESULT ngx_prepare_region(ID3D11Device *device, NGXRegion &r, Com<ID3D11Texture2D> &cache) {
  if (!r.copy) return S_OK;
  auto desc=r.desc; desc.Width=r.storage_width; desc.Height=r.storage_height;
  desc.MiscFlags=0; desc.CPUAccessFlags=0;
  if (cache) {
    D3D11_TEXTURE2D_DESC old={}; cache->GetDesc(&old);
    if (old.Width==desc.Width && old.Height==desc.Height && old.Format==desc.Format &&
        old.BindFlags==desc.BindFlags) return S_OK;
  }
  Com<ID3D11Texture2D> replacement;
  auto hr=device->CreateTexture2D(&desc,nullptr,&replacement);
  if (SUCCEEDED(hr)) cache=std::move(replacement);
  return hr;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_EvaluateFeature(
    ID3D11DeviceContext *context, const unsigned int *handle, NVNGXParameter *params, void *callback
) {
  if (ngx_forward_feature()) {
    auto fn = ngx_feature_export<decltype(&NVSDK_NGX_D3D11_EvaluateFeature)>("NVSDK_NGX_D3D11_EvaluateFeature");
    return fn ? fn(context, handle, params, callback) : NVNGX_RESULT_FAIL;
  }
  if (!context || !handle || !params) return NVNGX_RESULT_INVALID_PARAMETER;
  static std::atomic<uint64_t> evaluations{0};
  uint64_t sequence = ++evaluations;
  auto parameters = params;
  switch (static_cast<CommonFeature *>((void *)handle)->feature) {
  case NVNGX_FEATURE_SUPERSAMPLING: {
    auto dlss = static_cast<DLSSFeature *>((void *)handle);

    Com<IMTLD3D11ContextExt> pCtxExt;
    if (FAILED(context->QueryInterface(IID_PPV_ARGS(&pCtxExt))))
      return NVNGX_RESULT_INVALID_PARAMETER;

    ID3D11Resource *resources[5]={};
    const char *resource_keys[]={NVNGX_Parameter_Color,NVNGX_Parameter_Output,NVNGX_Parameter_Depth,
        NVNGX_Parameter_MotionVectors,NVNGX_Parameter_ExposureTexture};
    Com<ID3D11Texture2D> textures[5];
    for(unsigned i=0;i<5;++i) {
      auto result=parameters->Get(resource_keys[i],&resources[i]);
      if(i==4 && (NVNGX_FAILED(result) || !resources[i])) continue;
      if(NVNGX_FAILED(result) || !resources[i] || FAILED(resources[i]->QueryInterface(IID_PPV_ARGS(&textures[i]))))
        return NVNGX_RESULT_INVALID_PARAMETER;
    }
    auto input=textures[0].ptr(), output=textures[1].ptr(), depth=textures[2].ptr(),
         mv=textures[3].ptr(), exposure=textures[4].ptr();

    uint32_t width = 0, height = 0;
    if (NVNGX_FAILED(parameters->Get(NVNGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, &width)))
      return NVNGX_RESULT_INVALID_PARAMETER;
    if (NVNGX_FAILED(parameters->Get(NVNGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, &height)))
      return NVNGX_RESULT_INVALID_PARAMETER;

    MTL_TEMPORAL_UPSCALE_D3D11_DESC desc = {};
    desc.InputContentWidth = width;
    desc.InputContentHeight = height;

    desc.Color = input;
    desc.Output = output;
    desc.Depth = depth;
    desc.MotionVector = mv;
    desc.ExposureTexture = exposure;
    desc.DepthReversed = bool(dlss->flag & NVNGX_DLSS_FLAG_DEPTH_INVERTED);
    desc.AutoExposure = bool(dlss->flag & NVNGX_DLSS_FLAG_AUTO_EXPOSURE);
    desc.MotionVectorInDisplayRes = !bool(dlss->flag & NVNGX_DLSS_FLAG_MV_LOWRES);

    if (NVNGX_FAILED(parameters->Get(NVNGX_Parameter_Jitter_Offset_X, &desc.JitterOffsetX)))
      desc.JitterOffsetX = 0;
    if (NVNGX_FAILED(parameters->Get(NVNGX_Parameter_Jitter_Offset_Y, &desc.JitterOffsetY)))
      desc.JitterOffsetY = 0;

    int reset = 0;
    parameters->Get(NVNGX_Parameter_Reset, &reset);
    desc.InReset = reset;

    if (NVNGX_FAILED(parameters->Get(NVNGX_Parameter_MV_Scale_X, &desc.MotionVectorScaleX)))
      desc.MotionVectorScaleX = 1.0;
    if (NVNGX_FAILED(parameters->Get(NVNGX_Parameter_MV_Scale_Y, &desc.MotionVectorScaleY)))
      desc.MotionVectorScaleY = 1.0;
    if (NVNGX_FAILED(parameters->Get(NVNGX_Parameter_DLSS_Pre_Exposure, &desc.PreExposure)))
      desc.PreExposure = 1.0;

    if (ngx_trace_enabled() && (sequence <= 32 || sequence % 512 == 0)) {
      D3D11_TEXTURE2D_DESC in_desc = {}, out_desc = {};
      input->GetDesc(&in_desc); output->GetDesc(&out_desc);
      ngx_trace(str::format("Evaluate seq=", sequence, " handle=", handle,
          " input=", in_desc.Width, "x", in_desc.Height, " fmt=", in_desc.Format,
          " output=", out_desc.Width, "x", out_desc.Height, " fmt=", out_desc.Format,
          " content=", width, "x", height, " reset=", reset,
          " color=", input, " depth=", depth, " motion=", mv, " target=", output));
      const char *keys[] = {"DLSS.Input.Color.Subrect.Base.X", "DLSS.Input.Color.Subrect.Base.Y",
          "DLSS.Input.Depth.Subrect.Base.X", "DLSS.Input.Depth.Subrect.Base.Y",
          "DLSS.Input.MV.Subrect.Base.X", "DLSS.Input.MV.Subrect.Base.Y",
          "DLSS.Output.Subrect.Base.X", "DLSS.Output.Subrect.Base.Y"};
      for (auto key : keys) { unsigned value = 0;
        auto result = parameters->Get(key, &value);
        ngx_trace(str::format("  ", key, "=", value, " result=", (unsigned)result));
      }
    }
    static const bool subrects_enabled=env::getEnvVar("DXMT_NGX_SUBRECTS")=="1";
    if (!subrects_enabled) { pCtxExt->TemporalUpscale(&desc); return NVNGX_RESULT_OK; }
    if (!width) width=dlss->width;
    if (!height) height=dlss->height;
    desc.InputContentWidth=width; desc.InputContentHeight=height;
    NGXRegion regions[4]={
      {input,ngx_origin(parameters,"DLSS.Input.Color.Subrect.Base.X"),ngx_origin(parameters,"DLSS.Input.Color.Subrect.Base.Y"),width,height,dlss->width,dlss->height},
      {depth,ngx_origin(parameters,"DLSS.Input.Depth.Subrect.Base.X"),ngx_origin(parameters,"DLSS.Input.Depth.Subrect.Base.Y"),width,height,dlss->width,dlss->height},
      {mv,ngx_origin(parameters,"DLSS.Input.MV.Subrect.Base.X"),ngx_origin(parameters,"DLSS.Input.MV.Subrect.Base.Y"),
          desc.MotionVectorInDisplayRes ? dlss->target_width : width,
          desc.MotionVectorInDisplayRes ? dlss->target_height : height,
          desc.MotionVectorInDisplayRes ? dlss->target_width : dlss->width,
          desc.MotionVectorInDisplayRes ? dlss->target_height : dlss->height},
      {output,ngx_origin(parameters,"DLSS.Output.Subrect.Base.X"),ngx_origin(parameters,"DLSS.Output.Subrect.Base.Y"),
          dlss->target_width,dlss->target_height,dlss->target_width,dlss->target_height}
    };
    bool copies=false;
    for (auto &r:regions) { if (!ngx_validate_region(r)) return NVNGX_RESULT_INVALID_PARAMETER; copies|=r.copy; }
    if ((regions[3].x || regions[3].y) && !dlss->enable_output_subrects) return NVNGX_RESULT_INVALID_PARAMETER;
    Com<IMTLD3D11ContextExt2> roi;
    if (copies && FAILED(context->QueryInterface(IID_PPV_ARGS(&roi)))) return NVNGX_RESULT_FEATURE_NOT_SUPPORTED;
    Com<ID3D11Device> device; context->GetDevice(&device);
    for (auto &r:regions) {
      Com<ID3D11Device> owner; r.source->GetDevice(&owner);
      if (owner.ptr()!=device.ptr()) return NVNGX_RESULT_INVALID_PARAMETER;
    }
    // Allocate every required scratch texture before queuing any copies.
    for (unsigned i=0;i<4;++i)
      if (FAILED(ngx_prepare_region(device.ptr(),regions[i],dlss->roi_textures[i]))) return NVNGX_RESULT_FAIL;
    ID3D11Resource **bindings[]={&desc.Color,&desc.Depth,&desc.MotionVector,&desc.Output};
    for (unsigned i=0;i<4;++i) if (regions[i].copy) *bindings[i]=dlss->roi_textures[i].ptr();
    for (unsigned i=0;i<3;++i) if (regions[i].copy) {
      auto &r=regions[i];
      if (FAILED(roi->CopyTemporalRegion(r.source,r.x,r.y,dlss->roi_textures[i].ptr(),0,0,r.width,r.height)))
        return NVNGX_RESULT_FEATURE_NOT_SUPPORTED;
    }
    if (ngx_trace_enabled() && (sequence<=32 || sequence%512==0))
      ngx_trace(str::format("ROI input=",width,"x",height," output=",dlss->target_width,"x",dlss->target_height,
          " color_origin=",regions[0].x,",",regions[0].y," depth_origin=",regions[1].x,",",regions[1].y,
          " motion_origin=",regions[2].x,",",regions[2].y," output_origin=",regions[3].x,",",regions[3].y,
          " motion_source=",regions[2].desc.Width,"x",regions[2].desc.Height," copies=",copies));
    pCtxExt->TemporalUpscale(&desc);
    if (regions[3].copy) {
      auto &r=regions[3];
      if (FAILED(roi->CopyTemporalRegion(dlss->roi_textures[3].ptr(),0,0,output,r.x,r.y,r.width,r.height)))
        return NVNGX_RESULT_FEATURE_NOT_SUPPORTED;
    }

    return NVNGX_RESULT_OK;
    break;
  }
  default:
    break;
  }
  return NVNGX_RESULT_FAIL;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_Shutdown() {
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_Shutdown1(ID3D11Device *device) {
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_GetScratchBufferSize(unsigned int feature, const NVNGXParameter *params, size_t *out_size) {
  if (out_size)
    *out_size = 0;
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_ReleaseFeature(unsigned int *handle) {
  if (ngx_forward_feature()) {
    auto fn = ngx_feature_export<decltype(&NVSDK_NGX_D3D11_ReleaseFeature)>("NVSDK_NGX_D3D11_ReleaseFeature");
    return fn ? fn(handle) : NVNGX_RESULT_FAIL;
  }
  if (!handle) return NVNGX_RESULT_INVALID_PARAMETER;
  switch (static_cast<CommonFeature *>((void *)handle)->feature) {
  case NVNGX_FEATURE_SUPERSAMPLING:
    delete static_cast<DLSSFeature *>((void *)handle);
    break;
  default:
    break;
  }
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_DestroyParameters(NVNGXParameter *params) {
  delete static_cast<ParametersImpl *>(params);
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_AllocateParameters(NVNGXParameter **out_params) {
  *out_params = new ParametersImpl();
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_UpdateFeature(const void *app_id, const unsigned int feature) {
  // do nothing
  return NVNGX_RESULT_OK;
}

inline static NVNGX_RESULT
NVNGX_DLSS_GetOptimalSettingsCallback(NVNGXParameter *params) {
  unsigned int width;
  unsigned int height;
  unsigned int out_width;
  unsigned int out_height;
  float scale = 0.0f;
  NVNGX_PERFQUALITY perf_quality_value;

  if (params->Get(NVNGX_Parameter_Width, &width) != NVNGX_RESULT_OK ||
      params->Get(NVNGX_Parameter_Height, &height) != NVNGX_RESULT_OK ||
      params->Get(NVNGX_Parameter_PerfQualityValue, (int *)&perf_quality_value) != NVNGX_RESULT_OK)
    return NVNGX_RESULT_FAIL;

  switch (perf_quality_value) {
  case NVNGX_PERFQUALITY_ULTRA_PERFORMANCE:
    scale = 1.0f / 3.0f;
    break;
  case NVNGX_PERFQUALITY_MAXPERF:
    scale = 0.5f;
    break;
  case NVNGX_PERFQUALITY_MAXQUALITY:
    scale = 1.0f / 1.5f;
    break;
  case NVNGX_PERFQUALITY_ULTRA_QUALITY:
    scale = 1.0f / 1.3f;
    break;
  case NVNGX_PERFQUALITY_DLAA:
    scale = 1.0f;
    break;
  default:
    scale = 58.0f / 100.0f;
    break;
  }

  out_width = std::ceil(width * scale);
  out_height = std::ceil(height * scale);

  params->Set(NVNGX_Parameter_Scale, scale);
  params->Set(NVNGX_Parameter_SuperSampling_ScaleFactor, scale);
  params->Set(NVNGX_Parameter_OutWidth, out_width);
  params->Set(NVNGX_Parameter_OutHeight, out_height);
  params->Set(NVNGX_Parameter_Sharpness, 0.0f);

  params->Set(NVNGX_Parameter_DLSS_Get_Dynamic_Min_Render_Width, (unsigned int)std::ceil(width / 3));
  params->Set(NVNGX_Parameter_DLSS_Get_Dynamic_Min_Render_Height, (unsigned int)std::ceil(height / 3));

  params->Set(NVNGX_Parameter_DLSS_Get_Dynamic_Max_Render_Width, width);
  params->Set(NVNGX_Parameter_DLSS_Get_Dynamic_Max_Render_Height, height);

  params->Set("DLSSMode", 1);

  params->Set(NVNGX_Parameter_DLSS_Hint_Render_Preset_DLAA, (unsigned int)0);
  params->Set(NVNGX_Parameter_DLSS_Hint_Render_Preset_UltraQuality, (unsigned int)0);
  params->Set(NVNGX_Parameter_DLSS_Hint_Render_Preset_Quality, (unsigned int)0);
  params->Set(NVNGX_Parameter_DLSS_Hint_Render_Preset_Balanced, (unsigned int)0);
  params->Set(NVNGX_Parameter_DLSS_Hint_Render_Preset_Performance, (unsigned int)0);
  params->Set(NVNGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance, (unsigned int)0);

  return NVNGX_RESULT_OK;
}

inline static NVNGX_RESULT
NVNGX_DLSS_GetStatsCallback(NVNGXParameter *params) {
  params->Set("SizeInBytes", 0ull);
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_GetCapabilityParameters(NVNGXParameter **out_params) {
  ngx_trace("GetCapabilityParameters");
  auto out_parameters = new ParametersImpl();

  out_parameters->Set("SuperSampling.Available", 1u);
  out_parameters->Set(NVSDK_NGX_EParameter_SuperSampling_Available, 1u);
  out_parameters->Set("SuperSampling.MinDriverVersionMajor", 0);
  out_parameters->Set("SuperSampling.MinDriverVersionMinor", 0);

  out_parameters->Set("SuperSampling.NeedsUpdatedDriver", 0);
  out_parameters->Set("SuperSampling.FeatureInitResult", 1u);
  out_parameters->Set("Snippet.OptLevel", 0);
  out_parameters->Set("Snippet.IsDevBranch", 0);
  out_parameters->Set(NVNGX_Parameter_DLSSOptimalSettingsCallback, (void *)&NVNGX_DLSS_GetOptimalSettingsCallback);
  out_parameters->Set("DLSSGetStatsCallback", (void *)&NVNGX_DLSS_GetStatsCallback);
  out_parameters->Set(NVNGX_Parameter_Sharpness, 0.0f);
  out_parameters->Set(NVNGX_Parameter_MV_Scale_X, 1.0f);
  out_parameters->Set(NVNGX_Parameter_MV_Scale_Y, 1.0f);
  out_parameters->Set("MV.Offset.X", 0.0f);
  out_parameters->Set("MV.Offset.Y", 0.0f);
  out_parameters->Set("DLSS.Exposure.Scale", 1.0f);
  out_parameters->Set(NVNGX_Parameter_PerfQualityValue, 2);

  out_parameters->Set("CreationNodeMask", 1);
  out_parameters->Set("VisibilityNodeMask", 1);
  out_parameters->Set("DLSS.Enable.Output.Subrects", 1);
  out_parameters->Set("RTXValue", 0);

  *out_params = out_parameters;

  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_GetParameters(NVNGXParameter **out_params) {
  // FIXME: leak!
  return NVSDK_NGX_D3D11_GetCapabilityParameters(out_params);
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_GetFeatureRequirements(
    IDXGIAdapter *adapter, const NVNGX_FeatureDiscoveryInfo *discovery_info,
    NVNGX_FeatureRequirement *requirement
) {
  if (!requirement)
    return NVNGX_RESULT_INVALID_PARAMETER;
  if (discovery_info->FeatureID == NVNGX_FEATURE_SUPERSAMPLING) {
    requirement->FeatureSupported = NVNGX_FEATURE_SUPPORT_RESULT_SUPPORTED;
    requirement->MinHWArchitecture = 0;
    strcpy_s(requirement->MinOSVersion, "10.0.16299.0"); // v1709
    return NVNGX_RESULT_OK;
  }
  WARN("NVSDK_NGX_D3D11_GetFeatureRequirements: unsupported feature ", discovery_info->FeatureID);
  requirement->FeatureSupported = NVNGX_FEATURE_SUPPORT_RESULT_UNSUPPORTED;
  return NVNGX_RESULT_FAIL;
}

extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason,
                               LPVOID reserved) {
  if (reason != DLL_PROCESS_ATTACH)
    return TRUE;

  DisableThreadLibraryCalls(instance);
  wchar_t module_path[MAX_PATH] = {};
  GetModuleFileNameW(instance, module_path, MAX_PATH);
  const wchar_t *base = wcsrchr(module_path, L'\\');
  ngx_is_feature_module = !_wcsicmp(base ? base + 1 : module_path, L"nvngx_dlss.dll");
  ngx_trace(ngx_is_feature_module ? "Feature DLL attached (DXMT -> MetalFX)" : "Core DLL attached (DXMT NGX -> MetalFX)");

  if (FAILED(DXGIGetDebugInterface1(0, DXMT_NVEXT_GUID, nullptr))) {
    return FALSE;
  }

  return TRUE;
}

}; // namespace dxmt