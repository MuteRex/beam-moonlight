#pragma once

#include <QString>

#include "SDL_compat.h"
#include <SDL_ttf.h>

namespace Overlay {

enum OverlayType {
    OverlayDebug,
    OverlayStatusUpdate,
    OverlayControls, // Beam: clickable in-session pill/menu
    OverlayMax
};

class IOverlayRenderer
{
public:
    virtual ~IOverlayRenderer() = default;

    virtual void notifyOverlayUpdated(OverlayType type) = 0;
};

class OverlayManager
{
public:
    OverlayManager();
    ~OverlayManager();

    bool isOverlayEnabled(OverlayType type);
    char* getOverlayText(OverlayType type);
    void updateOverlayText(OverlayType type, const char* text);
    int getOverlayMaxTextLength();
    void setOverlayTextUpdated(OverlayType type);
    void setOverlayState(OverlayType type, bool enabled);
    SDL_Color getOverlayColor(OverlayType type);
    int getOverlayFontSize(OverlayType type);
    SDL_Surface* getUpdatedOverlaySurface(OverlayType type);

    void setOverlayRenderer(IOverlayRenderer* renderer);

    // Beam: overlays drawn from a caller-provided surface rather than text.
    // Takes ownership of the surface. With setEnabled, a null surface disables
    // the overlay and a non-null one enables it; otherwise the surface is only
    // swapped in if the overlay is already enabled.
    void updateOverlaySurface(OverlayType type, SDL_Surface* surface, bool setEnabled = true);

    // Beam: called by renderers to place an overlay anchored top-center. Returns
    // the rect in top-left-origin viewport coordinates and remembers it so input
    // can hit-test against what was actually drawn.
    SDL_Rect placeTopCenterOverlay(OverlayType type, int surfaceWidth, int surfaceHeight,
                                   int viewportWidth, int viewportHeight);

    // Beam: maps window coordinates into the overlay's surface coordinates.
    // Returns false if the point is outside the last drawn overlay rect.
    bool windowPointToOverlay(OverlayType type, int windowX, int windowY,
                              int windowWidth, int windowHeight,
                              int* overlayX, int* overlayY);

private:
    void notifyOverlayUpdated(OverlayType type);
    SDL_Surface* RenderTextOutlinedWrapped(TTF_Font* font, const char* text, SDL_Color textColor, SDL_Color outlineColor, int outlineWidth, int wrapWidth);

    struct {
        bool enabled;
        int fontSize;
        SDL_Color color;
        char text[1024];

        TTF_Font* font;
        SDL_Surface* surface;

        // Beam: retained copy of a custom surface, re-sent to new renderers
        SDL_Surface* customSurface;
        SDL_Rect placedRect;
        int placedViewportWidth;
        int placedViewportHeight;
    } m_Overlays[OverlayMax];
    SDL_SpinLock m_PlacementLock;
    SDL_mutex* m_CustomSurfaceLock;
    IOverlayRenderer* m_Renderer;
    QByteArray m_FontData;
};

}
