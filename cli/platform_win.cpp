#include "overlay.hpp"
#include <windows.h>
#include <tlhelp32.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>

namespace overlay {
using Microsoft::WRL::ComPtr;
std::uint32_t parentProcessId() noexcept {
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if (snapshot==INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W entry{}; entry.dwSize=sizeof entry;
    std::uint32_t parent=0;
    if (Process32FirstW(snapshot,&entry)) {
        do {
            if (entry.th32ProcessID==GetCurrentProcessId()) { parent=entry.th32ParentProcessID; break; }
        } while (Process32NextW(snapshot,&entry));
    }
    CloseHandle(snapshot);
    return parent;
}
static void checked(HRESULT hr,const char* message) {
    if (FAILED(hr)) throw Error("image_decode",message);
}
class ComScope {
    HRESULT hr;
public:
    ComScope():hr(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)) {
        if (FAILED(hr)) throw Error("native_error","cannot initialize COM apartment");
    }
    ~ComScope() { CoUninitialize(); }
};
Image decode(const std::vector<std::uint8_t>& bytes) {
    checkPng(bytes);
    ComScope com;
    ComPtr<IWICImagingFactory> factory;
    checked(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"cannot create WIC");
    ComPtr<IWICStream> stream;
    checked(factory->CreateStream(&stream),"cannot create WIC stream");
    checked(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()),DWORD(bytes.size())),"cannot read WIC stream");
    ComPtr<IWICBitmapDecoder> decoder;
    checked(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnDemand,&decoder),"WIC cannot decode PNG");
    GUID format{};
    checked(decoder->GetContainerFormat(&format),"cannot identify image");
    if (format!=GUID_ContainerFormatPng) throw Error("invalid_image","expected PNG");
    UINT count=0; checked(decoder->GetFrameCount(&count),"cannot read PNG frame count");
    if (count!=1) throw Error("invalid_image","expected single PNG frame");
    ComPtr<IWICBitmapFrameDecode> frame;
    checked(decoder->GetFrame(0,&frame),"cannot read PNG frame");
    UINT width=0,height=0; checked(frame->GetSize(&width,&height),"cannot read PNG size");
    if (!width || !height || width>maxDimension || height>maxDimension || std::uint64_t(width)*height>maxPixels)
        throw Error("image_limit","decoded PNG exceeds limits");
    ComPtr<IWICFormatConverter> converter;
    checked(factory->CreateFormatConverter(&converter),"cannot create PNG converter");
    checked(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,
        nullptr,0,WICBitmapPaletteTypeCustom),"cannot convert premultiplied PNG");
    Image out; out.width=width; out.height=height; out.pixels.resize(std::size_t(width)*height*4);
    checked(converter->CopyPixels(nullptr,width*4,UINT(out.pixels.size()),out.pixels.data()),"cannot copy PNG pixels");
    return out;
}
static void dpiAware() {
    // Dynamically resolve for older Windows; fail closed rather than use virtualized coordinates.
    using SetDpi=BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    auto setDpi=reinterpret_cast<SetDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"SetProcessDpiAwarenessContext"));
    static bool initialized=false;
    if (!initialized) {
        if (!setDpi || !setDpi(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
            throw Error("native_error","per-monitor-v2 DPI awareness unavailable");
        initialized=true;
    }
}
struct NativeScreen { HMONITOR id; Screen screen; bool primary; };
static std::vector<NativeScreen> nativeScreens() {
    dpiAware();
    std::vector<NativeScreen> out;
    if (!EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR id,HDC,LPRECT,LPARAM data)->BOOL {
        MONITORINFO info{}; info.cbSize=sizeof info;
        if (!GetMonitorInfoW(id,&info)) return FALSE;
        auto* list=reinterpret_cast<std::vector<NativeScreen>*>(data);
        auto r=info.rcMonitor;
        list->push_back({id,{double(r.left),double(r.top),double(r.right-r.left),double(r.bottom-r.top),1},
            (info.dwFlags&MONITORINFOF_PRIMARY)!=0});
        return TRUE;
    },reinterpret_cast<LPARAM>(&out))) throw Error("screen_unavailable","cannot enumerate screens");
    std::sort(out.begin(),out.end(),[](const NativeScreen& a,const NativeScreen& b) {
        if (a.primary!=b.primary) return a.primary;
        if (a.screen.x!=b.screen.x) return a.screen.x<b.screen.x;
        return a.screen.y<b.screen.y;
    });
    return out;
}
std::vector<Screen> screens() {
    std::vector<Screen> out;
    for (const auto& monitor:nativeScreens()) out.push_back(monitor.screen);
    return out;
}
struct WindowState { Result result; bool complete=false, abort=false; const Options* options=nullptr; };
static LRESULT CALLBACK windowProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
    auto* state=reinterpret_cast<WindowState*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if (message==WM_NCCREATE) {
        state=static_cast<WindowState*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(state));
    }
    switch (message) {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_LBUTTONDOWN:
        if (state && !state->complete) {
            POINT p{short(LOWORD(lp)),short(HIWORD(lp))};
            ClientToScreen(hwnd,&p);
            state->result=acceptedClick(*state->options,double(p.x),double(p.y)); state->complete=true;
        }
        return 0;
    case WM_KEYDOWN:
        if (wp==VK_ESCAPE && state && focusedEscapeAllowed(*state->options,GetFocus()==hwnd) && !state->complete) {
            state->result={"escape"}; state->complete=true;
        }
        return 0;
    case WM_CLOSE:
        if (state) {
            cancelOutcome(state->result,state->complete);
            state->abort=true;
        }
        return 0;
    }
    return DefWindowProcW(hwnd,message,wp,lp);
}
class Handle {
public:
    HANDLE value=nullptr;
    ~Handle() { if (value) CloseHandle(value); }
};
class HostWatch final : public HostLifetime {
    Handle host;
    bool dead=false;
public:
    explicit HostWatch(std::uint32_t pid) {
        if (!pid) throw Error("host_watch","host PID must be positive");
        host.value=OpenProcess(SYNCHRONIZE,FALSE,pid);
        if (!host.value) {
            if (GetLastError()==ERROR_INVALID_PARAMETER) dead=true;
            else throw Error("host_watch","cannot open host process synchronization handle");
        }
    }
    bool exited() override {
        if (dead) return true;
        const auto status=WaitForSingleObject(host.value,0);
        if (status==WAIT_FAILED) throw Error("host_watch","cannot query host process handle");
        return status==WAIT_OBJECT_0;
    }
};
std::unique_ptr<HostLifetime> watchHost(std::uint32_t pid) { return std::make_unique<HostWatch>(pid); }
class Surface {
public:
    HDC dc=nullptr;
    HBITMAP bitmap=nullptr;
    HGDIOBJ original=nullptr;
    void* bits=nullptr;
    Surface(int width,int height) {
        BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=width; info.bmiHeader.biHeight=-height;
        info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
        dc=CreateCompatibleDC(nullptr);
        if (dc) bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
        if (!dc || !bitmap) {
            if (dc) DeleteDC(dc);
            throw Error("native_error","cannot allocate overlay surface");
        }
        original=SelectObject(dc,bitmap);
        if (!original || original==HGDI_ERROR) {
            DeleteObject(bitmap); DeleteDC(dc);
            throw Error("native_error","cannot select overlay surface");
        }
    }
    ~Surface() {
        if (original) SelectObject(dc,original);
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
    }
};
Result show(const Options& o,const Image& image,HostLifetime& host) {
    dpiAware();
    if (controlCancelled(o)) return {"cancel"};
    if (host.exited()) return {"host-exit"};
    auto displays=nativeScreens();
    validate(o,image,screens());
    const auto rendered=rasterize(o,image);
    const int width=int(rendered.width),height=int(rendered.height);
    Surface surface(width,height);
    const auto& raster=rendered.pixels;
    WNDCLASSEXW cls{}; cls.cbSize=sizeof cls; cls.lpfnWndProc=windowProc;
    cls.hInstance=GetModuleHandleW(nullptr); cls.lpszClassName=L"OverlayCliPassiveSurface";
    cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    if (!RegisterClassExW(&cls)) throw Error("native_error","cannot register overlay window");
    WindowState state; state.options=&o;
    // WS_EX_TRANSPARENT is only passive. Clickable uses documented layered alpha-zero hit testing.
    const DWORD style=WS_EX_LAYERED|WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|
        (o.clickable?0:WS_EX_TRANSPARENT);
    HWND hwnd=CreateWindowExW(style,cls.lpszClassName,L"",WS_POPUP,int(std::lround(o.x)),int(std::lround(o.y)),
        width,height,nullptr,nullptr,cls.hInstance,&state);
    if (!hwnd) { UnregisterClassW(cls.lpszClassName,cls.hInstance); throw Error("native_error","cannot create overlay window"); }
    auto cleanup=[&]() { DestroyWindow(hwnd); UnregisterClassW(cls.lpszClassName,cls.hInstance); };
    try {
        int previousAlpha=-1;
        auto update=[&](double alpha) {
            const int level=int(std::lround(alpha*255));
            if (level==previousAlpha) return;
            auto* destinationBytes=static_cast<std::uint8_t*>(surface.bits);
            for (std::size_t i=0;i<raster.size();++i)
                destinationBytes[i]=std::uint8_t((unsigned(raster[i])*unsigned(level)+127)/255);
            POINT destination{LONG(std::lround(o.x)),LONG(std::lround(o.y))},source{0,0};
            SIZE size{width,height};
            // Actual surface alpha is zero throughout invisible fade frames, not
            // only SourceConstantAlpha; layered hit testing sees those same pixels.
            BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
            if (!UpdateLayeredWindow(hwnd,nullptr,&destination,&size,surface.dc,&source,0,&blend,ULW_ALPHA))
                throw Error("native_error","cannot update layered overlay");
            previousAlpha=level;
        };
        update(o.fadeIn?0:1);
        ShowWindow(hwnd,SW_SHOWNOACTIVATE);
        auto start=std::chrono::steady_clock::now(),endFade=start;
        bool fading=false;
        double fadeStart=1;
        while (true) {
            if (state.abort) break;
            if (controlCancelled(o)) { cancelOutcome(state.result,state.complete); break; }
            if (host.exited()) { if (!state.complete) state.result={"host-exit"}; break; }
            auto current=nativeScreens();
            bool changed=current.size()!=displays.size();
            if (!changed) for (std::size_t i=0;i<current.size();++i)
                if (current[i].id!=displays[i].id || current[i].screen.x!=displays[i].screen.x ||
                    current[i].screen.y!=displays[i].screen.y || current[i].screen.width!=displays[i].screen.width ||
                    current[i].screen.height!=displays[i].screen.height) changed=true;
            if (changed) { if (!state.complete) state.result={"screen-disconnect"}; break; }
            auto now=std::chrono::steady_clock::now();
            double elapsed=std::chrono::duration<double,std::milli>(now-start).count();
            double alpha=o.fadeIn?smoothstep(elapsed/o.fadeIn):1;
            if (!state.complete && timeoutExpired(o,elapsed)) { state.result={"timeout"}; state.complete=true; }
            if (state.complete && !fading) {
                if (!o.fadeOut) break;
                fading=true; endFade=now; fadeStart=alpha;
            }
            if (fading) {
                double t=std::chrono::duration<double,std::milli>(now-endFade).count()/o.fadeOut;
                if (t>=1) break;
                alpha=fadeStart*(1-smoothstep(t));
            }
            update(alpha);
            MSG msg;
            while (PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) {
                if (msg.message==WM_QUIT) {
                    cancelOutcome(state.result,state.complete);
                    state.abort=true;
                    break;
                }
                TranslateMessage(&msg); DispatchMessageW(&msg);
            }
            if (state.abort) break;
            MsgWaitForMultipleObjectsEx(0,nullptr,10,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        }
        cleanup(); return state.result;
    } catch (...) { cleanup(); throw; }
}
}
