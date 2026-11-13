// P23 native UI: public D3D11/SDL interfaces + the exact-build Arrow seam.
// Own GDI text bitmap -> own deferred D3D11 command list. ExecuteCommandList(TRUE)
// preserves the game's immediate context. No engine font/Actor/Player writes.
#include "ui_native.hpp"
#include "ui/shared_p23.hpp"
#include "ui/lobby.hpp"
#include "ui/discovery.hpp"
#include "ui/upnp.hpp"
#include "ui/opening_text.hpp"
#include <MinHook.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <intrin.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <string>
#include <vector>
#include <cstring>

namespace pse::native_ui {
namespace {
using Microsoft::WRL::ComPtr;
// Local projection of SDL2.0.14 public event layouts, not a game structure.
// https://github.com/libsdl-org/SDL/blob/release-2.0.14/include/SDL_events.h
struct Keysym {std::int32_t scancode,sym;std::uint16_t mod;std::uint16_t padding;std::uint32_t unused;};
struct KeyEvent {std::uint32_t type,time,window;std::uint8_t state,repeat,pad1,pad2;Keysym keysym;};
struct TextEvent {std::uint32_t type,time,window;char text[32];};
struct MouseEvent {std::uint32_t type,time,window,which;std::uint8_t button,state,clicks,pad;std::int32_t x,y;};
struct WindowEvent {std::uint32_t type,time,window;std::uint8_t event,pad1,pad2,pad3;std::int32_t a,b;};
struct WMInfo {
    std::uint8_t version[3]{2,0,14},padding{};std::uint32_t subsystem{};
    union {struct {HWND window;HDC dc;HINSTANCE instance;} win;std::uint8_t bytes[64];} info{};
};
static_assert(sizeof(WMInfo)==72 && offsetof(WMInfo,info)==8);
union alignas(8) Event {std::uint32_t type;KeyEvent key;TextEvent text;MouseEvent mouse;WindowEvent window;unsigned char padding[56];};
static_assert(sizeof(Event)==56 && offsetof(KeyEvent,keysym)==16 && offsetof(TextEvent,text)==12 && offsetof(MouseEvent,x)==20);
using Poll=int(__cdecl*)(Event*);
using Void=void(__cdecl*)();
using Flag=int(__cdecl*)();
using SetFlag=int(__cdecl*)(int);
using WindowById=void*(__cdecl*)(std::uint32_t);
using GetWMInfo=int(__cdecl*)(void*,WMInfo*);
using CreateDevice=HRESULT(WINAPI*)(IDXGIAdapter*,D3D_DRIVER_TYPE,HMODULE,UINT,const D3D_FEATURE_LEVEL*,UINT,UINT,ID3D11Device**,D3D_FEATURE_LEVEL*,ID3D11DeviceContext**);
using CreateSwap=HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*,IUnknown*,DXGI_SWAP_CHAIN_DESC*,IDXGISwapChain**);
using Present=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*,UINT,UINT);
using Arrow=void(__fastcall*)(void*,char,float,const float*,float,const std::uint8_t*);
struct Hook {void* site{};std::atomic<void*> original{};std::array<unsigned char,40> bytes{};bool enabled{};};
enum Site:unsigned {DeviceSite,SwapSite,PresentSite,PollSite,ArrowSite,Count};
struct Context {
    unsigned char* base{};DWORD tid{};Logger log{};
    Hook hooks[Count];
    std::atomic<bool> stopped{};
    std::atomic<DWORD> renderTid{};
    std::atomic<std::uint64_t> presents{},overlays{},arrows{},markers{},menus{},submitted{},chats{},restores{},errors{};
    std::atomic<std::uint64_t> mapAt{};
    std::uint32_t epoch{},generation{};std::uint8_t local=255; // Sim-only native admission tuple
    std::mutex uiMutex;
    bool menu{},composer{},board{},textLease{},textWasActive{};int mouseWasRelative{},field{};
    ui::OpeningText openingText;
    ui::Menu form;
    // Lobbies of the local network (LAN / Radmin VPN): the host announces itself, the client lists what it hears (src/ui/discovery.hpp).
    discovery::Browser browser;discovery::Advertiser advertiser;
    std::vector<discovery::Lobby> lobbies;std::vector<std::string> addresses;int pick{};bool autoLobby{};
    upnp::PortMapper mapper;int mapperLogged=-1; // host: router port for direct internet joins (src/ui/upnp.hpp)
    std::string draft,status="F7: Exhumed Together menu";
    std::size_t activityKey{};std::uint64_t showUntil{};bool activityInit{}; // chat / notices stay on screen a few seconds after they change
    Void textStart{},textStop{};Flag textActive{},relative{};SetFlag relativeSet{};
    WindowById windowById{};GetWMInfo windowInfo{};
    std::atomic<IDXGISwapChain*> chain{};std::atomic<HWND> hwnd{};
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> deferred;
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11InputLayout> layout;ComPtr<ID3D11Buffer> vertices;
    ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11ShaderResourceView> textureView;
    ComPtr<ID3D11SamplerState> sampler;ComPtr<ID3D11BlendState> blend;
    ComPtr<ID3D11DepthStencilState> depth;ComPtr<ID3D11RasterizerState> raster;
    HDC dc{};HBITMAP bitmap{};HFONT font{};void* pixels{};
};
struct ContextPointer {
    std::atomic<Context*> value{};
    Context* operator->() const {return value.load(std::memory_order_acquire);}
    explicit operator bool() const {return value.load(std::memory_order_acquire)!=nullptr;}
    void operator=(Context* p) {value.store(p,std::memory_order_release);}
} c;
std::atomic<bool> installed{};
constexpr int Width=620,Height=500;
bool readable(const void* begin,std::size_t count) {
    auto current=reinterpret_cast<std::uintptr_t>(begin);
    if(current>UINTPTR_MAX-count)return false;const auto end=current+count;
    while(current<end) {
        MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery(reinterpret_cast<void*>(current),&info,sizeof(info)) || info.State!=MEM_COMMIT ||
            (info.Protect&(PAGE_GUARD|PAGE_NOACCESS)) || !(info.Protect&(PAGE_READONLY|PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))return false;
        const auto next=reinterpret_cast<std::uintptr_t>(info.BaseAddress)+info.RegionSize;
        if(next<=current)return false;current=(std::min)(next,end);
    }
    return true;
}
[[maybe_unused]] constexpr std::array<unsigned char,16> ArrowPrefix{0x48,0x8b,0xc4,0x48,0x89,0x58,0x08,0x48,0x89,0x68,0x10,0x48,0x89,0x70,0x18,0x48};
bool hook(Site index,void* site,void* detour) {
    auto& h=c->hooks[index];
    if(h.site)return h.site==site && h.enabled;
    MEMORY_BASIC_INFORMATION info{};
    if(!VirtualQuery(site,&info,sizeof(info)) || info.State!=MEM_COMMIT || !(info.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))return false;
    if(!readable(static_cast<unsigned char*>(site)-8,40))return false;
    h.site=site;std::copy_n(static_cast<unsigned char*>(site)-8,40,h.bytes.begin());
    void* original{};
    if(MH_CreateHook(site,detour,&original)!=MH_OK) {++c->errors;return false;}
    h.original.store(original,std::memory_order_release);
    if(MH_EnableHook(site)!=MH_OK) {++c->errors;return false;}
    h.enabled=true;c->log("p23_hook","\"site\":%u,\"address\":\"%p\",\"pinned\":true",index,site);return true;
}
std::wstring wide(const std::string& s) {
    const auto n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);
    if(n<=0)return L"";std::wstring out(static_cast<std::size_t>(n),L' ');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),out.data(),n);return out;
}
void eraseUtf8(std::string& s) {
    if(s.empty())return;auto i=s.size()-1;while(i && (static_cast<unsigned char>(s[i])&0xc0)==0x80)--i;s.resize(i);
}
void inputLease(bool active,bool restoreMouse=true) {
    if(active==c->textLease)return;
    if(active) {c->textWasActive=c->textActive()!=0;c->mouseWasRelative=c->relative();c->textStart();c->relativeSet(0);}
    else {
        if(!c->textWasActive && c->textActive())c->textStop();
        DWORD foreground{};GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
        if(restoreMouse && foreground==GetCurrentProcessId() && c->relative()==0)c->relativeSet(c->mouseWasRelative);
    }
    c->textLease=active;ui::shared().capture.store(active,std::memory_order_release);
}
std::vector<std::string> addressesNow() {
    std::vector<std::string> out;for(const auto& i:discovery::interfaces())out.push_back(i.address);return out;
}
// Direct join (as in the 2015 co-op mod): the address field takes "1.2.3.4", "1.2.3.4:28745" or a DNS name (resolved here, IPv4).
bool directAddress(ui::Menu& form) {
    std::string a=form.address;
    while(!a.empty() && (a.back()==' ' || a.back()=='/'))a.pop_back();
    while(!a.empty() && a.front()==' ')a.erase(0,1);
    if(const auto colon=a.rfind(':');colon!=std::string::npos) {
        if(!ui::port(a.substr(colon+1)))return false;
        form.portText=a.substr(colon+1);a.resize(colon);
    }
    if(a.empty() || a.size()>63)return false;
    if(!ui::ipv4(a)) {
        if(!discovery::winsock())return false;
        addrinfo hints{},*res{};hints.ai_family=AF_INET;hints.ai_socktype=SOCK_DGRAM;
        if(getaddrinfo(a.c_str(),nullptr,&hints,&res)!=0 || !res)return false;
        char ip[16]{};inet_ntop(AF_INET,&reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr,ip,sizeof(ip));freeaddrinfo(res);
        a=ip;
    }
    form.address=a;return ui::ipv4(a);
}
bool onList() {return c->menu && !c->composer && !c->form.host && c->field==4;}
std::string& currentField() {
    if(c->composer)return c->draft;
    switch(c->field) {case 0:return c->form.displayName;case 1:return c->form.address;case 2:return c->form.portText;default:return c->form.token;}
}
void submit() {
    if(c->composer) {
        if(ui::shared().say(c->draft)) {++c->chats;c->draft.clear();c->composer=false;inputLease(false);c->status="Message sent";}
        else c->status="Chat: enter 1-90 characters";
        return;
    }
    if(c->form.host) {c->menu=false;inputLease(false);return;} // the host's lobby is already open: Enter just closes the screen
    if(onList()) {   // a lobby of the list: its address and port go into the form, then the usual connect
        if(c->lobbies.empty()) {c->status="No lobby found yet";return;}
        const auto& l=c->lobbies[static_cast<std::size_t>((std::min)(c->pick,static_cast<int>(c->lobbies.size())-1))];
        if(!l.open) {c->status="Host is not in a level yet";return;}
        c->form.address=l.address;c->form.portText=std::to_string(l.port);
    } else if(!directAddress(c->form)) {c->status="Address: IPv4, IPv4:port or a host name";return;}
    if(ui::shared().submit(c->form)) {
        ++c->submitted;c->menu=false;inputLease(false);c->status="Load the same level as the host";
        c->log("p23_menu_submit","\"host\":%s,\"name_bytes\":%zu,\"port\":%u",c->form.host?"true":"false",c->form.displayName.size(),*ui::port(c->form.portText));
    } else c->status=ui::shared().stopping.load()?"Session ended. Restart game for a new session":ui::shared().requested.load()?"Session already started":"Check player name, IPv4 and port";
}
bool input(Event& e) {
    if(GetCurrentThreadId()!=c->tid || !ui::shared().interfaceEnabled.load() || c->stopped.load() || !c->hwnd)return false;
    if(e.type!=0x200 && (e.type<0x300 || e.type>0x403))return false;
    WMInfo info;void* window=c->windowById(e.key.window);
    if(!window || !c->windowInfo(window,&info) || info.subsystem!=1 || info.info.win.window!=c->hwnd.load())return false;
    std::lock_guard lock(c->uiMutex);
    if(e.type==0x200 && (e.window.event==13 || e.window.event==14)) {
        c->menu=c->composer=c->board=false;c->openingText.reset();inputLease(false,false);return false;
    }
    DWORD foreground{};GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    if(foreground!=GetCurrentProcessId()) {
        if(c->textLease) {c->menu=c->composer=c->board=false;c->openingText.reset();inputLease(false,false);}return false;
    }
    if(e.type==0x301 && e.key.keysym.sym=='\\') {c->board=false;return true;}
    // SDL may enqueue the layout's character for the opening T key. Ignore its
    // text until key-up, forwarding that release so gameplay holds still clear.
    if(e.type==0x301 && e.key.keysym.sym=='t')c->openingText.release(e.key.time);
    if(e.type==0x300) {
        const auto key=e.key.keysym.sym;
        if(!e.key.repeat && key==((1<<30)|64)) { // SDL_SCANCODE_F7=64
            c->menu=!c->menu;c->composer=false;c->openingText.reset();if(c->menu)++c->menus;inputLease(c->menu);
            if(c->menu) {   // opens on the list of lobbies when there is one; the host sees its own lobby
                c->field=(!c->form.host && !c->lobbies.empty())?4:0;if(c->form.host)c->addresses=addressesNow();
                if(!c->form.host && c->form.address!="127.0.0.1")c->field=1; // join.bat <address> / an address typed before
            }
            return true;
        }
        if(!c->menu && !c->composer && key=='\\') {c->board=true;return true;}
        if(!c->menu && !c->composer && key=='t' && ui::shared().requested.load()) {c->composer=true;c->openingText.open();inputLease(true);return true;}
        if(c->menu || c->composer) {
            if(!e.key.repeat)c->openingText.nextKey();
            if(key==27) {c->menu=c->composer=false;inputLease(false);}
            else if(key==9 && c->menu)c->field=(c->field+1)%((!c->form.host && !c->lobbies.empty())?5:4);
            else if(key==((1<<30)|82) && onList()) {if(c->pick>0)--c->pick;}                                   // Up
            else if(key==((1<<30)|81) && onList()) {if(c->pick+1<static_cast<int>(c->lobbies.size()))++c->pick;} // Down
            else if(key==8 && !onList())eraseUtf8(currentField());
            else if(key==13 && !e.key.repeat)submit();
            return true;
        }
    }
    if(e.type==0x303 && (c->menu || c->composer)) {
        if(!c->openingText.accept(e.text.time) || onList())return true;
        const auto length=strnlen_s(e.text.text,32);
        if(length<32) {auto& s=currentField();const auto cap=c->composer?ui::ChatBytes:c->field==0?31u:c->field==3?64u:64u;if(s.size()+length<=cap)s.append(e.text.text,length);}
        return true;
    }
    if(e.type==0x401 && c->menu && e.mouse.button==1) {
        RECT client{};GetClientRect(c->hwnd,&client);
        const int sx=e.mouse.x-16,sy=e.mouse.y-16;
        if(!c->form.host) {   // client layout: lobby rows 64..184, fields from 195, connect button 360
            if(sx>=0 && sx<Width && sy>=64 && sy<64+4*30 && !c->lobbies.empty()) {
                const int row=(sy-64)/30;if(row<static_cast<int>(c->lobbies.size())) {c->pick=row;c->field=4;}
            }
            else if(sx>=0 && sx<Width && sy>=195 && sy<195+4*40)c->field=(sy-195)/40;
            else if(sx>=16 && sx<400 && sy>=360 && sy<400)submit();
        } else if(sx>=0 && sx<Width && sy>=65 && sy<65+4*48)c->field=(sy-65)/48;
        else if(sx>=16 && sx<400 && sy>=370 && sy<410)submit();
        return true;
    }
    // Forward key-up to release preexisting game holds; consume motion and new
    // presses during editing. Never change a native Player input structure.
    return (c->menu || c->composer) && (e.type==0x400 || e.type==0x401 || e.type==0x403 || e.type==0x302);
}
int __cdecl poll(Event* event) {
    auto original=reinterpret_cast<Poll>(c->hooks[PollSite].original.load(std::memory_order_acquire));
    if(!event || c->stopped.load())return original(event);
    int result{};while((result=original(event))!=0) {if(!input(*event))return result;}return result;
}
void text(int x,int y,const std::wstring& s,COLORREF color=RGB(238,238,225)) {
    SetTextColor(c->dc,color);RECT r{x,y,Width-16,Height-8};DrawTextW(c->dc,s.c_str(),static_cast<int>(s.size()),&r,DT_LEFT|DT_TOP|DT_NOPREFIX|DT_WORDBREAK);
}
bool graphics(IDXGISwapChain* chain) {
    if(c->device)return true;
    if(FAILED(chain->GetDevice(IID_PPV_ARGS(&c->device))) || FAILED(c->device->CreateDeferredContext(0,&c->deferred)))return false;
    const char shader[]=R"(
Texture2D image:register(t0);SamplerState samp:register(s0);
struct V{float2 p:POSITION;float2 uv:TEXCOORD;};
struct O{float4 p:SV_POSITION;float2 uv:TEXCOORD;};
O vs_main(V v){O o;o.p=float4(v.p,0,1);o.uv=v.uv;return o;}
float4 ps_main(O v):SV_TARGET{float3 col=image.Sample(samp,v.uv).rgb;return float4(col,0.94);}
)";
    ComPtr<ID3DBlob> vs,ps,err;
    if(FAILED(D3DCompile(shader,sizeof(shader),nullptr,nullptr,nullptr,"vs_main","vs_4_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&vs,&err)) ||
       FAILED(D3DCompile(shader,sizeof(shader),nullptr,nullptr,nullptr,"ps_main","ps_4_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&ps,&err)))return false;
    if(FAILED(c->device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&c->vs)) ||
       FAILED(c->device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&c->ps)))return false;
    const D3D11_INPUT_ELEMENT_DESC elements[]={{"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,8,D3D11_INPUT_PER_VERTEX_DATA,0}};
    if(FAILED(c->device->CreateInputLayout(elements,2,vs->GetBufferPointer(),vs->GetBufferSize(),&c->layout)))return false;
    D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=6*4*sizeof(float);buffer.Usage=D3D11_USAGE_DYNAMIC;buffer.BindFlags=D3D11_BIND_VERTEX_BUFFER;buffer.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    if(FAILED(c->device->CreateBuffer(&buffer,nullptr,&c->vertices)))return false;
    D3D11_TEXTURE2D_DESC tex{};tex.Width=Width;tex.Height=Height;tex.MipLevels=1;tex.ArraySize=1;tex.Format=DXGI_FORMAT_B8G8R8A8_UNORM;tex.SampleDesc.Count=1;tex.Usage=D3D11_USAGE_DEFAULT;tex.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    if(FAILED(c->device->CreateTexture2D(&tex,nullptr,&c->texture)) || FAILED(c->device->CreateShaderResourceView(c->texture.Get(),nullptr,&c->textureView)))return false;
    D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sampler.MaxLOD=D3D11_FLOAT32_MAX;
    D3D11_BLEND_DESC blend{};auto& target=blend.RenderTarget[0];target.BlendEnable=TRUE;target.SrcBlend=D3D11_BLEND_SRC_ALPHA;target.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;target.BlendOp=D3D11_BLEND_OP_ADD;target.SrcBlendAlpha=D3D11_BLEND_ONE;target.DestBlendAlpha=D3D11_BLEND_ZERO;target.BlendOpAlpha=D3D11_BLEND_OP_ADD;target.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    D3D11_DEPTH_STENCIL_DESC depth{};depth.DepthEnable=FALSE;depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;depth.DepthFunc=D3D11_COMPARISON_ALWAYS;
    D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
    if(FAILED(c->device->CreateSamplerState(&sampler,&c->sampler)) || FAILED(c->device->CreateBlendState(&blend,&c->blend)) ||
       FAILED(c->device->CreateDepthStencilState(&depth,&c->depth)) || FAILED(c->device->CreateRasterizerState(&raster,&c->raster)))return false;
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=Width;info.bmiHeader.biHeight=-Height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    c->dc=CreateCompatibleDC(nullptr);c->bitmap=CreateDIBSection(c->dc,&info,DIB_RGB_COLORS,&c->pixels,nullptr,0);
    if(!c->dc || !c->bitmap || !c->pixels)return false;
    SelectObject(c->dc,c->bitmap);c->font=CreateFontW(-21,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,NONANTIALIASED_QUALITY,FIXED_PITCH,L"Consolas");
    if(!c->font)return false;SelectObject(c->dc,c->font);SetBkMode(c->dc,TRANSPARENT);
    c->log("p23_graphics_ready","\"deferred\":true,\"restore_context\":true,\"native_font_calls\":0");return true;
}
void draw(IDXGISwapChain* chain) {
    const auto view=ui::shared().read();
    std::lock_guard lock(c->uiMutex);
    // Draw a compact status/chat HUD even when the menu is closed.
    PatBlt(c->dc,0,0,Width,Height,BLACKNESS);
    int panelHeight=100,stripW=Width;bool pauseHint=false;
    // Main menu (game state 1): a small note bottom right, above the player's name, tells that the mod is loaded and how to open its menu.
    const bool title=*reinterpret_cast<const int*>(c->base+0xaf33a0+0x24)==1 && !c->menu && !c->board && !c->composer;
    if(title) {
        // Two single lines in the usual overlay font; the strip is cut to the text (not the whole 620 px panel).
        panelHeight=60;
        const std::wstring a=L"Exhumed Together",b=L"F7 for Options";SIZE sa{},sb{};
        GetTextExtentPoint32W(c->dc,a.c_str(),static_cast<int>(a.size()),&sa);GetTextExtentPoint32W(c->dc,b.c_str(),static_cast<int>(b.size()),&sb);
        stripW=(std::min)(Width,(std::max)(static_cast<int>(sa.cx),static_cast<int>(sb.cx))+20);
        text(10,4,a,RGB(255,210,100));
        text(10,30,b);
    } else if(c->menu && c->form.host) {
        // The host's lobby is created by itself when the game starts and announced to the local network (LAN, Radmin VPN, ...).
        panelHeight=470;text(16,12,L"POWERSLAVE EXHUMED TOGETHER",RGB(255,210,100));
        text(16,40,L"You are the HOST - your lobby is open");
        text(16,78,L"Lobby:   "+wide(c->form.displayName),RGB(120,240,255));
        text(16,112,L"Players: "+std::to_wstring((std::max)(1u,static_cast<unsigned>(std::count_if(view.players().begin(),view.players().end(),[](const auto& p) {return p.has_value();}))))+L" / "+std::to_wstring(ui::Capacity));
        text(16,146,std::wstring(L"State:   ")+(ui::shared().requested.load()?L"announced in your network":L"starting..."));
        std::wstring addr=L"LAN / Radmin: ";for(const auto& a:c->addresses)addr+=wide(a)+L"   ";
        if(c->addresses.empty())addr+=L"none found";
        text(16,180,addr);
        // Direct internet join: the router's public address when UPnP opened the port, otherwise what to do about it.
        const auto ext=c->mapper.external();const auto port=c->form.portText;
        if(!ext.empty())text(16,240,L"Internet: "+wide(ext)+L":"+wide(port)+L"  (router port open)",RGB(130,240,180));
        else text(16,240,L"Internet: "+wide(c->mapper.message().empty()?std::string("UPnP not started"):c->mapper.message())+
                         (c->mapper.state()==upnp::State::Failed?L". Or forward UDP "+wide(port)+L" by hand":L""),RGB(200,200,190));
        text(16,320,L"Friends: F7, pick this lobby or type your address, Enter",RGB(255,210,100));
        text(16,378,L"Enter / Esc / F7: close");
    } else if(c->menu) {
        panelHeight=470;text(16,12,L"POWERSLAVE EXHUMED TOGETHER",RGB(255,210,100));
        text(16,40,L"Lobbies in your network (LAN / Radmin VPN)");
        if(c->lobbies.empty())text(16,68,L"  searching... (host must run the mod)",RGB(180,180,170));
        for(std::size_t i=0;i<c->lobbies.size() && i<4;++i) {
            const auto& l=c->lobbies[i];const bool sel=c->field==4 && c->pick==static_cast<int>(i);
            const std::wstring row=std::wstring(sel?L"> ":L"  ")+wide(l.host)+L"   "+std::to_wstring(l.players)+L"/"+std::to_wstring(l.capacity)+L"   "+(l.open?L"open":L"starting")+L"   "+wide(l.address)+L":"+std::to_wstring(l.port);
            text(16,68+static_cast<int>(i)*30,row,sel?RGB(120,240,255):l.open?RGB(238,238,225):RGB(170,170,160));
        }
        const std::wstring labels[]={L"Player",L"Host IP[:port]",L"Port",L"Token"};
        const std::string values[]={c->form.displayName,c->form.address,c->form.portText,std::string(c->form.token.size(),'*')};
        for(int i=0;i<4;++i)text(16,200+i*40,(c->field==i?L"> ":L"  ")+labels[i]+L": "+wide(values[i]),c->field==i?RGB(120,240,255):RGB(238,238,225));
        text(16,366,c->field==4?L"[ Enter: JOIN the chosen lobby ]":L"[ Enter / click: CONNECT to the typed address ]",RGB(255,210,100));
        text(16,404,L"Up/Down: lobby   Tab: next field   Esc/F7: close");
    } else if(c->board) {
        panelHeight=340;text(16,12,L"COOPERATIVE PLAYERS",RGB(255,210,100));int y=48;
        for(const auto& p:view.players())if(p) {
            const auto state=p->state==ui::State::Dead?L"DEAD (Press E)":p->state==ui::State::Waiting?L"NEXT LEVEL":L"ALIVE";
            text(16,y,L"P"+std::to_wstring(p->slot+1)+L" "+wide(p->displayName)+L"  "+(p->slot==0?L"HOST ":L"")+state+L"  "+std::to_wstring(p->ping)+L"ms");y+=34;
        }
    } else {
        // In the game the panel is hidden: the hint line (F7 / players / chat) appears only while the game's own Esc menu is open.
        // New chat lines and notices stay up for a few seconds; the composer (T) keeps the panel while typing.
        const bool pause=ui::shared().paused.load(std::memory_order_acquire);
        const auto& notices=view.notices();const auto& lines=view.messages();const auto now=ui::nowMs();
        const std::size_t key=notices.size()*1315423911u+(notices.empty()?0:std::hash<std::string>{}(notices.back()))+(lines.empty()?0:static_cast<std::size_t>(lines.back().sequence)*2654435761u);
        if(key!=c->activityKey) {c->activityKey=key;if(c->activityInit)c->showUntil=now+8000;c->activityInit=true;}
        const bool recent=now<c->showUntil;
        if(!pause && !recent && !c->composer)return; // nothing to show: no panel at all
        int y=10;pauseHint=pause;
        if(pause) {text(16,y,(!c->form.host && !ui::shared().requested.load() && !c->lobbies.empty())?(L"F7: JOIN a lobby ("+std::to_wstring(c->lobbies.size())+L" found)   \\ Players   T Chat"):std::wstring(L"F7 Exhumed Together menu   \\ Players   T Chat"),RGB(255,210,100));y=40;}
        if(!notices.empty()){text(16,y,wide(notices.back()),RGB(130,240,180));y+=28;}
        const std::size_t count=ui::nowMs()-c->mapAt.load()<100?2:4;
        const auto start=lines.size()>count?lines.size()-count:0;
        for(std::size_t i=start;i<lines.size();++i) {text(16,y,wide(lines[i].displayName+": "+lines[i].message));y+=46;}
        if(c->composer) {text(16,y,wide("> "+c->draft+"_"),RGB(120,240,255));y+=52;}
        panelHeight=(std::max)(pause?100:60,y+8);
        if(pause && view.players()[view.local()<8?view.local():0]==std::nullopt)text(16,panelHeight-30,wide(c->status),RGB(255,190,130));
    }
    if(c->menu)text(16,c->form.host?420:440,wide(c->status),RGB(255,190,130));
    GdiFlush();
    DXGI_SWAP_CHAIN_DESC desc{};if(FAILED(chain->GetDesc(&desc)))return;
    ComPtr<ID3D11Texture2D> back;ComPtr<ID3D11RenderTargetView> rtv;
    if(FAILED(chain->GetBuffer(0,IID_PPV_ARGS(&back))) || FAILED(c->device->CreateRenderTargetView(back.Get(),nullptr,&rtv)))return;
    D3D11_TEXTURE2D_DESC bd{};back->GetDesc(&bd);if(bd.Width<1 || bd.Height<1)return;
    const int yOffset=title?(std::max)(16,static_cast<int>(bd.Height)-panelHeight-static_cast<int>(bd.Height/5)):16; // notices and chat: top left, clear of the ammo/health bars
    const float left=title?1-2.0f*(stripW+14)/static_cast<float>(bd.Width):-1+32.0f/static_cast<float>(bd.Width),top=1-2.0f*static_cast<float>(yOffset)/static_cast<float>(bd.Height);
    const float u1=static_cast<float>(stripW)/Width,right=left+2.0f*stripW/static_cast<float>(bd.Width),bottom=top-2.0f*static_cast<float>(panelHeight)/static_cast<float>(bd.Height);
    const float v=static_cast<float>(panelHeight)/Height;
    const float vertices[]={left,top,0,0,right,top,u1,0,left,bottom,0,v,left,bottom,0,v,right,top,u1,0,right,bottom,u1,v};
    D3D11_MAPPED_SUBRESOURCE mapped{};if(FAILED(c->deferred->Map(c->vertices.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))return;
    std::copy(std::begin(vertices),std::end(vertices),static_cast<float*>(mapped.pData));c->deferred->Unmap(c->vertices.Get(),0);
    c->deferred->UpdateSubresource(c->texture.Get(),0,nullptr,c->pixels,Width*4,0);
    auto* target=rtv.Get();c->deferred->OMSetRenderTargets(1,&target,nullptr);
    const D3D11_VIEWPORT viewport{0,0,static_cast<float>(bd.Width),static_cast<float>(bd.Height),0,1};c->deferred->RSSetViewports(1,&viewport);
    c->deferred->RSSetState(c->raster.Get());c->deferred->OMSetDepthStencilState(c->depth.Get(),0);
    c->deferred->OMSetBlendState(c->blend.Get(),nullptr,0xffffffff);c->deferred->IASetInputLayout(c->layout.Get());
    auto* buffer=c->vertices.Get();const UINT stride=16,offset=0;c->deferred->IASetVertexBuffers(0,1,&buffer,&stride,&offset);
    c->deferred->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->deferred->VSSetShader(c->vs.Get(),nullptr,0);c->deferred->PSSetShader(c->ps.Get(),nullptr,0);
    auto* resource=c->textureView.Get();auto* sampler=c->sampler.Get();c->deferred->PSSetShaderResources(0,1,&resource);c->deferred->PSSetSamplers(0,1,&sampler);
    c->deferred->Draw(6,0);ComPtr<ID3D11CommandList> commands;
    if(FAILED(c->deferred->FinishCommandList(FALSE,&commands)))return;
    ComPtr<ID3D11DeviceContext> immediate;c->device->GetImmediateContext(&immediate);
    immediate->ExecuteCommandList(commands.Get(),TRUE);++c->overlays;
    if(c->overlays<=2 || c->overlays%180==0)c->log("p23_overlay","\"frames\":%llu,\"menu\":%s,\"chat\":%s,\"board\":%s,\"restore_context\":true",c->overlays.load(),c->menu?"true":"false",c->composer?"true":"false",c->board?"true":"false");
}
HRESULT STDMETHODCALLTYPE present(IDXGISwapChain* chain,UINT sync,UINT flags) {
    auto original=reinterpret_cast<Present>(c->hooks[PresentSite].original.load(std::memory_order_acquire));
    if(GetCurrentThreadId()==c->tid && !c->stopped.load() && chain==c->chain && !(flags&DXGI_PRESENT_TEST)) {
        ++c->presents;DWORD zero{};c->renderTid.compare_exchange_strong(zero,GetCurrentThreadId());
        if(c->renderTid.load()==GetCurrentThreadId() && ui::shared().interfaceEnabled.load()) {
            if(graphics(chain))draw(chain);else {++c->errors;c->log("p23_graphics_failed","");c->stopped.store(true);}
        }
    }
    return original(chain,sync,flags);
}
HRESULT STDMETHODCALLTYPE createSwap(IDXGIFactory* self,IUnknown* device,DXGI_SWAP_CHAIN_DESC* desc,IDXGISwapChain** out) {
    const auto result=reinterpret_cast<CreateSwap>(c->hooks[SwapSite].original.load(std::memory_order_acquire))(self,device,desc,out);
    if(SUCCEEDED(result) && out && *out && GetCurrentThreadId()==c->tid && !c->stopped.load()) {
        DWORD pid{};if(desc && desc->OutputWindow)GetWindowThreadProcessId(desc->OutputWindow,&pid);
        if(pid==GetCurrentProcessId()) {
            c->chain=*out;c->hwnd=desc->OutputWindow;
            hook(PresentSite,(*reinterpret_cast<void***>(*out))[8],reinterpret_cast<void*>(&present));
            c->log("p23_swapchain","\"hwnd\":\"%p\",\"tid\":%lu",c->hwnd.load(),GetCurrentThreadId());
        }
    }
    return result;
}
HRESULT WINAPI createDevice(IDXGIAdapter* adapter,D3D_DRIVER_TYPE type,HMODULE software,UINT flags,const D3D_FEATURE_LEVEL* levels,UINT count,UINT sdk,ID3D11Device** device,D3D_FEATURE_LEVEL* feature,ID3D11DeviceContext** context) {
    const auto result=reinterpret_cast<CreateDevice>(c->hooks[DeviceSite].original.load(std::memory_order_acquire))(adapter,type,software,flags,levels,count,sdk,device,feature,context);
    if(SUCCEEDED(result) && device && *device && GetCurrentThreadId()==c->tid && !c->stopped.load()) {
        ComPtr<IDXGIDevice> dx;ComPtr<IDXGIAdapter> a;ComPtr<IDXGIFactory> factory;
        if(SUCCEEDED((*device)->QueryInterface(IID_PPV_ARGS(&dx))) && SUCCEEDED(dx->GetAdapter(&a)) && SUCCEEDED(a->GetParent(IID_PPV_ARGS(&factory))))
            hook(SwapSite,(*reinterpret_cast<void***>(factory.Get()))[10],reinterpret_cast<void*>(&createSwap));
    }
    return result;
}
[[maybe_unused]] __declspec(noinline) void __fastcall arrow(void* self,char mode,float yaw,const float* xy,float radius,const std::uint8_t* rgba) {
    const auto caller=_ReturnAddress();auto original=reinterpret_cast<Arrow>(c->hooks[ArrowSite].original.load(std::memory_order_acquire));
    original(self,mode,yaw,xy,radius,rgba);
    if(c->stopped.load() || GetCurrentThreadId()!=c->tid || caller!=c->base+0x1a3271 || !ui::shared().ready.load() || !ui::shared().permitted.load())return;
    ++c->arrows;const auto now=ui::nowMs();c->mapAt.store(now);const auto view=ui::shared().read();const auto z=view.localHeight(now);if(!z)return;
    if(!view.admitted(c->epoch,c->local,c->generation))return;
    const auto sampleNow=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    // Host sampleNs is from the host's steady clock. Only the host may compare
    // that absolute value; clients expire the newest sequenced pose using their
    // own receive clock. No assumption of synchronized LAN computer clocks.
    const auto markers=view.markers(now,*z,true,c->form.host?sampleNow:0);
    for(const auto& m:markers) {
        const float pos[]={m.x,m.y};original(self,mode,m.yawRadians,pos,m.radius,m.rgba.data());++c->markers;
        if(c->arrows<=3 || c->arrows%120==0)c->log("p23_ally_arrow","\"slot\":%u,\"epoch\":%u,\"x\":%.9g,\"y\":%.9g,\"yaw\":%.9g,\"mode\":%u,\"caller_rva\":1716849,\"rgba\":[%u,%u,%u,%u],\"abi_yaw_xmm2\":true",m.slot,view.epoch(),m.x,m.y,m.yawRadians,static_cast<unsigned char>(mode),m.rgba[0],m.rgba[1],m.rgba[2],m.rgba[3]);
    }
}
} // namespace
// The player name defaults to the Windows user name (PSE_PLAYER_NAME overrides it, e.g. for two test instances on one PC).
std::string defaultName() {
    std::string out;char env[64]{};
    if(GetEnvironmentVariableA("PSE_PLAYER_NAME",env,sizeof(env)))out=env;
    else {
        wchar_t user[128]{};DWORD length=128;
        if(GetUserNameW(user,&length)) {
            char utf8[256]{};const int n=WideCharToMultiByte(CP_UTF8,0,user,-1,utf8,sizeof(utf8),nullptr,nullptr);
            if(n>1)out.assign(utf8,static_cast<std::size_t>(n-1));
        }
    }
    while(!out.empty() && (out.size()>31 || !ui::name(out)))eraseUtf8(out); // longer than the wire limit or malformed: cut at a character boundary
    return ui::name(out)?out:std::string("Player");
}
bool install(unsigned char* base,DWORD tid,bool host,Logger logger) {
    if(c || GetCurrentThreadId()!=tid || !base || !logger)return false;
    c=new Context;c->base=base;c->tid=tid;c->log=logger;c->form.host=host;c->form.displayName=defaultName();
    if(host)c->form.address="0.0.0.0"; // the automatic lobby listens on every interface (LAN, Radmin VPN, ...)
    else {
        c->browser.start();
        char join[80]{};   // join.bat 1.2.3.4[:port]: the address is already in the F7 form (F7, Enter)
        if(GetEnvironmentVariableA("PSE_JOIN_ADDRESS",join,sizeof(join)) && join[0]) {c->form.address=join;c->status="F7, Enter: connect to "+c->form.address;}
    }
    ui::shared().host=host;ui::shared().menu=c->form;
    ui::shared().eventLogger.store(logger,std::memory_order_release);
    HMODULE sdl=GetModuleHandleW(L"SDL2.dll"),d3d=GetModuleHandleW(L"d3d11.dll");if(!sdl || !d3d)return false;
    c->textStart=reinterpret_cast<Void>(GetProcAddress(sdl,"SDL_StartTextInput"));c->textStop=reinterpret_cast<Void>(GetProcAddress(sdl,"SDL_StopTextInput"));
    c->textActive=reinterpret_cast<Flag>(GetProcAddress(sdl,"SDL_IsTextInputActive"));c->relative=reinterpret_cast<Flag>(GetProcAddress(sdl,"SDL_GetRelativeMouseMode"));c->relativeSet=reinterpret_cast<SetFlag>(GetProcAddress(sdl,"SDL_SetRelativeMouseMode"));
    c->windowById=reinterpret_cast<WindowById>(GetProcAddress(sdl,"SDL_GetWindowFromID"));c->windowInfo=reinterpret_cast<GetWMInfo>(GetProcAddress(sdl,"SDL_GetWindowWMInfo"));
    if(!c->textStart || !c->textStop || !c->textActive || !c->relative || !c->relativeSet || !c->windowById || !c->windowInfo)return false;
    #ifndef PSE_P23_UI_FIXTURE
    if(!std::equal(ArrowPrefix.begin(),ArrowPrefix.end(),base+0x1a34c0))return false;
    #endif
    const bool ok=hook(DeviceSite,reinterpret_cast<void*>(GetProcAddress(d3d,"D3D11CreateDevice")),reinterpret_cast<void*>(&createDevice)) &&
        hook(PollSite,reinterpret_cast<void*>(GetProcAddress(sdl,"SDL_PollEvent")),reinterpret_cast<void*>(&poll))
        #ifndef PSE_P23_UI_FIXTURE
        && hook(ArrowSite,base+0x1a34c0,reinterpret_cast<void*>(&arrow))
        #endif
        ;
    installed.store(ok,std::memory_order_release);return ok;
}
void permit() {ui::shared().permitted.store(true,std::memory_order_release);}
void enableInterface() {ui::shared().interfaceEnabled.store(true,std::memory_order_release);}
void tick(bool ready,std::uint32_t epoch,std::uint8_t local,std::uint32_t generation) {
    if(c && c->stopped.load() && GetCurrentThreadId()==c->tid) {stop();return;}
    if(!c || GetCurrentThreadId()!=c->tid)return;
    c->epoch=epoch;c->local=local;c->generation=generation;
    ui::shared().ready.store(ready && epoch && local<8 && generation,std::memory_order_release);
    static unsigned lobbyTicks=0;++lobbyTicks;
    if(c->form.host) {
        // Automatic lobby: the host needs no menu. The first tick with the interface enabled creates the request (same as pressing Enter
        // in the old host form) and starts announcing; the game session itself still starts when the host stands in a level.
        if(!c->autoLobby && ui::shared().interfaceEnabled.load() && !ui::shared().requested.load() && !ui::shared().stopping.load()) {
            std::lock_guard lock(c->uiMutex);c->autoLobby=true;
            if(ui::shared().submit(c->form)) {
                ++c->submitted;c->status="Lobby created automatically";
                const bool ann=c->advertiser.start(c->form.displayName,*ui::port(c->form.portText));
                c->log("p23_lobby_auto_created","\"announcing\":%s,\"port\":%u,\"name_bytes\":%zu",ann?"true":"false",*ui::port(c->form.portText),c->form.displayName.size());
                c->mapper.start(*ui::port(c->form.portText));
            }
        }
        if(const int st=static_cast<int>(c->mapper.state());st!=c->mapperLogged && lobbyTicks%30==0) {
            c->mapperLogged=st;const auto m=c->mapper.message();const auto ext=c->mapper.external();
            c->log("upnp_state","\"state\":%d,\"external\":\"%s\",\"message\":\"%s\"",st,ext.c_str(),m.c_str());
        }
    } else if(lobbyTicks%20==0) {
        auto fresh=c->browser.list();
        std::lock_guard lock(c->uiMutex);
        if(fresh.size()!=c->lobbies.size() || (!fresh.empty() && c->lobbies.empty()))
            c->log("p23_lobbies_seen","\"count\":%zu,\"first\":\"%s\",\"address\":\"%s\",\"open\":%s",fresh.size(),fresh.empty()?"":fresh[0].host.c_str(),fresh.empty()?"":fresh[0].address.c_str(),!fresh.empty()&&fresh[0].open?"true":"false");
        c->lobbies=std::move(fresh);
        if(c->pick>=static_cast<int>(c->lobbies.size()))c->pick=c->lobbies.empty()?0:static_cast<int>(c->lobbies.size())-1;
    }
}
void lobbyInfo(const char* map,unsigned players,bool open) {
    if(!c || GetCurrentThreadId()!=c->tid || !c->form.host || !c->advertiser.running())return;
    static unsigned n=0;if(++n%30!=1)return; // twice a second is plenty for a beacon
    c->advertiser.set(map?map:"",static_cast<std::uint8_t>((std::min)(players,255u)),open);
}
void stop() {
    if(!c || GetCurrentThreadId()!=c->tid)return;
    c->advertiser.stop();c->browser.stop();
    if(c->form.host && c->mapper.state()!=upnp::State::Idle) {const int before=static_cast<int>(c->mapper.state());c->mapper.stop();c->log("upnp_stopped","\"state_before\":%d,\"mapping_removed\":%s",before,before==2?"true":"false");}
    c->stopped.store(true);ui::shared().stopping.store(true);ui::shared().ready.store(false);
    {std::lock_guard lock(c->uiMutex);inputLease(false,false);}
    for(unsigned i=0;i<Count;++i) {auto& h=c->hooks[i];if(!h.enabled)continue;
        if(MH_DisableHook(h.site)!=MH_OK) {++c->errors;continue;}h.enabled=false;
        const bool exact=readable(static_cast<unsigned char*>(h.site)-8,40) && std::equal(h.bytes.begin(),h.bytes.end(),static_cast<unsigned char*>(h.site)-8);
        if(exact)++c->restores;else ++c->errors;
        c->log("p23_hook_restore","\"site\":%u,\"exact16\":%s,\"exact40\":%s,\"patch_above_covered\":true,\"pinned\":true",i,exact?"true":"false",exact?"true":"false");
    }
    // No trampoline/resource reclamation while a callback may still return.
    // Context, graphics and module are retained to process exit, as DOC-51.
}
void worldExit() {
    if(!c || GetCurrentThreadId()!=c->tid)return;
    ui::shared().stopping.store(true);ui::shared().ready.store(false);ui::shared().requested.store(false);
    {std::lock_guard lock(c->uiMutex);c->menu=c->composer=c->board=false;inputLease(false,false);c->status="Session ended. Restart game for a new session";}
    auto& h=c->hooks[ArrowSite];
    if(h.enabled) {
        const auto result=MH_DisableHook(h.site);h.enabled=false;
        const bool exact=result==MH_OK && readable(static_cast<unsigned char*>(h.site)-8,40) && std::equal(h.bytes.begin(),h.bytes.end(),static_cast<unsigned char*>(h.site)-8);
        if(exact)++c->restores;else ++c->errors;
        c->log("p23_hook_restore","\"site\":4,\"exact40\":%s,\"world_exit\":true,\"public_ui_retained\":true",exact?"true":"false");
    }
}
DWORD observe(Observation* out,std::uint32_t size) {
    if(!out || size!=sizeof(Observation))return ERROR_INVALID_PARAMETER;
    if(!installed.load(std::memory_order_acquire))return ERROR_NOT_READY;
    Observation o;o.presents=c->presents.load();o.overlays=c->overlays.load();o.arrows=c->arrows.load();o.markers=c->markers.load();o.menus=c->menus.load();o.submitted=c->submitted.load();o.chats=c->chats.load();o.restores=c->restores.load();o.errors=c->errors.load();o.renderTid=c->renderTid.load();o.simulationTid=c->tid;o.ready=ui::shared().ready.load();o.requested=ui::shared().requested.load();*out=o;return ERROR_SUCCESS;
}
} // namespace pse::native_ui
extern "C" __declspec(dllexport) DWORD WINAPI PSE_P23_ReadObservation(pse::native_ui::Observation* out,std::uint32_t size) {return pse::native_ui::observe(out,size);}
extern "C" __declspec(dllexport) DWORD WINAPI PSE_P23_PermitInterface() {pse::native_ui::enableInterface();return ERROR_SUCCESS;}
