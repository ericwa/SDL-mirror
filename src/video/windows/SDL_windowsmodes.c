/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2017 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#include "../../SDL_internal.h"

#if SDL_VIDEO_DRIVER_WINDOWS

#include "SDL_windowsvideo.h"
#include "../../../include/SDL_assert.h"
#include "../../../include/SDL_log.h"

/* Windows CE compatibility */
#ifndef CDS_FULLSCREEN
#define CDS_FULLSCREEN 0
#endif

/* #define DEBUG_MODES */

static void
WIN_UpdateDisplayMode(_THIS, LPCTSTR deviceName, DWORD index, SDL_DisplayMode * mode)
{
    SDL_DisplayModeData *data = (SDL_DisplayModeData *) mode->driverdata;
    HDC hdc;

    data->DeviceMode.dmFields =
        (DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY |
         DM_DISPLAYFLAGS);

    if (index == ENUM_CURRENT_SETTINGS
        && (hdc = CreateDC(deviceName, NULL, NULL, NULL)) != NULL) {
        char bmi_data[sizeof(BITMAPINFOHEADER) + 256 * sizeof(RGBQUAD)];
        LPBITMAPINFO bmi;
        HBITMAP hbm;
        int logical_width = GetDeviceCaps( hdc, HORZRES );
        int logical_height = GetDeviceCaps( hdc, VERTRES );

        /* This is confusing.. If we are DPI-unaware:
        - DeviceMode.dmPelsWidth are in pixels (unlike most other sizes, which are usually points).
        - we can switch to a resolution in pixels which will temporarily disable DPI scaling
          (see WIN_SetDisplayMode), as long as it's not equal the desktop resolution.
        - for the desktop resolution, we have to live with DPI virtualization.
          e.g. if the desktop is 2880x1800 at 192dpi, there's no way to switch to
          2880x1800 at 96dpi aside from being DPI aware.
        */
        mode->w = logical_width;
        mode->h = logical_height;
        
        SDL_zero(bmi_data);
        bmi = (LPBITMAPINFO) bmi_data;
        bmi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);

        hbm = CreateCompatibleBitmap(hdc, 1, 1);
        GetDIBits(hdc, hbm, 0, 1, NULL, bmi, DIB_RGB_COLORS);
        GetDIBits(hdc, hbm, 0, 1, NULL, bmi, DIB_RGB_COLORS);
        DeleteObject(hbm);
        DeleteDC(hdc);
        if (bmi->bmiHeader.biCompression == BI_BITFIELDS) {
            switch (*(Uint32 *) bmi->bmiColors) {
            case 0x00FF0000:
                mode->format = SDL_PIXELFORMAT_RGB888;
                break;
            case 0x000000FF:
                mode->format = SDL_PIXELFORMAT_BGR888;
                break;
            case 0xF800:
                mode->format = SDL_PIXELFORMAT_RGB565;
                break;
            case 0x7C00:
                mode->format = SDL_PIXELFORMAT_RGB555;
                break;
            }
        } else if (bmi->bmiHeader.biBitCount == 8) {
            mode->format = SDL_PIXELFORMAT_INDEX8;
        } else if (bmi->bmiHeader.biBitCount == 4) {
            mode->format = SDL_PIXELFORMAT_INDEX4LSB;
        }
    } else if (mode->format == SDL_PIXELFORMAT_UNKNOWN) {
        /* FIXME: Can we tell what this will be? */
        if ((data->DeviceMode.dmFields & DM_BITSPERPEL) == DM_BITSPERPEL) {
            switch (data->DeviceMode.dmBitsPerPel) {
            case 32:
                mode->format = SDL_PIXELFORMAT_RGB888;
                break;
            case 24:
                mode->format = SDL_PIXELFORMAT_RGB24;
                break;
            case 16:
                mode->format = SDL_PIXELFORMAT_RGB565;
                break;
            case 15:
                mode->format = SDL_PIXELFORMAT_RGB555;
                break;
            case 8:
                mode->format = SDL_PIXELFORMAT_INDEX8;
                break;
            case 4:
                mode->format = SDL_PIXELFORMAT_INDEX4LSB;
                break;
            }
        }
    }
}

static SDL_bool
WIN_GetDisplayMode(_THIS, LPCTSTR deviceName, DWORD index, SDL_DisplayMode * mode)
{
    SDL_DisplayModeData *data;
    DEVMODE devmode;

    devmode.dmSize = sizeof(devmode);
    devmode.dmDriverExtra = 0;
    if (!EnumDisplaySettings(deviceName, index, &devmode)) {
        return SDL_FALSE;
    }

    data = (SDL_DisplayModeData *) SDL_malloc(sizeof(*data));
    if (!data) {
        return SDL_FALSE;
    }

    mode->driverdata = data;
    data->DeviceMode = devmode;

    mode->format = SDL_PIXELFORMAT_UNKNOWN;
    mode->w = data->DeviceMode.dmPelsWidth;
    mode->h = data->DeviceMode.dmPelsHeight;
    mode->refresh_rate = data->DeviceMode.dmDisplayFrequency;

    /* Fill in the mode information */
    WIN_UpdateDisplayMode(_this, deviceName, index, mode);
    return SDL_TRUE;
}

static SDL_bool
WIN_AddDisplay(_THIS, HMONITOR hMonitor, const MONITORINFOEX *info)
{
    SDL_VideoDisplay display;
    SDL_DisplayData *displaydata;
    SDL_DisplayMode mode;
    DISPLAY_DEVICE device;

#ifdef DEBUG_MODES
    SDL_Log("Display: %s\n", WIN_StringToUTF8(info->szDevice));
#endif

    if (!WIN_GetDisplayMode(_this, info->szDevice, ENUM_CURRENT_SETTINGS, &mode)) {
        return SDL_FALSE;
    }

    displaydata = (SDL_DisplayData *) SDL_malloc(sizeof(*displaydata));
    if (!displaydata) {
        return SDL_FALSE;
    }
    SDL_memcpy(displaydata->DeviceName, info->szDevice,
               sizeof(displaydata->DeviceName));
    displaydata->MonitorHandle = hMonitor;

    SDL_zero(display);
    device.cb = sizeof(device);
    if (EnumDisplayDevices(info->szDevice, 0, &device, 0)) {
        display.name = WIN_StringToUTF8(device.DeviceString);
    }
    display.desktop_mode = mode;
    display.current_mode = mode;
    display.driverdata = displaydata;
    SDL_AddVideoDisplay(&display);
    SDL_free(display.name);
    return SDL_TRUE;
}

typedef struct _WIN_AddDisplaysData {
    SDL_VideoDevice *video_device;
    SDL_bool want_primary;
} WIN_AddDisplaysData;

static BOOL CALLBACK
WIN_AddDisplaysCallback(HMONITOR hMonitor,
                        HDC      hdcMonitor,
                        LPRECT   lprcMonitor,
                        LPARAM   dwData)
{
    WIN_AddDisplaysData *data = (WIN_AddDisplaysData*)dwData;
    MONITORINFOEX info;

    SDL_zero(info);
    info.cbSize = sizeof(info);

    if (GetMonitorInfo(hMonitor, (LPMONITORINFO)&info) != 0) {
        const SDL_bool is_primary = ((info.dwFlags & MONITORINFOF_PRIMARY) == MONITORINFOF_PRIMARY);

        if (is_primary == data->want_primary) {
            WIN_AddDisplay(data->video_device, hMonitor, &info);
        }
    }

    // continue enumeration
    return TRUE;
}

static void
WIN_AddDisplays(_THIS)
{
    WIN_AddDisplaysData callback_data;
    callback_data.video_device = _this;

    callback_data.want_primary = SDL_TRUE;
    EnumDisplayMonitors(NULL, NULL, WIN_AddDisplaysCallback, (LPARAM)&callback_data);

    callback_data.want_primary = SDL_FALSE;
    EnumDisplayMonitors(NULL, NULL, WIN_AddDisplaysCallback, (LPARAM)&callback_data);
}

int
WIN_InitModes(_THIS)
{
    WIN_AddDisplays(_this);

    if (_this->num_displays == 0) {
        return SDL_SetError("No displays available");
    }
    return 0;
}

int
WIN_GetDisplayDPI(_THIS, SDL_VideoDisplay * display, float * ddpi_out, float * hdpi_out, float * vdpi_out)
{
    const SDL_DisplayData *displaydata = (SDL_DisplayData *)display->driverdata;
    const SDL_VideoData *videodata = (SDL_VideoData *)display->device->driverdata;
    float hdpi = 0, vdpi = 0, ddpi = 0;
    
    if (videodata->GetDpiForMonitor) {
        UINT hdpi_uint, vdpi_uint;
        // Windows 8.1+ codepath
        if (videodata->GetDpiForMonitor(displaydata->MonitorHandle, MDT_EFFECTIVE_DPI, &hdpi_uint, &vdpi_uint) == S_OK) {
            // GetDpiForMonitor docs promise to return the same hdpi/vdpi
            hdpi = (float)hdpi_uint;
            vdpi = (float)hdpi_uint;
            ddpi = (float)hdpi_uint;
        } else {
            return SDL_SetError("GetDpiForMonitor failed");
        }
    } else {
        // Window 8.0 and below: same DPI for all monitors.
        int hpoints, vpoints, hpix, vpix;
        float hinches, vinches;

        /* NOTE: all of this is just to compute the diagonal DPI. */
        hpoints = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        vpoints = GetSystemMetrics(SM_CYVIRTUALSCREEN);

        hpix = MulDiv(hpoints, videodata->system_xdpi, 96);
        vpix = MulDiv(vpoints, videodata->system_ydpi, 96);

        hinches = (float)hpoints / 96.0f;
        vinches = (float)vpoints / 96.0f;

        hdpi = (float)videodata->system_xdpi;
        vdpi = (float)videodata->system_ydpi;
        ddpi = SDL_ComputeDiagonalDPI(hpix, vpix, hinches, vinches);
    }

    if (ddpi_out) {
        *ddpi_out = ddpi;
    }
    if (hdpi_out) {
        *hdpi_out = hdpi;
    }
    if (vdpi_out) {
        *vdpi_out = vdpi;
    }

    return ddpi != 0.0f ? 0 : SDL_SetError("Couldn't get DPI");
}

static int
WIN_GetDisplayBoundsInternal(_THIS, SDL_VideoDisplay * display, SDL_Rect * rect, SDL_bool usable)
{
    const SDL_DisplayData *data = (const SDL_DisplayData *)display->driverdata;
    const SDL_VideoData *vid_data = (const SDL_VideoData *)_this->driverdata;
    MONITORINFO minfo;
    const RECT *rect_win;
    BOOL rc;
    int x, y;
    int w, h;

    SDL_zero(minfo);
    minfo.cbSize = sizeof(MONITORINFO);
    rc = GetMonitorInfo(data->MonitorHandle, &minfo);

    if (!rc) {
        return SDL_SetError("Couldn't find monitor data");
    }

    rect_win = usable ? &minfo.rcWork : &minfo.rcMonitor;

    x = rect_win->left;
    y = rect_win->top;
    w = rect_win->right - rect_win->left;
    h = rect_win->bottom - rect_win->top;
    WIN_ScreenRectToSDL(&x, &y, &w, &h);

    rect->x = x;
    rect->y = y;
    rect->w = w;
    rect->h = h;

    return 0;
}

int
WIN_GetDisplayBounds(_THIS, SDL_VideoDisplay * display, SDL_Rect * rect)
{
    return WIN_GetDisplayBoundsInternal(_this, display, rect, SDL_FALSE);
}

int
WIN_GetDisplayUsableBounds(_THIS, SDL_VideoDisplay * display, SDL_Rect * rect)
{
    return WIN_GetDisplayBoundsInternal(_this, display, rect, SDL_TRUE);
}

static int
WIN_GetMonitorDPIAndRects(const SDL_VideoData *videodata, HMONITOR monitor, UINT *xdpi, UINT *ydpi, RECT *monitorrect_sdl, RECT *monitorrect_win)
{
    HRESULT result;
    MONITORINFO moninfo = { 0 };
    UINT unused;
    int mon_width, mon_height;

    /* Check for Windows < 8.1*/
    if (!videodata->GetDpiForMonitor) {
        *xdpi = videodata->system_xdpi;
        *ydpi = videodata->system_ydpi;
    } else {
        result = videodata->GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, xdpi, &unused);
        *ydpi = *xdpi;
        if (result != S_OK) {
            /* Shouldn't happen? */
            return SDL_SetError("GetDpiForMonitor failed");
        }
    }

    moninfo.cbSize = sizeof(MONITORINFO);
    if (!GetMonitorInfo(monitor, &moninfo)) {
        /* Shouldn't happen? */
        return SDL_SetError("GetMonitorInfo failed");
    }

    *monitorrect_win = moninfo.rcMonitor;
    *monitorrect_sdl = moninfo.rcMonitor;

    /* fix up the right/bottom of monitorrect_sdl */
    mon_width = moninfo.rcMonitor.right - moninfo.rcMonitor.left;
    mon_height = moninfo.rcMonitor.bottom - moninfo.rcMonitor.top;
    mon_width = MulDiv(mon_width, 96, *xdpi);
    mon_height = MulDiv(mon_height, 96, *ydpi);

    monitorrect_sdl->right = monitorrect_sdl->left + mon_width;
    monitorrect_sdl->bottom = monitorrect_sdl->top + mon_height;

    return 0;
}

/* Convert an SDL to a Windows screen rect. */
void WIN_ScreenRectFromSDL(int *x, int *y, int *w, int *h)
{
    const SDL_VideoDevice *videodevice = SDL_GetVideoDevice();
    const SDL_VideoData *videodata;
    RECT inputrect;
    RECT monitorrect_sdl, monitorrect_win;
    UINT xdpi, ydpi;
    HMONITOR monitor;

    if (!videodevice || !videodevice->driverdata)
        return;

    videodata = (SDL_VideoData *)videodevice->driverdata;
    if (!videodata->highdpi_enabled)
        return;

    /*
    The trick here is passing SDL coordinates to MonitorFromRect, which expects Windows 
    coordinates (pixels). This is wrong, but there is no real alternative, and due to
    the way we derive the SDL coordinate system, it works OK:

    - top-left corner of monitors in SDL coordinates are identical to the top-left corner in Windows coordinates.
    - the widths/heights of monitors (and windows) in SDL coords are in scaled points,
      which are equal or less than the corresponding sizes in pixels (because we only support scale factors >=100%)
    - becuase of the above two points, a rect (in SDL coordinates) that is fully inside 
      a monitor's bounds (in SDL coordinates) will also be fully inside that monitor's bounds in Windows coordinates.
    */
    inputrect.left = *x;
    inputrect.top = *y;
    inputrect.right = *x + *w;
    inputrect.bottom = *y + *h;
    monitor = MonitorFromRect(&inputrect, MONITOR_DEFAULTTONEAREST);

    if (WIN_GetMonitorDPIAndRects(videodata, monitor, &xdpi, &ydpi, &monitorrect_sdl, &monitorrect_win) == 0) {
        *w = MulDiv(*w, xdpi, 96);
        *h = MulDiv(*h, ydpi, 96);

        *x = monitorrect_sdl.left + MulDiv(*x - monitorrect_sdl.left, xdpi, 96);
        *y = monitorrect_sdl.top + MulDiv(*y - monitorrect_sdl.top, ydpi, 96);

        /* ensure the result is not past the right/bottom of the monitor rect */
        if (*x >= monitorrect_win.right)
            *x = monitorrect_win.right - 1;
        if (*y >= monitorrect_win.bottom)
            *y = monitorrect_win.bottom - 1;
    }
}

/* Converts a Windows screen rect to an SDL one. */
void WIN_ScreenRectToSDL(int *x, int *y, int *w, int *h)
{
    const SDL_VideoDevice *videodevice = SDL_GetVideoDevice();
    const SDL_VideoData *videodata;
    RECT inputrect;
    RECT monitorrect_sdl, monitorrect_win;
    UINT xdpi, ydpi;
    HMONITOR monitor;

    if (!videodevice || !videodevice->driverdata)
        return;

    videodata = (SDL_VideoData *)videodevice->driverdata;
    if (!videodata->highdpi_enabled)
        return;
    
    inputrect.left = *x;
    inputrect.top = *y;
    inputrect.right = *x + *w;
    inputrect.bottom = *y + *h;
    monitor = MonitorFromRect(&inputrect, MONITOR_DEFAULTTONEAREST);

    if (WIN_GetMonitorDPIAndRects(videodata, monitor, &xdpi, &ydpi, &monitorrect_sdl, &monitorrect_win) == 0) {
        *w = MulDiv(*w, 96, xdpi);
        *h = MulDiv(*h, 96, ydpi);

        *x = monitorrect_win.left + MulDiv(*x - monitorrect_win.left, 96, xdpi);
        *y = monitorrect_win.top + MulDiv(*y - monitorrect_win.top, 96, ydpi);
    }
}

void
WIN_GetDisplayModes(_THIS, SDL_VideoDisplay * display)
{
    SDL_DisplayData *data = (SDL_DisplayData *) display->driverdata;
    DWORD i;
    SDL_DisplayMode mode;

    for (i = 0;; ++i) {
        if (!WIN_GetDisplayMode(_this, data->DeviceName, i, &mode)) {
            break;
        }
        if (SDL_ISPIXELFORMAT_INDEXED(mode.format)) {
            /* We don't support palettized modes now */
            SDL_free(mode.driverdata);
            continue;
        }
        if (mode.format != SDL_PIXELFORMAT_UNKNOWN) {
            if (!SDL_AddDisplayMode(display, &mode)) {
                SDL_free(mode.driverdata);
            }
        } else {
            SDL_free(mode.driverdata);
        }
    }
}

#ifdef DEBUG_MODES
static void
WIN_LogMonitor(_THIS, HMONITOR mon)
{
    const SDL_VideoData *vid_data = (const SDL_VideoData *)_this->driverdata;
    MONITORINFOEX minfo;
    UINT xdpi = 0, ydpi = 0;
    char *name_utf8;

    if (vid_data->GetDpiForMonitor)
        vid_data->GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &xdpi, &ydpi);

    SDL_zero(minfo);
    minfo.cbSize = sizeof(minfo);
    GetMonitorInfo(mon, (LPMONITORINFO)&minfo);

    name_utf8 = WIN_StringToUTF8(minfo.szDevice);

    SDL_Log("WIN_LogMonitor: monitor \"%s\": dpi: %d. Windows virtual screen coordinates: (%d, %d), %dx%d",
        name_utf8,
        xdpi,
        minfo.rcMonitor.left,
        minfo.rcMonitor.top,
        minfo.rcMonitor.right - minfo.rcMonitor.left,
        minfo.rcMonitor.bottom - minfo.rcMonitor.top);

    SDL_free(name_utf8);
}
#endif

int
WIN_SetDisplayMode(_THIS, SDL_VideoDisplay * display, SDL_DisplayMode * mode)
{
    SDL_DisplayData *displaydata = (SDL_DisplayData *) display->driverdata;
    SDL_DisplayModeData *data = (SDL_DisplayModeData *) mode->driverdata;
    LONG status;

#ifdef DEBUG_MODES
    SDL_Log("WIN_SetDisplayMode: monitor before mode change:");
    WIN_LogMonitor(_this, displaydata->MonitorHandle);
#endif

    /*
    High-DPI notes:

    - ChangeDisplaySettingsEx always takes pixels.
    - e.g. if the display is set to 2880x1800 with 200% scaling in the Control Panel,
      - calling ChangeDisplaySettingsEx with a dmPelsWidth/Height other than 2880x1800 will
        change the monitor DPI to 96. (100% scaling)
      - calling ChangeDisplaySettingsEx with a dmPelsWidth/Height of 2880x1800 (or a NULL DEVMODE*) will
        reset the monitor DPI to 192. (200% scaling)
      NOTE: these are temporary changes in DPI, not modifications to the Control Panel setting.

    - Windows bug: windows do not get a WM_DPICHANGED message after a ChangeDisplaySettingsEx, even though the
      monitor DPI changes
      (as of Windows 10 Creator's Update, at least)
    */
    if (mode->driverdata == display->desktop_mode.driverdata) {
#ifdef DEBUG_MODES
        SDL_Log("WIN_SetDisplayMode: resetting to original resolution");
#endif
        status = ChangeDisplaySettingsEx(displaydata->DeviceName, NULL, NULL, CDS_FULLSCREEN, NULL);
    } else {
#ifdef DEBUG_MODES
        SDL_Log("WIN_SetDisplayMode: changing to %dx%d pixels", data->DeviceMode.dmPelsWidth, data->DeviceMode.dmPelsHeight);
#endif
        status = ChangeDisplaySettingsEx(displaydata->DeviceName, &data->DeviceMode, NULL, CDS_FULLSCREEN, NULL);
    }
    if (status != DISP_CHANGE_SUCCESSFUL) {
        const char *reason = "Unknown reason";
        switch (status) {
        case DISP_CHANGE_BADFLAGS:
            reason = "DISP_CHANGE_BADFLAGS";
            break;
        case DISP_CHANGE_BADMODE:
            reason = "DISP_CHANGE_BADMODE";
            break;
        case DISP_CHANGE_BADPARAM:
            reason = "DISP_CHANGE_BADPARAM";
            break;
        case DISP_CHANGE_FAILED:
            reason = "DISP_CHANGE_FAILED";
            break;
        }
        return SDL_SetError("ChangeDisplaySettingsEx() failed: %s", reason);
    }

#ifdef DEBUG_MODES
    SDL_Log("WIN_SetDisplayMode: monitor after mode change:");
    WIN_LogMonitor(_this, displaydata->MonitorHandle);
#endif

    EnumDisplaySettings(displaydata->DeviceName, ENUM_CURRENT_SETTINGS, &data->DeviceMode);
    WIN_UpdateDisplayMode(_this, displaydata->DeviceName, ENUM_CURRENT_SETTINGS, mode);
    return 0;
}

void
WIN_QuitModes(_THIS)
{
    /* All fullscreen windows should have restored modes by now */
}

#endif /* SDL_VIDEO_DRIVER_WINDOWS */

/* vi: set ts=4 sw=4 expandtab: */
