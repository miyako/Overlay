#include "overlay.hpp"
#include <cerrno>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {
int checks=0;
void require(bool ok,const char* message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
void fails(const std::function<void()>& test,const std::string& code) {
    try { test(); } catch (const overlay::Error& e) { require(e.code==code,"unexpected error code"); return; }
    throw std::runtime_error("expected failure: "+code);
}
using Bytes=std::vector<std::uint8_t>;
void put32(Bytes& out,std::uint32_t n) {
    out.push_back(n>>24); out.push_back(n>>16); out.push_back(n>>8); out.push_back(n);
}
std::uint32_t crc32(const Bytes& data) {
    std::uint32_t crc=0xffffffff;
    for (auto b:data) {
        crc^=b;
        for (int k=0;k<8;++k) crc=(crc>>1)^(0xedb88320u&(0u-(crc&1)));
    }
    return ~crc;
}
void chunk(Bytes& out,const std::string& name,const Bytes& data) {
    put32(out,std::uint32_t(data.size()));
    Bytes body(name.begin(),name.end()); body.insert(body.end(),data.begin(),data.end());
    out.insert(out.end(),body.begin(),body.end()); put32(out,crc32(body));
}
Bytes png() {
    Bytes out={137,80,78,71,13,10,26,10},header;
    put32(header,2); put32(header,2);
    header.insert(header.end(),{8,6,0,0,0}); chunk(out,"IHDR",header);
    Bytes raw={0,255,0,0,255,0,255,0,0, 0,0,0,255,128,255,255,255,255};
    Bytes zlib={0x78,0x01,0x01,std::uint8_t(raw.size()),0,std::uint8_t(~raw.size()),0xff};
    zlib.insert(zlib.end(),raw.begin(),raw.end());
    std::uint32_t a=1,b=0;
    for (auto byte:raw) { a=(a+byte)%65521; b=(b+a)%65521; }
    put32(zlib,(b<<16)|a); chunk(out,"IDAT",zlib); chunk(out,"IEND",{});
    return out;
}
void parserTests() {
    using namespace overlay;
    const std::vector<std::string> base={"validate","--image","x.png","--x","-10.5","--y","0"};
    auto options=parse(base);
    require(options.x==-10.5 && options.timeout==0 && !options.clickable,"defaults");
    auto noTimeout=base; noTimeout.insert(noTimeout.end(),{"--timeout-ms","0"});
    require(parse(noTimeout).timeout==0,"explicit no automatic timeout");
    auto bad=[&](std::initializer_list<std::string> more) {
        auto v=base; v.insert(v.end(),more); fails([&]{parse(v);},"invalid_arguments");
    };
    bad({"--x","1"}); bad({"--unknown","1"}); bad({"--mode","x"});
    bad({"--stdin"}); bad({"--opacity","nan"}); bad({"--opacity","inf"}); bad({"--scale","1e10"});
    bad({"--opacity"," 1"}); bad({"--opacity","1."}); bad({"--opacity","-0.1"});
    bad({"--opacity","1.1"}); bad({"--scale","0"}); bad({"--scale","17"});
    bad({"--timeout-ms","-1"}); bad({"--timeout-ms","3600001"}); bad({"--timeout-ms","2.5"});
    bad({"--timeout-ms","999999999999999999999999999999999999999999999999999999999999"});
    bad({"--fade-in-ms","60001"}); bad({"--fade-out-ms","-1"}); bad({"--screen","256"});
    bad({"--host-pid","0"}); bad({"--host-pid","2147483648"});
    auto modal=base; modal.insert(modal.end(),{"--modal","true"});
    fails([&]{parse(modal);},"unsupported_modal");
    bad({"--modal","False"}); bad({"--mode","clickable","--opacity","0"});
    bad({"--image"}); bad({"--cancel-file",""});
    bad({std::string("\xc0\x80",2),"x"}); bad({"--mode",std::string("a\0b",3)});
    fails([]{parse({"show","--image","x.png","--x","1"});},"invalid_arguments");
    fails([]{parse({"screens","--x","1"});},"invalid_arguments");
    fails([]{parse({"capabilities","--mode","passive"});},"invalid_arguments");
    require(parse({"capabilities"}).command=="capabilities","capabilities command");
    require(parse({"arguments","--value",""}).image.empty(),"empty literal argument");
    fails([]{parse({"arguments","--image","x"});},"invalid_arguments");
    fails([]{parse({"arguments","--value","x","--value","y"});},"invalid_arguments");
    require(parse({"inspect","--image",u8"画像", "--x","0","--y","0"}).image==u8"画像","inspect literal path");
    fails([]{parse({"inspect","--stdin","--x","0","--y","0"});},"invalid_arguments");
    fails([]{parse({});},"invalid_arguments");
    fails([]{parse({"unknown-command"});},"invalid_arguments");
    require(parse({"show","--stdin","--x","0","--y","0","--modal","false","--mode","clickable"}).stdinImage,"stdin options");
    require(validUtf8(u8"画像 🖼é.png"),"Unicode arguments");
    require(!validUtf8(std::string("\xed\xa0\x80",3)),"UTF-8 surrogate");
    require(!validUtf8(std::string("\xf4\x90\x80\x80",4)),"UTF-8 range");
    require(!validUtf8(std::string("\xe2\x82",2)),"UTF-8 incomplete");
    require(quote("\"\n\\")=="\"\\\"\\u000a\\\\\"","JSON quoting");
}
void hostTests() {
    using namespace overlay;
    require(parentProcessId()>0,"original parent discovery");
    fails([]{watchHost(0);},"host_watch");
#ifdef _WIN32
    auto own=watchHost(GetCurrentProcessId());
    require(!own->exited(),"live kernel process handle");
#else
    auto own=watchHost(std::uint32_t(getpid()));
    require(!own->exited(),"live kernel process watch");
    int gate[2]; require(pipe(gate)==0,"host child gate");
    const pid_t child=fork();
    if (child==0) {
        close(gate[1]);
        char byte;
        while (read(gate[0],&byte,1)<0 && errno==EINTR) {}
        close(gate[0]); _exit(0);
    }
    if (child<0) { close(gate[0]); close(gate[1]); throw std::runtime_error("cannot fork host child"); }
    close(gate[0]);
    try {
        auto watched=watchHost(std::uint32_t(child));
        require(!watched->exited(),"bind original live child before input");
        close(gate[1]); gate[1]=-1;
        int status=0;
        pid_t waited;
        do { waited=waitpid(child,&status,0); } while (waited<0 && errno==EINTR);
        require(waited==child && WIFEXITED(status) && WEXITSTATUS(status)==0,"controlled host child exit");
        require(watched->exited() && watched->exited(),"kernel-bound exit remains latched without PID polling");
    } catch (...) {
        if (gate[1]>=0) close(gate[1]);
        int status=0; waitpid(child,&status,0);
        throw;
    }
#endif
}
void imageTests() {
    using namespace overlay;
    auto bytes=png(); checkPng(bytes);
    auto image=decode(bytes);
    require(image.width==2 && image.height==2 && image.pixels.size()==16,"PNG decode size");
    require(image.pixels[2]==255 && image.pixels[3]==255,"PNG top-left red");
    require(image.pixels[7]==0,"PNG top-right transparent");
    require(std::abs(int(image.pixels[8])-128)<=1 && image.pixels[11]==128,"PNG bottom-left premultiplied blue");
    require(image.pixels[12]==255 && image.pixels[15]==255,"PNG bottom-right white");
    auto bad=bytes; bad[0]=0; fails([&]{decode(bad);},"invalid_image");
    bad=bytes; bad.resize(20); fails([&]{decode(bad);},"invalid_image");
    bad=bytes; bad[16]=1; fails([&]{checkPng(bad);},"image_limit");
    bad=bytes; bad.push_back(0); fails([&]{checkPng(bad);},"invalid_image");
    bad=bytes; bad[40]^=1; fails([&]{checkPng(bad);},"invalid_image");
    bad=Bytes(maxBytes+1,0); fails([&]{checkPng(bad);},"image_limit");
    bad=bytes; bad[19]=0; fails([&]{checkPng(bad);},"image_limit");
    bad=bytes; bad.resize(bad.size()-12); fails([&]{checkPng(bad);},"invalid_image");
    bad=bytes; bad.resize(bad.size()-12);
    chunk(bad,"acTL",{0,0,0,1,0,0,0,0}); chunk(bad,"IEND",{});
    fails([&]{checkPng(bad);},"invalid_image");
    Options o; o.command="validate";
    std::vector<Screen> screens={{0,0,100,100,1},{-100,-100,100,100,2}};
    validate(o,image,screens);
    o.clickable=true; validate(o,image,screens);
    const auto regions=coverage(image);
    require(regions.size()==2 && regions[0].x==0 && regions[0].y==0 &&
        regions[0].width==1 && regions[1].width==2,"exact alpha-coverage rectangles");
    fails([&]{coverage(image,1);},"image_complexity");
    Image solid{2,2,Bytes(16,255)};
    require(coverage(solid).size()==1 && coverage(solid)[0].height==2,"vertical rectangle merging");
    auto empty=solid; for (std::size_t i=3;i<empty.pixels.size();i+=4) empty.pixels[i]=0;
    fails([&]{validate(o,empty,screens);},"invalid_image");
    o.opacity=0.000001;
    fails([&]{validate(o,image,screens);},"invalid_arguments");
    o.opacity=1;
    Image thinAlpha{2,1,{0,0,1,1,0,0,0,0}};
    o.opacity=0.49;
    fails([&]{validate(o,thinAlpha,screens);},"invalid_arguments");
    o.opacity=0.5;
    require(rasterize(o,thinAlpha).pixels[3]==1,"alpha rounding boundary");
    validate(o,thinAlpha,screens);
    o.opacity=1; o.scale=0.5;
    Image dropped{2,1,{0,0,0,0,255,255,255,255}};
    fails([&]{validate(o,dropped,screens);},"invalid_arguments");
    o.scale=1;
    Image outside{2,1,{255,255,255,255,0,0,1,1}};
    o.x=-1; o.opacity=0.49;
    fails([&]{validate(o,outside,screens);},"offscreen");
    o.x=0; o.opacity=1;
    o.x=100; fails([&]{validate(o,image,screens);},"offscreen");
    o.x=-1; o.y=-1; o.screen=0;
    // Only the transparent top-right pixel intersects this narrow display.
    std::vector<Screen> tiny={{0,-1,1,1,1}};
    fails([&]{validate(o,image,tiny);},"offscreen");
    o.x=-100; o.y=-100; o.screen=1; validate(o,image,screens);
    o.screen=2; fails([&]{validate(o,image,screens);},"invalid_arguments");
    o.screen=-1; fails([&]{validate(o,image,{});},"screen_unavailable");
    o.x=0; o.y=0; o.scale=16;
    Image large{8192,1,Bytes(8192*4,255)};
    fails([&]{validate(o,large,screens);},"image_limit");
    Image fragmented{1026,1,Bytes(1026*4,0)};
    for (std::size_t x=0;x<1026;x+=2) fragmented.pixels[x*4+3]=255;
    fails([&]{coverage(fragmented);},"image_complexity");
    std::string path=u8"overlay-native-test-画像.png";
    { std::ofstream file(std::filesystem::u8path(path),std::ios::binary); file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size()); }
    o.image=path; o.stdinImage=false;
    require(readImage(o)==bytes,"UTF-8 file read");
    std::filesystem::remove(std::filesystem::u8path(path));
    fails([&]{readImage(o);},"image_read");
}
void inputTests() {
    using namespace overlay;
    Options o; o.stdinImage=true;
    auto bytes=png();
#ifdef _WIN32
    HANDLE reader=nullptr,writer=nullptr;
    require(CreatePipe(&reader,&writer,nullptr,0)!=FALSE,"create stdin pipe");
    HANDLE saved=GetStdHandle(STD_INPUT_HANDLE); SetStdHandle(STD_INPUT_HANDLE,reader);
    DWORD written=0; WriteFile(writer,bytes.data(),DWORD(bytes.size()),&written,nullptr); CloseHandle(writer);
    require(readImage(o)==bytes,"binary stdin");
    CloseHandle(reader);
    require(CreatePipe(&reader,&writer,nullptr,0)!=FALSE,"create deadline pipe");
    SetStdHandle(STD_INPUT_HANDLE,reader);
    fails([&]{readImage(o);},"stdin_timeout");
    CloseHandle(writer); CloseHandle(reader); SetStdHandle(STD_INPUT_HANDLE,saved);
#else
    int fds[2]; require(pipe(fds)==0,"create stdin pipe");
    int saved=dup(STDIN_FILENO); dup2(fds[0],STDIN_FILENO); close(fds[0]);
    require(write(fds[1],bytes.data(),bytes.size())==ssize_t(bytes.size()),"write PNG");
    close(fds[1]); require(readImage(o)==bytes,"binary stdin");
    require(pipe(fds)==0,"create deadline pipe");
    dup2(fds[0],STDIN_FILENO); close(fds[0]);
    fails([&]{readImage(o);},"stdin_timeout");
    close(fds[1]); dup2(saved,STDIN_FILENO); close(saved);
#endif
    cancelled=1; fails([&]{readImage(o);},"cancelled"); cancelled=0;
    o.cancelFile="overlay-native-test-cancel";
    std::filesystem::remove(o.cancelFile);
    require(!controlCancelled(o),"absent cancellation file");
    { std::ofstream f(o.cancelFile); }
    require(controlCancelled(o),"present cancellation file");
    std::filesystem::remove(o.cancelFile);
}
void protocolTests() {
    using namespace overlay;
    Options origin; origin.x=123.5; origin.y=-45.25;
    auto click=acceptedClick(origin,133.5,-25.25);
    require(click.accepted && click.reason=="click" && click.x==10 && click.y==20,"window-relative top-left click mapping");
    origin.x=-100; origin.y=-200;
    click=acceptedClick(origin,-93,-189);
    require(click.x==7 && click.y==11,"negative desktop origin mapping");
    origin.x=100; origin.y=50;
    click=acceptedClick(origin,145,78);
    require(click.x==45 && click.y==28,"coverage-window offset mapped to whole overlay");
    require(!timeoutExpired(origin,1000000000),"timeout zero has no display deadline");
    origin.timeout=30;
    require(!timeoutExpired(origin,29) && timeoutExpired(origin,30),"finite display timeout boundary");
    require(!focusedEscapeAllowed(origin,true) && !focusedEscapeAllowed(origin,false),"passive never handles Escape");
    origin.clickable=true;
    require(!focusedEscapeAllowed(origin,false) && focusedEscapeAllowed(origin,true),"Escape requires clickable overlay focus");
    require(smoothstep(-1)==0 && smoothstep(0)==0 && smoothstep(0.5)==0.5 &&
        smoothstep(1)==1 && smoothstep(2)==1,"smoothstep clamped endpoints and midpoint");
    require(smoothstep(0.25)==0.15625 && smoothstep(0.75)==0.84375,"smoothstep ease-in-out curve");
    double previous=0;
    for (int i=0;i<=100;++i) {
        double value=smoothstep(i/100.0);
        require(value>=previous && value>=0 && value<=1,"smoothstep bounded monotonicity");
        previous=value;
    }
    require(smoothstep(std::numeric_limits<double>::quiet_NaN())==0 &&
        smoothstep(std::numeric_limits<double>::infinity())==1,"smoothstep nonfinite guards");
    Result provisional={"click",true,10,20};
    bool complete=true;
    cancelOutcome(provisional,complete);
    require(complete && !provisional.accepted && provisional.reason=="cancel","explicit cancel wins during completed fade");
    require(terminal(provisional)=="{\"protocol\":1,\"outcome\":\"dismissed\",\"reason\":\"cancel\"}","cancel removes provisional click coordinates");
    cancelOutcome(provisional,complete);
    require(complete && provisional.reason=="cancel","explicit cancellation is idempotent");
    require(terminal({"timeout"})=="{\"protocol\":1,\"outcome\":\"dismissed\",\"reason\":\"timeout\"}","dismissed protocol");
    require(terminal({"click",true,-1.5,2})=="{\"protocol\":1,\"outcome\":\"accepted\",\"reason\":\"click\",\"x\":-1.5,\"y\":2}","accepted protocol");
    require(errorJson(Error("test","a\nb"))=="{\"protocol\":1,\"error\":{\"code\":\"test\",\"message\":\"a\\u000ab\"}}","error protocol");
    std::ostringstream out,err;
    auto* previousOut=std::cout.rdbuf(out.rdbuf()); auto* previousErr=std::cerr.rdbuf(err.rdbuf());
    int status=run({"--version"},"test-version");
    std::cout.rdbuf(previousOut); std::cerr.rdbuf(previousErr);
    require(status==0 && out.str()=="{\"protocol\":1,\"version\":\"test-version\"}\n" && err.str().empty(),"version terminal framing");
    out.str(""); err.str("");
    previousOut=std::cout.rdbuf(out.rdbuf()); previousErr=std::cerr.rdbuf(err.rdbuf());
    status=run({"capabilities"},"test-version");
    std::cout.rdbuf(previousOut); std::cerr.rdbuf(previousErr);
    require(status==0 && out.str()=="{\"protocol\":1,\"modal\":false,\"format\":\"png\",\"modes\":[\"passive\",\"clickable\"]}\n" && err.str().empty(),"capabilities terminal framing");
    out.str(""); err.str("");
    previousOut=std::cout.rdbuf(out.rdbuf()); previousErr=std::cerr.rdbuf(err.rdbuf());
    status=run({"arguments","--value",u8"画像 /'\"\\\n"},"test-version");
    std::cout.rdbuf(previousOut); std::cerr.rdbuf(previousErr);
    require(status==0 && out.str()==u8"{\"protocol\":1,\"value\":\"画像 /'\\\"\\\\\\u000a\"}\n" && err.str().empty(),"literal argument roundtrip");
    out.str(""); err.str("");
    previousOut=std::cout.rdbuf(out.rdbuf()); previousErr=std::cerr.rdbuf(err.rdbuf());
    status=run({"inspect","--image",u8"画像 /'\"\\","--x","0","--y","0"},"test-version");
    std::cout.rdbuf(previousOut); std::cerr.rdbuf(previousErr);
    require(status==0 && out.str()==u8"{\"protocol\":1,\"image\":\"画像 /'\\\"\\\\\"}\n" && err.str().empty(),"parser-only path roundtrip");
    out.str(""); err.str("");
    previousOut=std::cout.rdbuf(out.rdbuf()); previousErr=std::cerr.rdbuf(err.rdbuf());
    status=run({"unknown-command"},"test-version");
    std::cout.rdbuf(previousOut); std::cerr.rdbuf(previousErr);
    require(status!=0 && out.str().empty() && err.str().back()=='\n',"failure no stdout");
}
}
int main() {
    try {
        parserTests(); hostTests(); imageTests(); inputTests(); protocolTests();
        std::cout<<"passed "<<checks<<" native validation assertions (no GUI)\n";
        return 0;
    } catch (const std::exception& e) { std::cerr<<"FAILED: "<<e.what()<<'\n'; return 1; }
}
