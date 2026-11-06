#import <AppKit/AppKit.h>
#import <ImageIO/ImageIO.h>
#include "overlay.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cerrno>
#include <memory>
#include <sys/event.h>
#include <unistd.h>

namespace {
struct Session {
    overlay::Result result;
    bool complete=false;
    double mainTop=0;
    const overlay::Options* options=nullptr;
};
}
@interface OverlayPanel : NSPanel
@end
@implementation OverlayPanel
- (BOOL)canBecomeKeyWindow { return NO; }
- (BOOL)canBecomeMainWindow { return NO; }
@end
@interface OverlayView : NSView
@property(nonatomic, strong) NSImage* image;
@property(nonatomic) NSRect destination;
@property(nonatomic) Session* session;
@end
@implementation OverlayView
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return NO; }
- (BOOL)acceptsFirstMouse:(NSEvent*)event { (void)event; return YES; }
- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    [[NSGraphicsContext currentContext] setImageInterpolation:NSImageInterpolationNone];
    [self.image drawInRect:self.destination fromRect:NSZeroRect operation:NSCompositingOperationCopy
                  fraction:1 respectFlipped:YES hints:@{NSImageHintInterpolation:@(NSImageInterpolationNone)}];
}
- (void)mouseDown:(NSEvent*)event {
    if (!self.session || self.session->complete) return;
    NSPoint location=[self.window convertPointToScreen:event.locationInWindow];
    self.session->result=overlay::acceptedClick(*self.session->options,location.x,self.session->mainTop-location.y);
    self.session->complete=true;
}
@end

namespace overlay {
std::vector<Screen> screens() {
    @autoreleasepool {
        NSArray<NSScreen*>* list=[NSScreen screens];
        if (!list.count) return {};
        const double top=NSMaxY(list[0].frame);
        std::vector<Screen> out;
        for (NSScreen* screen in list) {
            NSRect r=screen.frame;
            out.push_back({r.origin.x,top-NSMaxY(r),r.size.width,r.size.height,screen.backingScaleFactor});
        }
        return out;
    }
}
Image decode(const std::vector<std::uint8_t>& bytes) {
    @autoreleasepool {
        checkPng(bytes);
        CFDataRef data=CFDataCreate(kCFAllocatorDefault,bytes.data(),bytes.size());
        if (!data) throw Error("image_decode","cannot allocate encoded PNG");
        CGImageSourceRef source=CGImageSourceCreateWithData(data,nullptr);
        CFRelease(data);
        if (!source) throw Error("image_decode","ImageIO cannot open PNG");
        const bool png=CGImageSourceGetType(source) && CFEqual(CGImageSourceGetType(source),CFSTR("public.png"));
        CGImageRef decoded=png && CGImageSourceGetCount(source)==1 && CGImageSourceGetStatus(source)==kCGImageStatusComplete?
            CGImageSourceCreateImageAtIndex(source,0,(__bridge CFDictionaryRef)@{(id)kCGImageSourceShouldCacheImmediately:@YES}):nullptr;
        CFRelease(source);
        if (!decoded) throw Error("image_decode","ImageIO cannot decode a single PNG");
        Image out; out.width=std::uint32_t(CGImageGetWidth(decoded)); out.height=std::uint32_t(CGImageGetHeight(decoded));
        if (!out.width || !out.height || out.width>maxDimension || out.height>maxDimension ||
            std::uint64_t(out.width)*out.height>maxPixels) {
            CGImageRelease(decoded); throw Error("image_limit","decoded PNG exceeds limits");
        }
        out.pixels.resize(std::size_t(out.width)*out.height*4);
        CGColorSpaceRef colors=CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        CGContextRef context=CGBitmapContextCreate(out.pixels.data(),out.width,out.height,8,out.width*4,
            colors,kCGImageAlphaPremultipliedFirst|kCGBitmapByteOrder32Little);
        CGColorSpaceRelease(colors);
        if (!context) { CGImageRelease(decoded); throw Error("image_decode","cannot create PNG bitmap"); }
        CGContextSetBlendMode(context,kCGBlendModeCopy);
        CGContextDrawImage(context,CGRectMake(0,0,out.width,out.height),decoded);
        CGContextRelease(context); CGImageRelease(decoded);
        return out;
    }
}
std::uint32_t parentProcessId() noexcept { return std::uint32_t(getppid()); }
class HostWatch final : public HostLifetime {
    int fd=-1;
    bool dead=false;
public:
    explicit HostWatch(std::uint32_t pid) {
        if (!pid) throw Error("host_watch","host PID must be positive");
        fd=kqueue();
        if (fd<0) throw Error("host_watch","cannot create host process watcher");
        struct kevent change;
        EV_SET(&change,pid,EVFILT_PROC,EV_ADD|EV_ENABLE,NOTE_EXIT,0,nullptr);
        if (kevent(fd,&change,1,nullptr,0,nullptr)<0) {
            const int error=errno; close(fd); fd=-1;
            if (error==ESRCH) dead=true;
            else throw Error("host_watch","cannot register host process lifetime");
        }
    }
    ~HostWatch() override { if (fd>=0) close(fd); }
    bool exited() override {
        if (dead) return true;
        if (fd<0) return false;
        struct kevent event; timespec zero{0,0};
        const int n=kevent(fd,nullptr,0,&event,1,&zero);
        if (n<0 && errno!=EINTR) throw Error("host_watch","cannot read host process watcher");
        if (n>0 && (event.fflags&NOTE_EXIT)) dead=true;
        return dead;
    }
};
std::unique_ptr<HostLifetime> watchHost(std::uint32_t pid) { return std::make_unique<HostWatch>(pid); }
static std::vector<std::uint32_t> displayIds() {
    std::vector<std::uint32_t> ids;
    for (NSScreen* s in [NSScreen screens])
        ids.push_back([s.deviceDescription[@"NSScreenNumber"] unsignedIntValue]);
    return ids;
}
static bool sameBounds(const std::vector<Screen>& a,const std::vector<Screen>& b) {
    if (a.size()!=b.size()) return false;
    for (std::size_t i=0;i<a.size();++i)
        if (a[i].x!=b[i].x || a[i].y!=b[i].y || a[i].width!=b[i].width ||
            a[i].height!=b[i].height || a[i].scale!=b[i].scale) return false;
    return true;
}
Result show(const Options& o,const Image& source,HostLifetime& host) {
    @autoreleasepool {
        if (controlCancelled(o)) return {"cancel"};
        if (host.exited()) return {"host-exit"};
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
        [NSApp finishLaunching];
        const auto initialDisplays=displayIds();
        auto current=screens();
        validate(o,source,current);
        auto image=rasterize(o,source);
        NSScreen* main=[NSScreen screens].firstObject;
        if (!main) return {"screen-disconnect"};
        const double mainTop=NSMaxY(main.frame);
        Session session; session.mainTop=mainTop; session.options=&o;
        CGColorSpaceRef color=CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        CGDataProviderRef provider=CGDataProviderCreateWithData(nullptr,image.pixels.data(),image.pixels.size(),nullptr);
        CGImageRef bitmap=CGImageCreate(image.width,image.height,8,32,image.width*4,color,
            kCGImageAlphaPremultipliedFirst|kCGBitmapByteOrder32Little,provider,nullptr,false,kCGRenderingIntentDefault);
        CGDataProviderRelease(provider); CGColorSpaceRelease(color);
        if (!bitmap) throw Error("native_error","cannot create overlay image");
        NSImage* drawing=[[NSImage alloc] initWithCGImage:bitmap size:NSMakeSize(image.width,image.height)];
        CGImageRelease(bitmap);
        std::vector<Region> regions=o.clickable?coverage(image):std::vector<Region>{{0,0,int(image.width),int(image.height)}};
        NSMutableArray<OverlayPanel*>* windows=[NSMutableArray array];
        auto start=std::chrono::steady_clock::now();
        @try {
            for (const auto& r:regions) {
                if (controlCancelled(o)) return {"cancel"};
                if (host.exited()) return {"host-exit"};
                if (timeoutExpired(o,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()))
                    return {"timeout"};
                NSRect frame=NSMakeRect(o.x+r.x,mainTop-o.y-r.y-r.height,r.width,r.height);
                OverlayPanel* panel=[[OverlayPanel alloc] initWithContentRect:frame
                    styleMask:NSWindowStyleMaskBorderless|NSWindowStyleMaskNonactivatingPanel backing:NSBackingStoreBuffered defer:NO];
                panel.releasedWhenClosed=NO;
                panel.opaque=NO; panel.backgroundColor=NSColor.clearColor;
                panel.hasShadow=NO; panel.level=NSFloatingWindowLevel;
                panel.hidesOnDeactivate=NO; panel.becomesKeyOnlyIfNeeded=YES;
                panel.collectionBehavior=NSWindowCollectionBehaviorCanJoinAllSpaces|NSWindowCollectionBehaviorStationary|NSWindowCollectionBehaviorFullScreenAuxiliary;
                panel.ignoresMouseEvents=!o.clickable || o.fadeIn>0;
                OverlayView* view=[[OverlayView alloc] initWithFrame:NSMakeRect(0,0,frame.size.width,frame.size.height)];
                view.image=drawing; view.session=&session;
                view.destination=NSMakeRect(-r.x,-r.y,image.width,image.height);
                panel.contentView=view; panel.alphaValue=o.fadeIn?0:1;
                [windows addObject:panel]; [panel orderFrontRegardless];
            }
            auto endFade=start;
            bool fading=false;
            double fadeStart=1;
            while (true) {
              @autoreleasepool {
                if (controlCancelled(o)) { cancelOutcome(session.result,session.complete); break; }
                if (host.exited()) { if (!session.complete) session.result={"host-exit"}; break; }
                if (displayIds()!=initialDisplays || !sameBounds(current,screens())) {
                    if (!session.complete) session.result={"screen-disconnect"};
                    break;
                }
                const auto now=std::chrono::steady_clock::now();
                double elapsed=std::chrono::duration<double,std::milli>(now-start).count();
                double alpha=o.fadeIn?smoothstep(elapsed/o.fadeIn):1;
                if (!session.complete && timeoutExpired(o,elapsed)) { session.result={"timeout"}; session.complete=true; }
                if (session.complete && !fading) {
                    if (!o.fadeOut) break;
                    fading=true; endFade=now; fadeStart=alpha;
                }
                if (fading) {
                    double t=std::chrono::duration<double,std::milli>(now-endFade).count()/o.fadeOut;
                    if (t>=1) break;
                    alpha=fadeStart*(1-smoothstep(t));
                }
                const double frameAlpha=std::round(alpha*255)/255;
                for (OverlayPanel* panel in windows) {
                    panel.alphaValue=frameAlpha;
                    panel.ignoresMouseEvents=!o.clickable || fading || frameAlpha<=0;
                }
                NSEvent* event=[NSApp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]
                    inMode:NSDefaultRunLoopMode dequeue:YES];
                if (event) {
                    if (event.type==NSEventTypeKeyDown && event.keyCode==53 &&
                        focusedEscapeAllowed(o,event.window.isKeyWindow && [(NSArray*)windows containsObject:event.window]) && !session.complete) {
                        session.result={"escape"}; session.complete=true;
                    }
                    else [NSApp sendEvent:event];
                }
                [NSApp updateWindows];
              }
            }
        } @finally {
            for (OverlayPanel* panel in windows) { [panel orderOut:nil]; [panel close]; }
        }
        return session.result;
    }
}
}
