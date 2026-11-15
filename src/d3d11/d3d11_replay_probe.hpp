// Private local ownership experiment. Copyright 2026 Nick Kennedy; LGPL-2.1-or-later.
#pragma once
#include "dxmt_replay_probe.h"
#include <vector>
#include <memory>
#include <unordered_map>

namespace dxmt {
class ReplayProbePacket final : public IDXMTReplayPacket {
  std::atomic<ULONG> refs_{1};
  HRESULT result_ = E_PENDING;
  UINT64 budget_, bytes_ = 0;
  // Pending lists use the existing private refcount, avoiding a device/context
  // ownership cycle while a scope is armed. Public ownership starts at finish.
  Com<ID3D11CommandList> finished_;
  Com<MTLD3D11CommandList, false> list_;
  Com<ID3D11RenderTargetView> original_rtv_, rtv_;
  Com<ID3D11DepthStencilView> original_dsv_, dsv_;
  UINT draws_=0, limit_;
  bool pass_;
  const bool cache_enabled_;
  struct Snapshot {Com<ID3D11Resource> source, copy;};
  std::unordered_map<ID3D11Resource*,Snapshot> snapshots_;
  template<class T> bool cached(T *src,Com<T>&dst) {
    if(!cache_enabled_)return false;
    auto it=snapshots_.find(src);if(it==snapshots_.end())return false;
    // Each resource kind is fixed for a COM resource identity.
    dst=static_cast<T*>(it->second.copy.ptr());return true;
  }
  template<class T> void remember(T *src,T *dst) {
    if(cache_enabled_)snapshots_.insert_or_assign(src,Snapshot{Com<ID3D11Resource>(src),Com<ID3D11Resource>(dst)});
  }
  Com<ID3D11Texture2D> colour_, depth_;
  using Inputs = std::vector<Com<IUnknown>>;
  std::shared_ptr<Inputs> inputs_ = std::make_shared<Inputs>();
  template<class T> void keep(T *p) { if(p) inputs_->emplace_back(p); }
  bool charge(UINT64 n) { if(n > budget_ - bytes_) return false; bytes_ += n; return true; }
  HRESULT buffer(ID3D11Device *dev, ID3D11DeviceContext *ctx, ID3D11Buffer *src, Com<ID3D11Buffer>& dst) {
    if(cached(src,dst))return S_OK;
    D3D11_BUFFER_DESC d={};src->GetDesc(&d);
    if(d.MiscFlags || d.StructureByteStride || !charge(d.ByteWidth)) return DXGI_ERROR_UNSUPPORTED;
    d.Usage=D3D11_USAGE_DEFAULT;d.CPUAccessFlags=0;
    HRESULT hr=dev->CreateBuffer(&d,nullptr,&dst);if(FAILED(hr))return hr;
    keep(dst.ptr());ctx->CopyResource(dst.ptr(),src);remember(src,dst.ptr());return S_OK;
  }
  HRESULT texture(ID3D11Device *dev, ID3D11DeviceContext *ctx, ID3D11Texture2D *src,
      Com<ID3D11Texture2D>& dst, bool depth=false, bool input=false) {
    if(input && cached(src,dst))return S_OK;
    D3D11_TEXTURE2D_DESC d={};src->GetDesc(&d);
    UINT bpp=(d.Format==DXGI_FORMAT_D32_FLOAT_S8X24_UINT || d.Format==DXGI_FORMAT_R32G8X24_TYPELESS)?8:4;
    if(d.MipLevels!=1 || d.ArraySize!=1 || d.MiscFlags || d.SampleDesc.Quality ||
        (d.SampleDesc.Count!=1 && d.SampleDesc.Count!=2) ||
        (d.Format!=DXGI_FORMAT_R8G8B8A8_UNORM && d.Format!=DXGI_FORMAT_R11G11B10_FLOAT &&
         !(depth && (d.Format==DXGI_FORMAT_D32_FLOAT_S8X24_UINT || d.Format==DXGI_FORMAT_R32G8X24_TYPELESS))) ||
        (d.BindFlags&D3D11_BIND_UNORDERED_ACCESS) ||
        !charge(UINT64(d.Width)*d.Height*d.SampleDesc.Count*bpp)) return DXGI_ERROR_UNSUPPORTED;
    d.Usage=D3D11_USAGE_DEFAULT;d.CPUAccessFlags=0;
    HRESULT hr=dev->CreateTexture2D(&d,nullptr,&dst);if(FAILED(hr))return hr;
    keep(dst.ptr());ctx->CopyResource(dst.ptr(),src);if(input)remember(src,dst.ptr());return S_OK;
  }
public:
  explicit ReplayProbePacket(UINT64 budget,UINT limit=1,bool pass=false):budget_(budget),limit_(limit),pass_(pass),cache_enabled_(pass && env::getEnvVar("DXMT_REPLAY_CACHE_INPUTS")=="1"){}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void **out) override {
    if(!out)return E_POINTER;
    *out=nullptr;
    if(iid!=__uuidof(IUnknown) && iid!=DXMT_IID_REPLAY_PACKET)return E_NOINTERFACE;
    *out=static_cast<IDXMTReplayPacket*>(this);AddRef();return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override {return ++refs_;}
  ULONG STDMETHODCALLTYPE Release() override {auto n=--refs_;if(!n)delete this;return n;}
  HRESULT STDMETHODCALLTYPE GetResult(ID3D11CommandList **list,ID3D11Texture2D **colour,
      ID3D11Texture2D **depth,UINT64 *bytes) override {
    if(list)*list=nullptr;
    if(colour)*colour=nullptr;
    if(depth)*depth=nullptr;
    if(bytes)*bytes=bytes_;
    if(result_!=S_OK)return result_;
    if(list)*list=ref(finished_.ptr());
    if(colour)*colour=ref(colour_.ptr());
    if(depth)*depth=ref(depth_.ptr());
    return S_OK;
  }
  bool isPass() const {return pass_;}
  void invalidate(ID3D11Resource *resource) {snapshots_.erase(resource);}
  bool pending() const {return result_==E_PENDING;}
  bool output(ID3D11Resource *resource) const {
    if(!resource)return false;
    Com<ID3D11Resource> r,d;
    if(original_rtv_)original_rtv_->GetResource(&r);
    if(original_dsv_)original_dsv_->GetResource(&d);
    return resource==r.ptr() || resource==d.ptr();
  }
  void fail(HRESULT hr=DXGI_ERROR_UNSUPPORTED) {
    result_=hr;list_=nullptr;finished_=nullptr;snapshots_.clear();inputs_.reset();colour_=nullptr;depth_=nullptr;
    rtv_=nullptr;dsv_=nullptr;original_rtv_=nullptr;original_dsv_=nullptr;
  }
  HRESULT finish() {
    if(!pending())return result_;
    if(!draws_){fail();return result_;}
    finished_=list_.ptr();snapshots_.clear();inputs_.reset();result_=S_OK;return S_OK;
  }
  // Called on the immediate-context owner thread, with its recursive mutex.
  void capture(ID3D11Device *dev, ID3D11DeviceContext1 *ctx, UINT count,UINT start,bool indexed=false,INT base=0) {
    if(draws_==limit_){fail();return;}
    HRESULT hr=build(dev,ctx,count,start,indexed,base);
    if(FAILED(hr)){fail(hr);return;}
    if(env::getEnvVar("DXMT_REPLAY_LIFETIME_DEBUG")=="1") {
      auto n=list_->AddRef();list_->Release();WARN("REPLAY pending list public refs=",n-1);
    }
    ++draws_;if(!pass_)finish();
  }
private:
  HRESULT build(ID3D11Device *dev, ID3D11DeviceContext1 *ctx,UINT count,UINT start,bool indexed,INT base) {
#define REPLAY_TRY(x) do { HRESULT hr=(x);if(FAILED(hr))return hr; } while(0)
    Com<ID3D11DeviceContext> deferred;REPLAY_TRY(dev->CreateDeferredContext(0,&deferred));
    Com<ID3D11DeviceContext1> def;REPLAY_TRY(deferred->QueryInterface(__uuidof(ID3D11DeviceContext1),reinterpret_cast<void**>(&def)));
    Com<ID3D11RenderTargetView> original_rtv;Com<ID3D11DepthStencilView> original_dsv;
    ctx->OMGetRenderTargets(1,&original_rtv,&original_dsv);
    if(!original_rtv)return DXGI_ERROR_UNSUPPORTED;
    Com<ID3D11Resource> r;original_rtv->GetResource(&r);
    Com<ID3D11Texture2D> original;REPLAY_TRY(r->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&original)));
    D3D11_RENDER_TARGET_VIEW_DESC rd={};original_rtv->GetDesc(&rd);
    if((rd.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2D && rd.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2DMS) ||
       (rd.ViewDimension==D3D11_RTV_DIMENSION_TEXTURE2D && rd.Texture2D.MipSlice))return DXGI_ERROR_UNSUPPORTED;
    D3D11_TEXTURE2D_DESC td={};
    if(!draws_) {
      original_rtv_=original_rtv;original_dsv_=original_dsv;
    Com<ID3D11Texture2D> seed;REPLAY_TRY(texture(dev,ctx,original.ptr(),seed));
    original->GetDesc(&td);
    if(!charge(UINT64(td.Width)*td.Height*td.SampleDesc.Count*4))return DXGI_ERROR_UNSUPPORTED;
    td.Usage=D3D11_USAGE_DEFAULT;td.CPUAccessFlags=0;REPLAY_TRY(dev->CreateTexture2D(&td,nullptr,&colour_));keep(colour_.ptr());
    def->CopyResource(colour_.ptr(),seed.ptr());
    REPLAY_TRY(dev->CreateRenderTargetView(colour_.ptr(),&rd,&rtv_));keep(rtv_.ptr());
    if(original_dsv) {
      Com<ID3D11Resource> dr;original_dsv->GetResource(&dr);Com<ID3D11Texture2D> source;
      REPLAY_TRY(dr->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&source)));
      D3D11_DEPTH_STENCIL_VIEW_DESC dd={};original_dsv->GetDesc(&dd);
      if((dd.ViewDimension!=D3D11_DSV_DIMENSION_TEXTURE2D && dd.ViewDimension!=D3D11_DSV_DIMENSION_TEXTURE2DMS) ||
         (dd.ViewDimension==D3D11_DSV_DIMENSION_TEXTURE2D && dd.Texture2D.MipSlice))return DXGI_ERROR_UNSUPPORTED;
      Com<ID3D11Texture2D> depth_seed;REPLAY_TRY(texture(dev,ctx,source.ptr(),depth_seed,true));
      source->GetDesc(&td);if(!charge(UINT64(td.Width)*td.Height*td.SampleDesc.Count*8))return DXGI_ERROR_UNSUPPORTED;
      td.Usage=D3D11_USAGE_DEFAULT;td.CPUAccessFlags=0;REPLAY_TRY(dev->CreateTexture2D(&td,nullptr,&depth_));keep(depth_.ptr());
      def->CopyResource(depth_.ptr(),depth_seed.ptr());REPLAY_TRY(dev->CreateDepthStencilView(depth_.ptr(),&dd,&dsv_));keep(dsv_.ptr());
    }
    } else if(original_rtv.ptr()!=original_rtv_.ptr() || original_dsv.ptr()!=original_dsv_.ptr()) return DXGI_ERROR_UNSUPPORTED;
    ID3D11RenderTargetView *rt=rtv_.ptr();def->OMSetRenderTargets(1,&rt,dsv_.ptr());
    Com<ID3D11VertexShader> vs;Com<ID3D11PixelShader> ps;
    ctx->VSGetShader(&vs,nullptr,nullptr);ctx->PSGetShader(&ps,nullptr,nullptr);
    if(!vs)return DXGI_ERROR_UNSUPPORTED;
    keep(vs.ptr());keep(ps.ptr());def->VSSetShader(vs.ptr(),nullptr,0);def->PSSetShader(ps.ptr(),nullptr,0);
    Com<ID3D11InputLayout> layout;ctx->IAGetInputLayout(&layout);keep(layout.ptr());def->IASetInputLayout(layout.ptr());
    D3D11_PRIMITIVE_TOPOLOGY topology;ctx->IAGetPrimitiveTopology(&topology);def->IASetPrimitiveTopology(topology);
    for(UINT slot=0;slot<D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT;++slot) {
      Com<ID3D11Buffer> src;UINT stride=0,offset=0;ctx->IAGetVertexBuffers(slot,1,&src,&stride,&offset);
      if(src){Com<ID3D11Buffer> dst;REPLAY_TRY(buffer(dev,ctx,src.ptr(),dst));auto p=dst.ptr();def->IASetVertexBuffers(slot,1,&p,&stride,&offset);}
    }
    if(indexed) {
      Com<ID3D11Buffer> source,copy;DXGI_FORMAT format;UINT offset=0;ctx->IAGetIndexBuffer(&source,&format,&offset);
      if(!source || (format!=DXGI_FORMAT_R16_UINT && format!=DXGI_FORMAT_R32_UINT))return DXGI_ERROR_UNSUPPORTED;
      REPLAY_TRY(buffer(dev,ctx,source.ptr(),copy));def->IASetIndexBuffer(copy.ptr(),format,offset);
    }
    for(UINT stage=0;stage<2;++stage) {
      for(UINT slot=0;slot<D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;++slot) {
        Com<ID3D11Buffer> src;UINT first=0,num=0;
        if(stage)ctx->PSGetConstantBuffers1(slot,1,&src,&first,&num);else ctx->VSGetConstantBuffers1(slot,1,&src,&first,&num);
        if(src){Com<ID3D11Buffer> dst;REPLAY_TRY(buffer(dev,ctx,src.ptr(),dst));auto p=dst.ptr();
          if(stage)def->PSSetConstantBuffers1(slot,1,&p,&first,&num);else def->VSSetConstantBuffers1(slot,1,&p,&first,&num);}
      }
      for(UINT slot=0;slot<D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;++slot) {
        Com<ID3D11ShaderResourceView> src;
        if(stage)ctx->PSGetShaderResources(slot,1,&src);else ctx->VSGetShaderResources(slot,1,&src);
        if(!src)continue;
        Com<ID3D11Resource> resource;src->GetResource(&resource);
        // Feedback/attachment aliases and nontrivial views require a later recorder.
        if(resource.ptr()==r.ptr())return DXGI_ERROR_UNSUPPORTED;
        if(original_dsv){Com<ID3D11Resource> dr;original_dsv->GetResource(&dr);if(dr.ptr()==resource.ptr())return DXGI_ERROR_UNSUPPORTED;}
        Com<ID3D11Texture2D> input;REPLAY_TRY(resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&input)));
        D3D11_SHADER_RESOURCE_VIEW_DESC sd={};src->GetDesc(&sd);
        if(sd.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D || sd.Texture2D.MostDetailedMip || sd.Texture2D.MipLevels!=1)return DXGI_ERROR_UNSUPPORTED;
        input->GetDesc(&td);if(td.Format!=DXGI_FORMAT_R8G8B8A8_UNORM || td.SampleDesc.Count!=1 || td.BindFlags!=D3D11_BIND_SHADER_RESOURCE)return DXGI_ERROR_UNSUPPORTED;
        Com<ID3D11Texture2D> copy;REPLAY_TRY(texture(dev,ctx,input.ptr(),copy,false,true));Com<ID3D11ShaderResourceView> view;
        REPLAY_TRY(dev->CreateShaderResourceView(copy.ptr(),&sd,&view));keep(view.ptr());auto p=view.ptr();
        if(stage)def->PSSetShaderResources(slot,1,&p);else def->VSSetShaderResources(slot,1,&p);
      }
      for(UINT slot=0;slot<D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT;++slot) {
        Com<ID3D11SamplerState> sampler;if(stage)ctx->PSGetSamplers(slot,1,&sampler);else ctx->VSGetSamplers(slot,1,&sampler);
        keep(sampler.ptr());auto p=sampler.ptr();if(stage)def->PSSetSamplers(slot,1,&p);else def->VSSetSamplers(slot,1,&p);
      }
    }
    Com<ID3D11RasterizerState> rs;ctx->RSGetState(&rs);keep(rs.ptr());def->RSSetState(rs.ptr());
    UINT n=D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;D3D11_VIEWPORT vp[16]={};ctx->RSGetViewports(&n,vp);def->RSSetViewports(n,vp);
    n=16;D3D11_RECT rect[16]={};ctx->RSGetScissorRects(&n,rect);def->RSSetScissorRects(n,rect);
    Com<ID3D11BlendState> blend;FLOAT factors[4]={};UINT mask=0;ctx->OMGetBlendState(&blend,factors,&mask);keep(blend.ptr());def->OMSetBlendState(blend.ptr(),factors,mask);
    Com<ID3D11DepthStencilState> ds;UINT refval=0;ctx->OMGetDepthStencilState(&ds,&refval);keep(ds.ptr());def->OMSetDepthStencilState(ds.ptr(),refval);
    if(indexed)def->DrawIndexed(count,start,base);else def->Draw(count,start);
    Com<ID3D11CommandList> recorded;REPLAY_TRY(def->FinishCommandList(FALSE,&recorded));
    auto child=static_cast<MTLD3D11CommandList*>(recorded.ptr());
    // Snapshots use only DEFAULT resources and no query commands. Nested
    // execution below is bounded to lists without execution-time remapping.
    if(child->visibility_query_count || !child->issued_event_query.empty() || !child->issued_timestamp_query.empty() ||
       !child->used_dynamic_buffers.empty() || !child->used_dynamic_lineartextures.empty() ||
       !child->read_staging_resources.empty() || !child->written_staging_resources.empty())return DXGI_ERROR_UNSUPPORTED;
    child->Emit([inputs=inputs_](ArgumentEncodingContext&){});
    if(!draws_)list_=child;
    else {
      list_->work+=child->work;list_->promote_flush|=child->promote_flush;
      list_->Emit([owned=Com<MTLD3D11CommandList,false>(child)](ArgumentEncodingContext& enc){owned->Execute(enc);});
    }
    if(env::getEnvVar("DXMT_REPLAY_LIFETIME_DEBUG")=="1") {
      auto a=def->Release();def.takeOwnership();
      auto b=deferred->Release();deferred.takeOwnership();WARN("REPLAY deferred release refs=",a,",",b);
    }
    return S_OK;
#undef REPLAY_TRY
  }
};
}
