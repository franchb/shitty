/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "ui_csd_tabs.h"

#include "brand.h"
#include "options.h"
#include "session.h"
#include "composer.h"

#include <lib/vterm/listener.h>

#include <std/str/view.h>
#include <std/lib/buffer.h>
#include <std/mem/obj_pool.h>

#include <plt/window.h>

#define Point MacLegacyPoint
#define Rect MacLegacyRect

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#undef Rect
#undef Point

#include <stdio.h>

using namespace stl;

namespace {
    struct CsdTabsUi;

    // Where the tabs sit in the title bar and how the active one is cut
    // into it: the notch lifts off the seam by inset, rounds its top
    // corners by radius and flares into the seam by fillet.
    struct TabLayout {
        CGFloat left;
        CGFloat cellWidth;
        CGFloat tabsWidth;
        CGFloat inset;
        CGFloat radius;
        CGFloat fillet;
    };

    // The colors of the well. The fill is the terminal's own background;
    // the shade is the dark cut on the material right at the edge, the
    // glow the lit rim one point further out, the lip the faint line on
    // the well's inner wall, mixed from the terminal's foreground so it
    // shows on any background.
    struct WellStyle {
        NSColor* fill;
        NSColor* shade;
        NSColor* glow;
        NSColor* lip;
    };
}

// The iTerm2 look, recessed: the title bar itself, split into tabs by
// hairline separators, with the active tab and the terminal below it
// forming one well sunk into the window's material. The view spans the
// whole title bar so the well's seam runs under the traffic lights too;
// hits left of the tabs fall through to the bar, which keeps dragging
// the window natively. The view owns no model - it reads labels and the
// active index through its owner, which outlives it.
@interface CsdTabBarView: NSView {
@public
    CsdTabsUi* owner;
}
@end

// One solid line of the well's edging: a layer with a background color
// and no backing store, which the compositor draws as a colored quad.
@interface CsdHairlineView: NSView {
    NSColor* color_;
}
- (void)setColor:(NSColor*)color;
@end

// A strip of title-bar material along a window edge, so the well's rim
// matches the bar above it on every background behind the window.
@interface CsdBezelView: NSVisualEffectView
@end

// One top corner of the well, where the rim meets the title bar: the
// well's edge rounds off there, and this view paints the quarter of the
// terminal's background inside the curve, the lifted material outside
// it, and bends the edging lines around it.
@interface CsdWellCornerView: NSView {
@public
    CsdTabsUi* owner;
@public
    BOOL trailing;
}
@end

namespace {
    struct CallSessionsChanged final: public Listener {
        explicit CallSessionsChanged(CsdTabsUi* parent);

        void onListen(void*) override;

        CsdTabsUi* parent;
    };

    struct CallConfigChanged final: public Listener {
        explicit CallConfigChanged(CsdTabsUi* parent);

        void onListen(void*) override;

        CsdTabsUi* parent;
    };

    // Listens to the tab model and mirrors it into the title bar. All
    // AppKit work runs on the main queue: the listener fires on client
    // fibers (the input pump delivers tab chords, the parser fiber
    // delivers titles), and AppKit layout has no business on a fiber
    // stack. The fibers themselves run on the main thread, so the
    // deferred block never races the snapshot it reads.
    struct CsdTabsUi {
        explicit CsdTabsUi(Composer& composer);

        void project();
        void apply();
        void tabSelected(size_t index);
        void tabClosed(size_t index);
        void tabOpened();
        NSWindow* nativeWindow() const;
        TabLayout layout(CGFloat width) const;
        WellStyle style(NSAppearance* appearance) const;
        CGFloat bezelWidth() const;
        bool lipVisible() const;
        void installBezel(NSWindow* window);
        void removeBezel();
        void placeBezel();
        void restyle();
        void logGeometry(NSWindow* window) const;

        Composer& composer;
        CallSessionsChanged sessionsChanged{this};
        CallConfigChanged configChanged{this};
        CsdTabBarView* bar = nil;
        // The projected model snapshot the view draws from; nil hides
        // the strip (a lone session keeps the clean native title).
        NSArray<NSString*>* labels = nil;
        size_t active = 0;
        bool applyPending = false;
        CGFloat tabsLeft = 0;
        // The well's edging around the terminal, all subviews of the
        // content view over the Metal layer: material strips along the
        // three window edges, the hairlines on and beside them, and the
        // two seam lines under the title bar. Owned through bezelViews;
        // the typed pointers place and restyle them.
        NSMutableArray<NSView*>* bezelViews = nil;
        id frameObserver = nil;
        u16 bezelBorder = 0;
        CsdBezelView* strips[3] = {};
        CsdHairlineView* lifts[3] = {};
        CsdHairlineView* sideLines[3][3] = {};
        CsdHairlineView* seamLines[2] = {};
        CsdBezelView* cornerMaterial[2] = {};
        CsdWellCornerView* wellCorners[2] = {};
    };

    static bool csdDarkAppearance(NSAppearance* appearance);
    static NSColor* csdColor(Color color, CGFloat alpha);
}

// The trailing new-tab cell is square-ish; everything left of it is
// split evenly between the tabs. The close glyph answers clicks in a
// fixed leading zone of each tab.
static const CGFloat csdTabPlusWidth = 34;
static const CGFloat csdTabCloseZone = 24;
// The notch of the active tab: how far its top sits below the window
// edge, its top corner radius, and the radius of the flare into the seam.
static const CGFloat csdTabInset = 5;
static const CGFloat csdTabRadius = 7;
static const CGFloat csdTabFillet = 5;
// The widest material rim the well keeps between the window edge and
// its shade line; the terminal's border must leave room for it, which
// the macOS default border does.
static const CGFloat csdBezelMax = 3;
// Lifts the title-bar material under the tabs: the well wants a plate
// visibly lighter than the terminal to be cut into, and the standard
// dark material sits too close to a dark terminal.
static const CGFloat csdPlateLift = 0.08;

namespace {
    static bool csdDarkAppearance(NSAppearance* appearance) {
        if (@available(macOS 10.14, *)) {
            NSAppearanceName const name = [appearance bestMatchFromAppearancesWithNames:@[ NSAppearanceNameAqua, NSAppearanceNameDarkAqua ]];
            return [name isEqualToString:NSAppearanceNameDarkAqua];
        }
        return false;
    }

    // sRGB, the space the terminal itself renders in: a calibrated color
    // would land beside the grid it is supposed to continue.
    static NSColor* csdColor(Color color, CGFloat alpha) {
        return [NSColor colorWithSRGBRed:color.red / 255.0 green:color.green / 255.0 blue:color.blue / 255.0 alpha:alpha];
    }
}

CallSessionsChanged::CallSessionsChanged(CsdTabsUi* parent_)
    : parent(parent_)
{
}

void CallSessionsChanged::onListen(void*) {
    parent->project();
}

CallConfigChanged::CallConfigChanged(CsdTabsUi* parent_)
    : parent(parent_)
{
}

void CallConfigChanged::onListen(void*) {
    parent->project();
}

CsdTabsUi::CsdTabsUi(Composer& composer_)
    : composer(composer_)
{
    composer.sessionsChangedListeners.pushBack(&sessionsChanged);
    // A reload may change the terminal colors or the border the well's
    // rim lives in; the strip repaints and re-places its edging.
    composer.configChangedListeners.pushBack(&configChanged);
}

NSWindow* CsdTabsUi::nativeWindow() const {
    if (composer.window == nullptr) {
        return nil;
    }
    return (__bridge NSWindow*)(composer.window->renderContext().window);
}

TabLayout CsdTabsUi::layout(CGFloat width) const {
    TabLayout result;
    result.left = tabsLeft;
    result.tabsWidth = width - tabsLeft - csdTabPlusWidth;
    if (result.tabsWidth < 0) {
        result.tabsWidth = 0;
    }
    const NSUInteger count = labels.count;
    result.cellWidth = count == 0 ? 0 : result.tabsWidth / (CGFloat)(count);
    // A narrow cell keeps its notch a notch: the curves shrink before
    // they could cross each other.
    const CGFloat quarter = result.cellWidth / 4;
    result.inset = csdTabInset;
    result.radius = csdTabRadius < quarter ? csdTabRadius : quarter;
    result.fillet = csdTabFillet < quarter ? csdTabFillet : quarter;
    return result;
}

WellStyle CsdTabsUi::style(NSAppearance* appearance) const {
    const bool dark = csdDarkAppearance(appearance);
    WellStyle result;
    result.fill = csdColor(composer.opts->vt.bg, 1.0);
    result.lip = csdColor(composer.opts->vt.fg, 0.07);
    result.shade = [NSColor colorWithSRGBRed:0 green:0 blue:0 alpha:dark ? 0.6 : 0.2];
    result.glow = [NSColor colorWithSRGBRed:1 green:1 blue:1 alpha:dark ? 0.1 : 0.6];
    return result;
}

// The material rim between the window edge and the shade line, in
// points. It comes out of the terminal's border: the border's innermost
// point stays bare for the lip, the rest becomes rim up to csdBezelMax.
CGFloat CsdTabsUi::bezelWidth() const {
    const u16 border = composer.opts->border;
    if (border < 1) {
        return 0;
    }
    const CGFloat room = (CGFloat)(border - 1);
    return room < csdBezelMax ? room : csdBezelMax;
}

bool CsdTabsUi::lipVisible() const {
    return composer.opts->border >= 1;
}

void CsdTabsUi::project() {
    SessionSet* const sessions = composer.sessions;
    if (sessions == nullptr || composer.window == nullptr) {
        return;
    }
    const size_t count = sessions->count();
    NSMutableArray<NSString*>* next = nil;
    if (count >= 2) {
        next = [NSMutableArray arrayWithCapacity:(NSUInteger)(count)];
        for (size_t at = 0; at < count; ++at) {
            // A tab whose shell never set a title shows the brand name,
            // like a fresh window does.
            StringView title = sessions->title(at);
            if (title.length() == 0) {
                title = composer.brand->displayName();
            }
            Buffer label(title);
            NSString* const text = [NSString stringWithUTF8String:label.cStr()];
            [next addObject:text == nil ? @"" : text];
        }
    }
    [next retain];
    [labels release];
    labels = next;
    active = sessions->activeIndex();
    if (applyPending) {
        return;
    }
    applyPending = true;
    dispatch_async(dispatch_get_main_queue(), ^{
      apply();
    });
}

void CsdTabsUi::apply() {
    applyPending = false;
    NSWindow* const window = nativeWindow();
    if (window == nil) {
        if (composer.opts->vt.verbose) {
            fprintf(stderr, "%s: tabs: no native window in the render context\n", composer.brand->identifierCString());
        }
        return;
    }
    if (labels == nil) {
        if (bar != nil) {
            removeBezel();
            [bar removeFromSuperview];
            [bar release];
            bar = nil;
            window.titleVisibility = NSWindowTitleVisible;
            if (@available(macOS 11.0, *)) {
                window.titlebarSeparatorStyle = NSTitlebarSeparatorStyleAutomatic;
            }
        }
        return;
    }
    NSButton* const zoom = [window standardWindowButton:NSWindowZoomButton];
    NSView* const titlebar = zoom != nil ? zoom.superview : nil;
    if (titlebar == nil) {
        if (composer.opts->vt.verbose) {
            fprintf(stderr, "%s: tabs: no titlebar container to draw into\n", composer.brand->identifierCString());
        }
        return;
    }
    // The gap before the first tab is bare title bar as far as hit
    // testing goes, so it drags the window natively, double-click zoom
    // included; the view still paints the well's seam under it.
    tabsLeft = NSMaxX(zoom.frame) + 56;
    const NSRect frame = titlebar.bounds;
    if (bar == nil) {
        bar = [[CsdTabBarView alloc] initWithFrame:frame];
        bar.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        bar->owner = this;
        // Below the traffic lights: the seam runs under them, the
        // buttons stay on top.
        [titlebar addSubview:bar positioned:NSWindowBelow relativeTo:zoom];
        window.titleVisibility = NSWindowTitleHidden;
        // The automatic style draws a hard rule under the title bar on
        // some releases - straight through the seam where the active tab
        // continues into its terminal.
        if (@available(macOS 11.0, *)) {
            window.titlebarSeparatorStyle = NSTitlebarSeparatorStyleNone;
        }
        if (composer.opts->vt.verbose) {
            fprintf(stderr, "%s: tabs: strip installed over the title bar\n", composer.brand->identifierCString());
        }
    } else {
        bar.frame = frame;
    }
    if (bezelViews != nil && bezelBorder != composer.opts->border) {
        removeBezel();
    }
    if (bezelViews == nil) {
        installBezel(window);
    }
    restyle();
    placeBezel();
    bar.needsDisplay = YES;
    if (composer.opts->vt.verbose) {
        logGeometry(window);
    }
}

namespace {
    static void csdLogSubtree(const char* prefix, NSView* view, int depth) {
        const NSRect frame = view.frame;
        const NSRect bounds = view.bounds;
        fprintf(stderr, "%s: tabs: %*s%s frame=(%g,%g %gx%g) bounds=(%g,%g %gx%g)%s\n", prefix, depth * 2, "", view.className.UTF8String, frame.origin.x, frame.origin.y, frame.size.width, frame.size.height, bounds.origin.x, bounds.origin.y, bounds.size.width, bounds.size.height, view.hidden ? " hidden" : "");
        if (depth >= 3) {
            return;
        }
        for (NSView* child in view.subviews) {
            csdLogSubtree(prefix, child, depth + 1);
        }
    }
}

// Everything the seam depends on, for a bug report from a machine this
// code cannot be run on: where the title bar, its container and the
// content view actually sit, and what AppKit put between them.
void CsdTabsUi::logGeometry(NSWindow* window) const {
    const char* const prefix = composer.brand->identifierCString();
    const NSRect frame = window.frame;
    const NSRect content = window.contentView.frame;
    const NSRect layout = window.contentLayoutRect;
    fprintf(stderr, "%s: tabs: window frame=(%g,%g %gx%g) scale=%g content frame=(%g,%g %gx%g) contentLayoutRect=(%g,%g %gx%g) border=%u bezel=%g\n", prefix, frame.origin.x, frame.origin.y, frame.size.width, frame.size.height, window.backingScaleFactor, content.origin.x, content.origin.y, content.size.width, content.size.height, layout.origin.x, layout.origin.y, layout.size.width, layout.size.height, (unsigned)(composer.opts->border), bezelWidth());
    NSView* titlebar = bar.superview;
    NSView* root = titlebar;
    while (root.superview != nil && root.superview != window.contentView.superview) {
        root = root.superview;
    }
    if (root != nil) {
        csdLogSubtree(prefix, root, 0);
    }
    if (titlebar != nil) {
        const NSRect inWindow = [titlebar convertRect:titlebar.bounds toView:nil];
        fprintf(stderr, "%s: tabs: titlebar in window coordinates=(%g,%g %gx%g)\n", prefix, inWindow.origin.x, inWindow.origin.y, inWindow.size.width, inWindow.size.height);
    }
}

void CsdTabsUi::installBezel(NSWindow* window) {
    NSView* const content = window.contentView;
    if (content == nil) {
        return;
    }
    bezelBorder = composer.opts->border;
    bezelViews = [[NSMutableArray alloc] init];
    const CGFloat bezel = bezelWidth();
    const bool lip = lipVisible();
    const auto add = [&](NSView* view) {
        view.wantsLayer = YES;
        [content addSubview:view];
        [bezelViews addObject:view];
        [view release];
    };
    // Strips first, so every line lands above the material; each strip
    // wears the same lift as the title bar, so plate and rim are one.
    NSColor* const liftColor = [NSColor colorWithSRGBRed:1 green:1 blue:1 alpha:csdPlateLift];
    for (size_t side = 0; side < 3; ++side) {
        if (bezel >= 1) {
            strips[side] = [[CsdBezelView alloc] initWithFrame:NSZeroRect];
            add(strips[side]);
            lifts[side] = [[CsdHairlineView alloc] initWithFrame:NSZeroRect];
            [lifts[side] setColor:liftColor];
            add(lifts[side]);
        }
    }
    for (size_t side = 0; side < 3; ++side) {
        for (size_t line = 0; line < 3; ++line) {
            const bool present = line == 0 ? lip : line == 1 ? bezel >= 1 : bezel >= 2;
            if (!present) {
                continue;
            }
            sideLines[side][line] = [[CsdHairlineView alloc] initWithFrame:NSZeroRect];
            add(sideLines[side][line]);
        }
    }
    if (lip) {
        for (size_t at = 0; at < 2; ++at) {
            seamLines[at] = [[CsdHairlineView alloc] initWithFrame:NSZeroRect];
            add(seamLines[at]);
        }
    }
    if (bezel >= 1) {
        for (size_t at = 0; at < 2; ++at) {
            cornerMaterial[at] = [[CsdBezelView alloc] initWithFrame:NSZeroRect];
            add(cornerMaterial[at]);
            wellCorners[at] = [[CsdWellCornerView alloc] initWithFrame:NSZeroRect];
            wellCorners[at]->owner = this;
            wellCorners[at]->trailing = at == 1;
            add(wellCorners[at]);
        }
    }
    // The seam lines end where the active tab flares out, which moves
    // with the window width; every other frame is replaced along with
    // them, in the same transaction as the content view's own resize.
    content.postsFrameChangedNotifications = YES;
    frameObserver = [[NSNotificationCenter.defaultCenter addObserverForName:NSViewFrameDidChangeNotification object:content queue:nil usingBlock:^(NSNotification* note) {
      (void)note;
      placeBezel();
    }] retain];
}

void CsdTabsUi::removeBezel() {
    if (frameObserver != nil) {
        [NSNotificationCenter.defaultCenter removeObserver:frameObserver];
        [frameObserver release];
        frameObserver = nil;
    }
    for (NSView* view in bezelViews) {
        [view removeFromSuperview];
    }
    [bezelViews release];
    bezelViews = nil;
    for (size_t side = 0; side < 3; ++side) {
        strips[side] = nil;
        lifts[side] = nil;
        for (size_t line = 0; line < 3; ++line) {
            sideLines[side][line] = nil;
        }
    }
    for (size_t at = 0; at < 2; ++at) {
        seamLines[at] = nil;
        cornerMaterial[at] = nil;
        wellCorners[at] = nil;
    }
}

void CsdTabsUi::placeBezel() {
    NSWindow* const window = nativeWindow();
    if (window == nil || bezelViews == nil) {
        return;
    }
    const NSRect bounds = window.contentView.bounds;
    const CGFloat width = bounds.size.width;
    const CGFloat height = bounds.size.height;
    const CGFloat bezel = bezelWidth();
    // With a rim the well's top corners round off by the tab fillet;
    // the side lines stop under the curve and the corner views take
    // over. Without one the lines run straight up to the seam.
    const CGFloat corner = strips[0] != nil ? csdTabFillet : 0;
    const CGFloat rise = height > corner ? height - corner : 0;
    if (strips[0] != nil) {
        strips[0].frame = NSMakeRect(0, 0, bezel, height);
        strips[1].frame = NSMakeRect(width - bezel, 0, bezel, height);
        strips[2].frame = NSMakeRect(0, 0, width, bezel);
        for (size_t side = 0; side < 3; ++side) {
            lifts[side].frame = strips[side].frame;
        }
        cornerMaterial[0].frame = NSMakeRect(bezel, rise, corner, corner);
        cornerMaterial[1].frame = NSMakeRect(width - bezel - corner, rise, corner, corner);
        wellCorners[0].frame = NSMakeRect(0, rise, bezel + corner, corner);
        wellCorners[1].frame = NSMakeRect(width - bezel - corner, rise, bezel + corner, corner);
        wellCorners[0].needsDisplay = YES;
        wellCorners[1].needsDisplay = YES;
    }
    // Line 0 is the lip on the terminal's own border, line 1 the shade
    // at the rim's inner edge, line 2 the glow beside it: each one point
    // further out from the well. They run straight into the corners;
    // the window's own corner mask trims them the way it trims the
    // window's edge, whatever radius this release rounds with.
    for (size_t line = 0; line < 3; ++line) {
        const CGFloat distance = bezel - (CGFloat)(line);
        if (sideLines[0][line] != nil) {
            sideLines[0][line].frame = NSMakeRect(distance, 0, 1, rise);
        }
        if (sideLines[1][line] != nil) {
            sideLines[1][line].frame = NSMakeRect(width - distance - 1, 0, 1, rise);
        }
        if (sideLines[2][line] != nil) {
            sideLines[2][line].frame = NSMakeRect(0, distance, width, 1);
        }
    }
    if (seamLines[0] != nil) {
        const TabLayout tabs = layout(width);
        const CGFloat begin = tabs.left + tabs.cellWidth * (CGFloat)(active) - tabs.fillet;
        const CGFloat end = begin + tabs.cellWidth + 2 * tabs.fillet;
        // The seam lip runs between the well's rounded top corners and
        // the active tab's flares.
        const CGFloat from = bezel + corner;
        const CGFloat to = width - bezel - corner;
        const CGFloat leading = begin > from ? begin - from : 0;
        const CGFloat trailing = to > end ? to - end : 0;
        seamLines[0].frame = NSMakeRect(from, height - 1, leading, 1);
        seamLines[1].frame = NSMakeRect(end, height - 1, trailing, 1);
    }
}

void CsdTabsUi::restyle() {
    NSWindow* const window = nativeWindow();
    if (window == nil) {
        return;
    }
    const WellStyle colors = style(window.effectiveAppearance);
    NSColor* const byLine[3] = {colors.lip, colors.shade, colors.glow};
    for (size_t side = 0; side < 3; ++side) {
        for (size_t line = 0; line < 3; ++line) {
            if (sideLines[side][line] != nil) {
                [sideLines[side][line] setColor:byLine[line]];
            }
        }
    }
    for (size_t at = 0; at < 2; ++at) {
        if (seamLines[at] != nil) {
            [seamLines[at] setColor:colors.lip];
        }
        if (wellCorners[at] != nil) {
            wellCorners[at].needsDisplay = YES;
        }
    }
    if (bar != nil) {
        bar.needsDisplay = YES;
    }
}

void CsdTabsUi::tabSelected(size_t index) {
    SessionSet* const sessions = composer.sessions;
    if (sessions == nullptr || index >= sessions->count()) {
        return;
    }
    sessions->activate(index);
    composer.window->requestFrame();
}

void CsdTabsUi::tabClosed(size_t index) {
    SessionSet* const sessions = composer.sessions;
    if (sessions == nullptr || index >= sessions->count()) {
        return;
    }
    if (sessions->close(index)) {
        composer.window->requestFrame();
    } else {
        // The strip only shows with two or more tabs, so this is
        // unreachable in practice; the chord path's semantics anyway.
        composer.window->requestClose();
    }
}

void CsdTabsUi::tabOpened() {
    SessionSet* const sessions = composer.sessions;
    if (sessions == nullptr) {
        return;
    }
    sessions->newSession();
    composer.window->requestFrame();
}

@implementation CsdTabBarView

- (BOOL)mouseDownCanMoveWindow {
    return NO;
}

- (NSView*)hitTest:(NSPoint)point {
    const NSPoint local = [self convertPoint:point fromView:self.superview];
    if (local.x < owner->tabsLeft) {
        return nil;
    }
    return [super hitTest:point];
}

- (void)viewDidChangeEffectiveAppearance {
    [super viewDidChangeEffectiveAppearance];
    owner->restyle();
}

- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    NSArray<NSString*>* const labels = owner->labels;
    const NSUInteger count = labels.count;
    if (count == 0) {
        return;
    }
    const NSUInteger active = (NSUInteger)(owner->active);
    const NSRect bounds = self.bounds;
    const TabLayout tabs = owner->layout(bounds.size.width);
    const WellStyle colors = owner->style(self.effectiveAppearance);
    const CGFloat cellWidth = tabs.cellWidth;
    const CGFloat height = bounds.size.height;
    // The notch of the active tab, drawn from the seam up and back down
    // to it, y up: a flare out of the seam, two rounded top corners, a
    // flare back in.
    const CGFloat left = tabs.left + cellWidth * (CGFloat)(active);
    const CGFloat right = left + cellWidth;
    const CGFloat fillet = tabs.fillet;
    const CGFloat radius = tabs.radius;
    const CGFloat top = height - tabs.inset;
    NSBezierPath* const notch = [NSBezierPath bezierPath];
    [notch moveToPoint:NSMakePoint(left - fillet, 0)];
    [notch appendBezierPathWithArcWithCenter:NSMakePoint(left - fillet, fillet) radius:fillet startAngle:270 endAngle:360 clockwise:NO];
    [notch lineToPoint:NSMakePoint(left, top - radius)];
    [notch appendBezierPathWithArcWithCenter:NSMakePoint(left + radius, top - radius) radius:radius startAngle:180 endAngle:90 clockwise:YES];
    [notch lineToPoint:NSMakePoint(right - radius, top)];
    [notch appendBezierPathWithArcWithCenter:NSMakePoint(right - radius, top - radius) radius:radius startAngle:90 endAngle:0 clockwise:YES];
    [notch lineToPoint:NSMakePoint(right, fillet)];
    [notch appendBezierPathWithArcWithCenter:NSMakePoint(right + fillet, fillet) radius:fillet startAngle:180 endAngle:270 clockwise:NO];
    // The well outline: the seam along the whole bar, lifted into the
    // notch. Strokes centered on it leave their outer half on the
    // material; the fill covers the inner half.
    // With a rim the seam starts and ends where the well's rounded top
    // corners come up to it; the curve itself lies mostly below the
    // seam, drawn by the corner views, and only the outer half of the
    // strokes shows up here. Without a rim the seam spans the bar.
    const CGFloat bezel = owner->bezelWidth();
    const CGFloat corner = bezel >= 1 ? csdTabFillet : 0;
    const CGFloat width = bounds.size.width;
    NSBezierPath* const outline = [NSBezierPath bezierPath];
    if (corner > 0) {
        [outline moveToPoint:NSMakePoint(bezel, -corner)];
        [outline appendBezierPathWithArcWithCenter:NSMakePoint(bezel + corner, -corner) radius:corner startAngle:180 endAngle:90 clockwise:YES];
    } else {
        [outline moveToPoint:NSMakePoint(0, 0)];
    }
    [outline lineToPoint:NSMakePoint(left - fillet, 0)];
    [outline appendBezierPath:notch];
    if (corner > 0) {
        [outline lineToPoint:NSMakePoint(width - bezel - corner, 0)];
        [outline appendBezierPathWithArcWithCenter:NSMakePoint(width - bezel - corner, -corner) radius:corner startAngle:90 endAngle:0 clockwise:YES];
    } else {
        [outline lineToPoint:NSMakePoint(width, 0)];
    }
    outline.lineJoinStyle = NSLineJoinStyleRound;
    // The fill closes below the view's bottom edge, so it reaches the
    // seam exactly and never covers the seam's own strokes.
    NSBezierPath* const well = [[notch copy] autorelease];
    [well lineToPoint:NSMakePoint(right + fillet, -2)];
    [well lineToPoint:NSMakePoint(left - fillet, -2)];
    [well closePath];
    // The plate: the material lifted evenly, so the well is cut into
    // something visibly lighter than the terminal it holds.
    [[NSColor colorWithSRGBRed:1 green:1 blue:1 alpha:csdPlateLift] setFill];
    NSRectFillUsingOperation(bounds, NSCompositingOperationSourceOver);
    outline.lineWidth = 4;
    [colors.glow setStroke];
    [outline stroke];
    outline.lineWidth = 2;
    [colors.shade setStroke];
    [outline stroke];
    // The active tab is a piece of the terminal it fronts: its cell
    // wears the terminal's background and foreground. Idle tabs stay
    // bare, so the title bar's own material shows through.
    [colors.fill setFill];
    [well fill];
    if (owner->lipVisible()) {
        [NSGraphicsContext saveGraphicsState];
        [well addClip];
        [colors.lip setStroke];
        [outline stroke];
        [NSGraphicsContext restoreGraphicsState];
    }
    const Color terminalForeground = owner->composer.opts->vt.fg;
    NSColor* const activeText = csdColor(terminalForeground, 1.0);
    NSColor* const activeGlyphs = [activeText colorWithAlphaComponent:0.75];
    // The strip is our own surface, and the system label tiers are tuned
    // for controls on the standard material: tertiary label over a dark
    // title bar measures 1.16:1 against it (issue 84), which is nothing.
    // Every idle tier moves one step up, and the hairlines are mixed from
    // the label color so they keep following the appearance.
    NSColor* const idleText = NSColor.labelColor;
    NSColor* const idleGlyphs = NSColor.secondaryLabelColor;
    NSColor* const hairline = [NSColor.labelColor colorWithAlphaComponent:0.4];
    NSMutableParagraphStyle* const centered = [[[NSMutableParagraphStyle alloc] init] autorelease];
    centered.alignment = NSTextAlignmentCenter;
    // Long shell titles differ at the tail; keep it, iTerm style.
    centered.lineBreakMode = NSLineBreakByTruncatingHead;
    NSFont* const activeFont = [NSFont titleBarFontOfSize:0];
    NSFont* const idleFont = [NSFont systemFontOfSize:activeFont.pointSize];
    NSDictionary* const activeAttributes = @{
        NSFontAttributeName : activeFont,
        NSForegroundColorAttributeName : activeText,
        NSParagraphStyleAttributeName : centered,
    };
    NSDictionary* const idleAttributes = @{
        NSFontAttributeName : idleFont,
        NSForegroundColorAttributeName : idleText,
        NSParagraphStyleAttributeName : centered,
    };
    NSDictionary* const activeGlyphAttributes = @{
        NSFontAttributeName : activeFont,
        NSForegroundColorAttributeName : activeGlyphs,
    };
    NSDictionary* const idleGlyphAttributes = @{
        NSFontAttributeName : idleFont,
        NSForegroundColorAttributeName : idleGlyphs,
    };
    const auto drawGlyph = [&](NSString* glyph, CGFloat x, NSDictionary* attributes) {
        const NSSize size = [glyph sizeWithAttributes:attributes];
        [glyph drawAtPoint:NSMakePoint(x, bounds.origin.y + (height - size.height) / 2) withAttributes:attributes];
    };
    for (NSUInteger at = 0; at < count; ++at) {
        const NSRect cell = NSMakeRect(tabs.left + cellWidth * (CGFloat)(at), bounds.origin.y, cellWidth, height);
        // Hairlines separate bare cells only; the well draws its own
        // edges. The leftmost tab has the drag gap to its left, and that
        // seam wants the same line unless the tab itself is the well.
        if (at != active && (at == 0 || at - 1 != active)) {
            [hairline setFill];
            NSRectFillUsingOperation(NSMakeRect(cell.origin.x, cell.origin.y + 7, 1, height - 14), NSCompositingOperationSourceOver);
        }
        NSDictionary* const glyphAttributes = at == active ? activeGlyphAttributes : idleGlyphAttributes;
        drawGlyph(@"×", cell.origin.x + 9, glyphAttributes);
        CGFloat trailing = 8;
        if (at < 9) {
            NSString* const hint = [NSString stringWithFormat:@"⌘%u", (unsigned)(at + 1)];
            const NSSize hintSize = [hint sizeWithAttributes:glyphAttributes];
            trailing += hintSize.width + 8;
            drawGlyph(hint, NSMaxX(cell) - 8 - hintSize.width, glyphAttributes);
        }
        NSDictionary* const attributes = at == active ? activeAttributes : idleAttributes;
        NSString* const label = labels[at];
        const NSSize size = [label sizeWithAttributes:attributes];
        const CGFloat leading = csdTabCloseZone;
        const CGFloat available = cell.size.width - leading - trailing;
        if (available <= 0) {
            continue;
        }
        const NSRect text = NSMakeRect(cell.origin.x + leading, cell.origin.y + (height - size.height) / 2, available, size.height);
        [label drawWithRect:text options:NSStringDrawingUsesLineFragmentOrigin attributes:attributes context:nil];
    }
    // The trailing new-tab cell: bare material, a plus, and a hairline
    // against the last tab unless the well already draws that edge.
    const CGFloat plusLeft = tabs.left + tabs.tabsWidth;
    if (count - 1 != active) {
        [hairline setFill];
        NSRectFillUsingOperation(NSMakeRect(plusLeft, bounds.origin.y + 7, 1, height - 14), NSCompositingOperationSourceOver);
    }
    NSString* const plus = @"+";
    const NSSize plusSize = [plus sizeWithAttributes:idleGlyphAttributes];
    drawGlyph(plus, plusLeft + (csdTabPlusWidth - plusSize.width) / 2, idleGlyphAttributes);
}

- (void)mouseDown:(NSEvent*)event {
    const NSUInteger count = owner->labels.count;
    if (count == 0) {
        return;
    }
    const NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    const TabLayout tabs = owner->layout(self.bounds.size.width);
    const CGFloat x = point.x - tabs.left;
    if (x < 0) {
        return;
    }
    if (x >= tabs.tabsWidth) {
        owner->tabOpened();
        return;
    }
    NSUInteger index = (NSUInteger)(x / tabs.cellWidth);
    if (index >= count) {
        index = count - 1;
    }
    if (x - tabs.cellWidth * (CGFloat)(index) < csdTabCloseZone) {
        owner->tabClosed((size_t)(index));
        return;
    }
    owner->tabSelected((size_t)(index));
}

@end

@implementation CsdHairlineView

- (void)dealloc {
    [color_ release];
    [super dealloc];
}

- (void)setColor:(NSColor*)color {
    [color retain];
    [color_ release];
    color_ = color;
    self.needsDisplay = YES;
}

- (BOOL)wantsUpdateLayer {
    return YES;
}

- (void)updateLayer {
    self.layer.backgroundColor = color_.CGColor;
}

- (NSView*)hitTest:(NSPoint)point {
    (void)point;
    return nil;
}

@end

@implementation CsdBezelView

- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (self != nil) {
        self.material = NSVisualEffectMaterialTitlebar;
        self.blendingMode = NSVisualEffectBlendingModeBehindWindow;
        self.state = NSVisualEffectStateFollowsWindowActiveState;
    }
    return self;
}

- (NSView*)hitTest:(NSPoint)point {
    (void)point;
    return nil;
}

@end

@implementation CsdWellCornerView

- (NSView*)hitTest:(NSPoint)point {
    (void)point;
    return nil;
}

- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    const WellStyle colors = owner->style(self.effectiveAppearance);
    const CGFloat bezel = owner->bezelWidth();
    const CGFloat corner = csdTabFillet;
    // The material square beside the rim, lifted like the rest of the
    // plate; the quarter of the well inside the curve is filled over it.
    const NSRect square = trailing ? NSMakeRect(0, 0, corner, corner) : NSMakeRect(bezel, 0, corner, corner);
    [[NSColor colorWithSRGBRed:1 green:1 blue:1 alpha:csdPlateLift] setFill];
    NSRectFillUsingOperation(square, NSCompositingOperationSourceOver);
    // The well's corner circle: its center sits at the bottom of this
    // view, a fillet in from the rim.
    const NSPoint center = trailing ? NSMakePoint(0, 0) : NSMakePoint(bezel + corner, 0);
    const CGFloat startAngle = trailing ? 90 : 180;
    const CGFloat endAngle = trailing ? 0 : 90;
    NSBezierPath* const quarter = [NSBezierPath bezierPath];
    [quarter moveToPoint:center];
    [quarter appendBezierPathWithArcWithCenter:center radius:corner startAngle:startAngle endAngle:endAngle clockwise:YES];
    [quarter closePath];
    [colors.fill setFill];
    [quarter fill];
    // The same lines as along the rim, bent around the curve: the glow
    // and the shade outside it, the lip inside.
    const auto drawLine = [&](CGFloat radius, NSColor* color) {
        NSBezierPath* const arc = [NSBezierPath bezierPath];
        [arc appendBezierPathWithArcWithCenter:center radius:radius startAngle:startAngle endAngle:endAngle clockwise:YES];
        arc.lineWidth = 1;
        [color setStroke];
        [arc stroke];
    };
    if (bezel >= 2) {
        drawLine(corner + 1.5, colors.glow);
    }
    drawLine(corner + 0.5, colors.shade);
    if (owner->lipVisible()) {
        drawLine(corner - 0.5, colors.lip);
    }
}

@end

void createCsdTabsUi(ObjPool& owner, Composer& composer) {
    owner.make<CsdTabsUi>(composer);
}
