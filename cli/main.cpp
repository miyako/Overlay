#include <csignal>
#include "overlay.hpp"
#include <iostream>
const std::string_view kVersion = "0.1.0";
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <fcntl.h>
#include <io.h>
int wmain(int argc,wchar_t** argv) {
    const auto originalParent=overlay::parentProcessId();
    const int outputDescriptor=_fileno(stdout),errorDescriptor=_fileno(stderr);
    const bool outputMode=outputDescriptor>=0 && _setmode(outputDescriptor,_O_BINARY)!=-1;
    const bool errorMode=errorDescriptor>=0 && _setmode(errorDescriptor,_O_BINARY)!=-1;
    if (!outputMode || !errorMode) {
        const auto record=overlay::errorJson(overlay::Error("native_error","cannot configure binary output streams"))+"\n";
        DWORD written=0;
        WriteFile(GetStdHandle(STD_ERROR_HANDLE),record.data(),DWORD(record.size()),&written,nullptr);
        return 3;
    }
    std::vector<std::string> args;
    for (int i=1;i<argc;++i) {
        int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,argv[i],-1,nullptr,0,nullptr,nullptr);
        if (!n) { std::cerr<<overlay::errorJson(overlay::Error("invalid_arguments","invalid UTF-16 argument"))<<'\n'; return 2; }
        std::string text(n,'\0');
        WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,argv[i],-1,text.data(),n,nullptr,nullptr);
        text.pop_back(); args.push_back(std::move(text));
    }
    return overlay::run(args,kVersion,originalParent);
}
#else
int main(int argc,char** argv) {
    const auto originalParent=overlay::parentProcessId();
    return overlay::run(std::vector<std::string>(argv+1,argv+argc),kVersion,originalParent);
}
#endif
