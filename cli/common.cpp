#include "overlay.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace overlay {
volatile std::sig_atomic_t cancelled = 0;
bool validUtf8(const std::string& s) {
    for (std::size_t i = 0; i < s.size();) {
        unsigned c = static_cast<unsigned char>(s[i++]), n = 0, cp = 0, min = 0;
        if (c == 0) return false;
        if (c < 128) continue;
        if (c >= 0xc2 && c <= 0xdf) { n=1; cp=c&31; min=128; }
        else if (c >= 0xe0 && c <= 0xef) { n=2; cp=c&15; min=2048; }
        else if (c >= 0xf0 && c <= 0xf4) { n=3; cp=c&7; min=65536; }
        else return false;
        if (n > s.size()-i) return false;
        while (n--) {
            unsigned b=static_cast<unsigned char>(s[i++]);
            if ((b&192)!=128) return false;
            cp=(cp<<6)|(b&63);
        }
        if (cp<min || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff)) return false;
    }
    return true;
}
std::string quote(const std::string& text) {
    std::string out="\"";
    constexpr char hex[]="0123456789abcdef";
    for (unsigned char c : text) {
        if (c=='"' || c=='\\') { out+='\\'; out+=char(c); }
        else if (c<32) { out+="\\u00"; out+=hex[c>>4]; out+=hex[c&15]; }
        else out+=char(c);
    }
    return out+'"';
}
static double number(const std::string& s, double min, double max, bool integer=false) {
    // A locale-independent decimal grammar; no whitespace, hex, NaN or infinity.
    std::size_t i=0;
    if (i<s.size() && s[i]=='-') ++i;
    std::size_t start=i;
    while (i<s.size() && s[i]>='0' && s[i]<='9') ++i;
    if (i==start) throw Error("invalid_arguments","expected decimal number");
    if (!integer && i<s.size() && s[i]=='.') {
        start=++i;
        while (i<s.size() && s[i]>='0' && s[i]<='9') ++i;
        if (start==i) throw Error("invalid_arguments","expected digits after decimal point");
    }
    if (i!=s.size()) throw Error("invalid_arguments","invalid decimal number");
    std::istringstream input(s); input.imbue(std::locale::classic());
    double v=0; input>>v;
    if (input.fail() || !std::isfinite(v) || v<min || v>max)
        throw Error("invalid_arguments","number outside supported range");
    return v;
}
Options parse(const std::vector<std::string>& args) {
    for (const auto& a:args)
        if (!validUtf8(a)) throw Error("invalid_arguments","arguments must be non-NUL UTF-8");
    if (args.empty()) throw Error("invalid_arguments","expected show, validate, inspect, arguments, screens, capabilities, --version or --help");
    Options o; o.command=args[0];
    if (o.command=="arguments") {
        if (args.size()!=3 || args[1]!="--value")
            throw Error("invalid_arguments","arguments requires exactly --value TEXT");
        o.image=args[2];
        return o;
    }
    if (o.command=="screens" || o.command=="capabilities" || o.command=="--version" || o.command=="--help") {
        if (args.size()!=1) throw Error("invalid_arguments","command takes no options");
        return o;
    }
    if (o.command!="show" && o.command!="validate" && o.command!="inspect") throw Error("invalid_arguments","unknown command");
    std::set<std::string> seen;
    for (std::size_t i=1;i<args.size();++i) {
        const auto key=args[i];
        if (!seen.insert(key).second) throw Error("invalid_arguments","duplicate option: "+key);
        if (key=="--stdin") { o.stdinImage=true; continue; }
        static const std::set<std::string> keys={"--image","--x","--y","--mode","--timeout-ms",
            "--fade-in-ms","--fade-out-ms","--opacity","--scale","--screen","--modal","--host-pid","--cancel-file"};
        if (!keys.count(key)) throw Error("invalid_arguments","unknown option: "+key);
        if (++i==args.size()) throw Error("invalid_arguments","missing option value");
        const auto& v=args[i];
        if (key=="--image") { if (v.empty()) throw Error("invalid_arguments","empty image path"); o.image=v; }
        else if (key=="--cancel-file") { if (v.empty()) throw Error("invalid_arguments","empty cancellation path"); o.cancelFile=v; }
        else if (key=="--x") o.x=number(v,-1000000,1000000);
        else if (key=="--y") o.y=number(v,-1000000,1000000);
        else if (key=="--opacity") o.opacity=number(v,0,1);
        else if (key=="--scale") o.scale=number(v,0.01,16);
        else if (key=="--timeout-ms") o.timeout=int(number(v,0,3600000,true));
        else if (key=="--fade-in-ms") o.fadeIn=int(number(v,0,60000,true));
        else if (key=="--fade-out-ms") o.fadeOut=int(number(v,0,60000,true));
        else if (key=="--screen") o.screen=int(number(v,0,255,true));
        else if (key=="--host-pid") o.hostPid=std::uint32_t(number(v,1,2147483647,true));
        else if (key=="--mode") {
            if (v!="passive" && v!="clickable") throw Error("invalid_arguments","mode must be passive or clickable");
            o.clickable=(v=="clickable");
        } else if (key=="--modal") {
            if (v=="true") throw Error("unsupported_modal","modal overlays are unsupported; use false");
            if (v!="false") throw Error("invalid_arguments","modal must be true or false");
        }
    }
    if ((o.image.empty()==!o.stdinImage) || !seen.count("--x") || !seen.count("--y"))
        throw Error("invalid_arguments","require exactly one --image PATH or --stdin, and --x and --y");
    if (o.clickable && o.opacity==0) throw Error("invalid_arguments","clickable opacity must be positive");
    if (o.command=="inspect" && o.stdinImage) throw Error("invalid_arguments","inspect requires --image, not --stdin");
    return o;
}
bool controlCancelled(const Options& o) {
    if (cancelled) return true;
    if (o.cancelFile.empty()) return false;
    std::error_code ec;
    const bool exists=std::filesystem::exists(std::filesystem::u8path(o.cancelFile),ec);
    if (ec) throw Error("control_error","cannot inspect cancellation path");
    return exists;
}
std::vector<std::uint8_t> readImage(const Options& o) {
    std::vector<std::uint8_t> bytes;
    auto append=[&](const char* b, std::size_t n) {
        if (bytes.size()+n>maxBytes) throw Error("image_limit","PNG exceeds 32 MiB");
        bytes.insert(bytes.end(),b,b+n);
    };
    if (!o.stdinImage) {
        std::error_code fileError;
        if (!std::filesystem::is_regular_file(std::filesystem::u8path(o.image),fileError) || fileError)
            throw Error("image_read","image must be an accessible regular file");
        std::ifstream input(std::filesystem::u8path(o.image),std::ios::binary);
        if (!input) throw Error("image_read","cannot open PNG");
        char buffer[65536];
        while (input) { input.read(buffer,sizeof buffer); append(buffer,std::size_t(input.gcount())); }
        if (!input.eof()) throw Error("image_read","cannot read PNG");
    } else {
        if (controlCancelled(o)) throw Error("cancelled","cancelled during stdin read");
#ifndef _WIN32
        struct stat inputStat{};
        if (fstat(STDIN_FILENO,&inputStat)<0 || (!S_ISREG(inputStat.st_mode) && !S_ISFIFO(inputStat.st_mode)))
            throw Error("image_read","stdin must be a binary pipe or file");
#endif
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        char buffer[65536];
        for (;;) {
            if (controlCancelled(o)) throw Error("cancelled","cancelled during stdin read");
            if (std::chrono::steady_clock::now()>=deadline) throw Error("stdin_timeout","stdin PNG requires EOF within 5000 ms");
#ifdef _WIN32
            HANDLE input=GetStdHandle(STD_INPUT_HANDLE);
            DWORD type=GetFileType(input), available=0, count=0;
            if (type==FILE_TYPE_PIPE) {
                if (!PeekNamedPipe(input,nullptr,0,nullptr,&available,nullptr)) {
                    if (GetLastError()==ERROR_BROKEN_PIPE) break;
                    throw Error("image_read","cannot inspect stdin pipe");
                }
                if (!available) { std::this_thread::sleep_for(std::chrono::milliseconds(10)); continue; }
            } else if (type!=FILE_TYPE_DISK) throw Error("image_read","stdin must be a binary pipe or file");
            if (!ReadFile(input,buffer,type==FILE_TYPE_PIPE?std::min<DWORD>(sizeof buffer,available):sizeof buffer,&count,nullptr)) {
                if (GetLastError()==ERROR_BROKEN_PIPE) break;
                throw Error("image_read","cannot read stdin");
            }
            if (!count) break;
            append(buffer,count);
#else
            pollfd fd{STDIN_FILENO,POLLIN,0};
            int ready=poll(&fd,1,20);
            if (ready<0) { if (errno==EINTR) continue; throw Error("image_read","cannot poll stdin"); }
            if (!ready) continue;
            auto n=::read(STDIN_FILENO,buffer,sizeof buffer);
            if (n==0) break;
            if (n<0) { if (errno==EINTR || errno==EAGAIN) continue; throw Error("image_read","cannot read stdin"); }
            append(buffer,std::size_t(n));
#endif
        }
    }
    checkPng(bytes);
    return bytes;
}
static std::uint32_t big32(const std::uint8_t* p) {
    return (std::uint32_t(p[0])<<24)|(std::uint32_t(p[1])<<16)|(std::uint32_t(p[2])<<8)|p[3];
}
static std::uint32_t pngCrc(const std::uint8_t* data,std::size_t count) {
    static const auto table=[] {
        std::array<std::uint32_t,256> entries{};
        for (std::uint32_t i=0;i<256;++i) {
            std::uint32_t value=i;
            for (int bit=0;bit<8;++bit) value=(value>>1)^((value&1)?0xedb88320u:0);
            entries[i]=value;
        }
        return entries;
    }();
    std::uint32_t value=0xffffffff;
    for (std::size_t i=0;i<count;++i) value=table[(value^data[i])&255]^(value>>8);
    return ~value;
}
void checkPng(const std::vector<std::uint8_t>& b) {
    const std::uint8_t signature[]={137,80,78,71,13,10,26,10};
    if (b.size()>maxBytes) throw Error("image_limit","PNG exceeds encoded limit");
    if (b.size()<33 || !std::equal(std::begin(signature),std::end(signature),b.begin()) ||
        big32(&b[8])!=13 || std::string(reinterpret_cast<const char*>(&b[12]),4)!="IHDR")
        throw Error("invalid_image","expected PNG with IHDR");
    auto w=big32(&b[16]), h=big32(&b[20]);
    if (!w || !h || w>maxDimension || h>maxDimension || std::uint64_t(w)*h>maxPixels)
        throw Error("image_limit","PNG dimensions exceed 8192 or 16777216 pixels");
    bool ended=false, data=false;
    for (std::size_t pos=8;pos<b.size();) {
        if (b.size()-pos<12) throw Error("invalid_image","truncated PNG chunk");
        const auto n=big32(&b[pos]);
        if (n>b.size()-pos-12) throw Error("invalid_image","truncated PNG chunk");
        if (pngCrc(&b[pos+4],std::size_t(n)+4)!=big32(&b[pos+8+n]))
            throw Error("invalid_image","PNG chunk checksum mismatch");
        std::string type(reinterpret_cast<const char*>(&b[pos+4]),4);
        if (pos!=8 && type=="IHDR") throw Error("invalid_image","duplicate PNG header");
        if (type=="acTL") throw Error("invalid_image","animated PNG is unsupported");
        if (type=="IDAT") data=true;
        if (type=="IEND") {
            if (n!=0 || pos+12!=b.size()) throw Error("invalid_image","invalid PNG end");
            ended=true; break;
        }
        pos+=std::size_t(n)+12;
    }
    if (!ended || !data) throw Error("invalid_image","PNG lacks image data or end");
}
std::vector<Region> coverage(const Image& image, std::size_t limit) {
    if (image.pixels.size()!=std::uint64_t(image.width)*image.height*4)
        throw Error("invalid_image","invalid decoded pixel buffer");
    std::vector<Region> regions;
    std::map<std::pair<int,int>,std::size_t> previous;
    for (std::uint32_t y=0;y<image.height;++y) {
        std::map<std::pair<int,int>,std::size_t> current;
        for (std::uint32_t x=0;x<image.width;) {
            if (!image.pixels[(std::size_t(y)*image.width+x)*4+3]) { ++x; continue; }
            const int begin=int(x++);
            while (x<image.width && image.pixels[(std::size_t(y)*image.width+x)*4+3]) ++x;
            auto key=std::make_pair(begin,int(x)-begin);
            auto found=previous.find(key);
            std::size_t index;
            if (found!=previous.end()) { index=found->second; ++regions[index].height; }
            else {
                if (regions.size()>=limit) throw Error("image_complexity","clickable PNG exceeds 512 alpha-coverage rectangles");
                index=regions.size(); regions.push_back({begin,int(y),int(x)-begin,1});
            }
            current[key]=index;
        }
        previous=std::move(current);
    }
    return regions;
}
Image rasterize(const Options& o,const Image& image) {
    if (!image.width || !image.height || image.width>maxDimension || image.height>maxDimension ||
        std::uint64_t(image.width)*image.height>maxPixels ||
        image.pixels.size()!=std::uint64_t(image.width)*image.height*4)
        throw Error("invalid_image","invalid decoded image");
    const double renderedWidth=std::ceil(image.width*o.scale),renderedHeight=std::ceil(image.height*o.scale);
    if (renderedWidth>maxDimension || renderedHeight>maxDimension || renderedWidth*renderedHeight>maxPixels)
        throw Error("image_limit","scaled surface exceeds dimension/pixel limits");
    Image raster; raster.width=std::uint32_t(renderedWidth); raster.height=std::uint32_t(renderedHeight);
    raster.pixels.resize(std::size_t(raster.width)*raster.height*4);
    for (std::uint32_t y=0;y<raster.height;++y) for (std::uint32_t x=0;x<raster.width;++x) {
        auto sx=std::min(image.width-1,std::uint32_t(x/o.scale));
        auto sy=std::min(image.height-1,std::uint32_t(y/o.scale));
        auto* dst=&raster.pixels[(std::size_t(y)*raster.width+x)*4];
        const auto* src=&image.pixels[(std::size_t(sy)*image.width+sx)*4];
        for (int c=0;c<4;++c) dst[c]=std::uint8_t(std::lround(src[c]*o.opacity));
    }
    return raster;
}
void validate(const Options& o,const Image& image,const std::vector<Screen>& displays) {
    if (displays.empty()) throw Error("screen_unavailable","no screens available");
    if (o.screen>=int(displays.size())) throw Error("invalid_arguments","screen index is unavailable");
    Options renderOptions=o;
    // Passive zero opacity is allowed, but placement must still intersect artwork.
    if (!o.clickable) renderOptions.opacity=1;
    auto raster=rasterize(renderOptions,image);
    bool sourceVisible=false;
    for (std::size_t i=3;i<image.pixels.size();i+=4) if (image.pixels[i]) { sourceVisible=true; break; }
    if (!sourceVisible) throw Error("invalid_image","PNG is fully transparent");
    bool visible=false, intersects=false;
#ifdef _WIN32
    const double originX=std::lround(o.x),originY=std::lround(o.y);
#else
    const double originX=o.x,originY=o.y;
#endif
    for (std::uint32_t y=0;y<raster.height;++y)
        for (std::uint32_t x=0;x<raster.width;++x) {
            if (!raster.pixels[(std::size_t(y)*raster.width+x)*4+3]) continue;
            visible=true;
            double left=originX+x, top=originY+y;
            for (std::size_t i=0;i<displays.size();++i) {
                if (o.screen>=0 && i!=std::size_t(o.screen)) continue;
                const auto& s=displays[i];
                if (left<s.x+s.width && left+1>s.x && top<s.y+s.height && top+1>s.y) intersects=true;
            }
        }
    if (!visible) throw Error(o.clickable?"invalid_arguments":"invalid_image","rendered PNG has no nonzero-alpha pixels after scaling and opacity");
    if (!intersects) throw Error("offscreen","no nonzero-alpha pixel intersects the selected/current screens");
    if (o.clickable) coverage(raster);
}
Result acceptedClick(const Options& o,double globalX,double globalY) {
    return {"click",true,globalX-o.x,globalY-o.y};
}
bool timeoutExpired(const Options& o,double elapsedMilliseconds) {
    return o.timeout>0 && elapsedMilliseconds>=o.timeout;
}
bool focusedEscapeAllowed(const Options& o,bool focused) {
    return o.clickable && focused;
}
double smoothstep(double progress) {
    if (!std::isfinite(progress)) return progress>0?1:0;
    const double t=std::clamp(progress,0.0,1.0);
    return t*t*(3-2*t);
}
void cancelOutcome(Result& result,bool& complete) {
    result={"cancel"};
    complete=true;
}
std::string terminal(const Result& r) {
    std::ostringstream s; s.imbue(std::locale::classic()); s<<std::setprecision(15);
    s<<"{\"protocol\":1,\"outcome\":\""<<(r.accepted?"accepted":"dismissed")<<"\",\"reason\":"<<quote(r.reason);
    if (r.accepted) s<<",\"x\":"<<r.x<<",\"y\":"<<r.y;
    s<<"}"; return s.str();
}
std::string screenJson(const std::vector<Screen>& displays) {
    std::ostringstream s; s.imbue(std::locale::classic()); s<<std::setprecision(15)<<"{\"protocol\":1,\"screens\":[";
    for (std::size_t i=0;i<displays.size();++i) {
        if (i) s<<',';
        const auto& d=displays[i];
        s<<"{\"index\":"<<i<<",\"x\":"<<d.x<<",\"y\":"<<d.y<<",\"width\":"<<d.width
         <<",\"height\":"<<d.height<<",\"scale\":"<<d.scale<<"}";
    }
    return s.str()+"]}";
}
std::string errorJson(const Error& e) {
    return "{\"protocol\":1,\"error\":{\"code\":"+quote(e.code)+",\"message\":"+quote(e.what())+"}}";
}
int run(const std::vector<std::string>& args,std::string_view version,std::uint32_t originalParent) {
    std::signal(SIGINT,[](int){cancelled=1;});
    std::signal(SIGTERM,[](int){cancelled=1;});
    try {
        auto o=parse(args);
        std::string output;
        if (o.command=="--version") output="{\"protocol\":1,\"version\":"+quote(std::string(version))+"}";
        else if (o.command=="capabilities") output="{\"protocol\":1,\"modal\":false,\"format\":\"png\",\"modes\":[\"passive\",\"clickable\"]}";
        else if (o.command=="arguments") output="{\"protocol\":1,\"value\":"+quote(o.image)+"}";
        else if (o.command=="inspect") output="{\"protocol\":1,\"image\":"+quote(o.image)+"}";
        else if (o.command=="--help")
            output="{\"protocol\":1,\"usage\":\"show|validate --image PATH|--stdin --x N --y N [--mode passive|clickable] [--timeout-ms N] [--fade-in-ms N] [--fade-out-ms N] [--opacity N] [--scale N] [--screen N] [--modal false] [--host-pid PID] [--cancel-file PATH]; inspect --image PATH --x N --y N [same options]; arguments --value TEXT; screens; capabilities; --version\"}";
        else if (o.command=="screens") output=screenJson(screens());
        else {
            std::unique_ptr<HostLifetime> host;
            if (o.command=="show") {
                const auto pid=o.hostPid?o.hostPid:originalParent;
                if (!pid) throw Error("host_watch","cannot determine original parent process");
                host=watchHost(pid);
            }
            auto image=decode(readImage(o));
            validate(o,image,screens());
            if (o.command=="validate") output="{\"protocol\":1,\"valid\":true,\"width\":"+std::to_string(image.width)+",\"height\":"+std::to_string(image.height)+"}";
            else {
                auto result=show(o,image,*host);
                if (controlCancelled(o)) {
                    bool complete=true;
                    cancelOutcome(result,complete);
                }
                output=terminal(result);
            }
        }
        std::cout<<output<<'\n';
        return 0;
    } catch (const Error& e) {
        std::cerr<<errorJson(e)<<'\n'; return 2;
    } catch (const std::exception&) {
        std::cerr<<errorJson(Error("native_error","native operation failed"))<<'\n'; return 3;
    }
}
}
