#pragma once
#include <atomic>
#include <csignal>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace overlay {
constexpr std::size_t maxBytes = 32 * 1024 * 1024;
constexpr std::uint32_t maxDimension = 8192;
constexpr std::uint64_t maxPixels = 16 * 1024 * 1024;
constexpr std::size_t maxRegions = 512;
struct Error : std::runtime_error {
    std::string code;
    Error(std::string c, const std::string& message) : std::runtime_error(message), code(std::move(c)) {}
};
struct Options {
    std::string command, image, cancelFile;
    bool stdinImage = false, clickable = false;
    double x = 0, y = 0, opacity = 1, scale = 1;
    int timeout = 0, fadeIn = 0, fadeOut = 0, screen = -1;
    std::uint32_t hostPid = 0;
};
struct Screen { double x, y, width, height, scale; };
struct Image {
    std::uint32_t width = 0, height = 0;
    // Top-down premultiplied BGRA, one byte per channel, independent of host endian.
    std::vector<std::uint8_t> pixels;
};
struct Region { int x, y, width, height; };
struct Result { std::string reason; bool accepted = false; double x = 0, y = 0; };
class HostLifetime {
public:
    virtual ~HostLifetime() = default;
    virtual bool exited() = 0;
};
extern volatile std::sig_atomic_t cancelled;
bool validUtf8(const std::string&);
std::string quote(const std::string&);
Options parse(const std::vector<std::string>&);
std::vector<std::uint8_t> readImage(const Options&);
void checkPng(const std::vector<std::uint8_t>&);
std::vector<Region> coverage(const Image&, std::size_t limit = maxRegions);
Image rasterize(const Options&, const Image&);
void validate(const Options&, const Image&, const std::vector<Screen>&);
std::string terminal(const Result&);
Result acceptedClick(const Options&, double globalX, double globalY);
bool timeoutExpired(const Options&, double elapsedMilliseconds);
bool focusedEscapeAllowed(const Options&, bool focused);
double smoothstep(double progress);
void cancelOutcome(Result&, bool& complete);
std::string screenJson(const std::vector<Screen>&);
std::string errorJson(const Error&);
bool controlCancelled(const Options&);
std::vector<Screen> screens();
Image decode(const std::vector<std::uint8_t>&);
std::uint32_t parentProcessId() noexcept;
std::unique_ptr<HostLifetime> watchHost(std::uint32_t pid);
Result show(const Options&, const Image&, HostLifetime&);
int run(const std::vector<std::string>&, std::string_view version, std::uint32_t originalParent = 0);
}
