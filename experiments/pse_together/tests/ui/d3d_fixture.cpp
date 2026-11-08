// Actual public-API rendering probe in an owned WARP window; never KEX.
#include "native/p23/ui_native.hpp"
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <MinHook.h>
#include <cstdio>
#include <cstdarg>
#include <initializer_list>
using Microsoft::WRL::ComPtr;
int failures{},checks{};
void check(bool ok,const char* label) {++checks;if(!ok){++failures;std::printf("FAIL %s\n",label);}}
void log(const char* event,const char* fmt,...) {std::printf("%s ",event);va_list a;va_start(a,fmt);std::vprintf(fmt,a);va_end(a);std::puts("");}
LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l) {return DefWindowProcW(h,m,w,l);}
struct State {
    ID3D11RasterizerState* raster{};ID3D11BlendState* blend{};ID3D11DepthStencilState* depth{};
    ID3D11VertexShader* vs{};ID3D11PixelShader* ps{};ID3D11RenderTargetView* rtv{};
    D3D11_PRIMITIVE_TOPOLOGY topology{};UINT mask{},ref{};float factor[4]{};
    bool operator==(const State&) const=default;
};
State state(ID3D11DeviceContext* context) {
    State s;context->RSGetState(&s.raster);context->OMGetBlendState(&s.blend,s.factor,&s.mask);context->OMGetDepthStencilState(&s.depth,&s.ref);
    context->VSGetShader(&s.vs,nullptr,nullptr);context->PSGetShader(&s.ps,nullptr,nullptr);context->OMGetRenderTargets(1,&s.rtv,nullptr);context->IAGetPrimitiveTopology(&s.topology);
    for(IUnknown* p:{static_cast<IUnknown*>(s.raster),static_cast<IUnknown*>(s.blend),static_cast<IUnknown*>(s.depth),static_cast<IUnknown*>(s.vs),static_cast<IUnknown*>(s.ps),static_cast<IUnknown*>(s.rtv)})if(p)p->Release();return s;
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=2)return 2;
    check(LoadLibraryW(argv[1])!=nullptr,"load exact SDL fixture dependency");
    check(MH_Initialize()==MH_OK,"MinHook initialized");
    check(pse::native_ui::install(reinterpret_cast<unsigned char*>(1),GetCurrentThreadId(),true,log),"install public hooks only (fixture macro)");
    if(failures)return 1;
    WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"P23OwnedWARPFixture";RegisterClassW(&wc);
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"P23 owned D3D11 fixture",WS_OVERLAPPEDWINDOW,20,20,980,600,nullptr,nullptr,wc.hInstance,nullptr);
    check(window!=nullptr,"owned window");
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
    check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)),"actual WARP device and factory hook");
    if(failures)return 1;
    ComPtr<IDXGIDevice> dx;ComPtr<IDXGIAdapter> adapter;ComPtr<IDXGIFactory> factory;
    device.As(&dx);dx->GetAdapter(&adapter);adapter->GetParent(IID_PPV_ARGS(&factory));
    DXGI_SWAP_CHAIN_DESC desc{};desc.BufferCount=1;desc.BufferDesc.Width=960;desc.BufferDesc.Height=540;desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.OutputWindow=window;desc.SampleDesc.Count=1;desc.Windowed=TRUE;desc.SwapEffect=DXGI_SWAP_EFFECT_SEQUENTIAL;
    ComPtr<IDXGISwapChain> swap;check(SUCCEEDED(factory->CreateSwapChain(device.Get(),&desc,&swap)),"actual owned swapchain");
    if(failures)return 1;pse::native_ui::enableInterface();pse::native_ui::permit();
    for(unsigned frame=0;frame<3;++frame) {
        ComPtr<ID3D11Texture2D> back;swap->GetBuffer(0,IID_PPV_ARGS(&back));ComPtr<ID3D11RenderTargetView> view;device->CreateRenderTargetView(back.Get(),nullptr,&view);
        auto* target=view.Get();context->OMSetRenderTargets(1,&target,nullptr);const float clear[]={0.2f,0.4f,0.6f,1};context->ClearRenderTargetView(target,clear);
        const auto before=state(context.Get());check(SUCCEEDED(swap->Present(0,0)),"Present executes UI overlay");check(before==state(context.Get()),"immediate context selected states preserved");
        D3D11_TEXTURE2D_DESC stagingDesc{};back->GetDesc(&stagingDesc);stagingDesc.Usage=D3D11_USAGE_STAGING;stagingDesc.BindFlags=0;stagingDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;device->CreateTexture2D(&stagingDesc,nullptr,&staging);context->CopyResource(staging.Get(),back.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
        if(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped))) {
            // Status panel sits at bottom-left; sample a plain black area.
            const auto* row=static_cast<const unsigned char*>(mapped.pData)+430*mapped.RowPitch;
            check(row[20*4]<20,"actual overlay changed owned backbuffer pixels");context->Unmap(staging.Get(),0);
        } else check(false,"readback");
        context->OMSetRenderTargets(0,nullptr,nullptr);
    }
    check(SUCCEEDED(swap->ResizeBuffers(1,900,600,DXGI_FORMAT_UNKNOWN,0)),"no retained swapchain-buffer refs across resize");
    pse::native_ui::Observation observation;check(pse::native_ui::observe(&observation,sizeof(observation))==0 && observation.overlays==3 && !observation.errors,"public renderer observation");
    pse::native_ui::stop();check(pse::native_ui::observe(&observation,sizeof(observation))==0 && observation.restores==4 && !observation.errors,"all public hook exact restores (no Arrow in fixture)");
    DestroyWindow(window);std::printf("P23_D3D_FIXTURE checks=%d failures=%d kex=0 warp=1\n",checks,failures);return failures?1:0;
}
