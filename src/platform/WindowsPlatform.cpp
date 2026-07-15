// SPDX-License-Identifier: MPL-2.0

#include "platform/WindowsPlatform.h"

#include <QBuffer>
#include <QClipboard>
#include <QGuiApplication>
#include <QImage>
#include <QMimeData>
#include <QWidget>

#include <cstring>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <qt_windows.h>
// Use custom names to avoid conflict with Windows macros SPI_GETANIMATION/SPI_SETANIMATION
#define RUWA_SPI_GETANIMATION 0x0048
#define RUWA_SPI_SETANIMATION 0x0049
#endif

#if defined(Q_OS_WIN)
namespace {

// ---------------------------------------------------------------------------
// Clipboard delayed rendering
//
// Copying the canvas used to PNG-encode the whole image synchronously on the
// UI thread before returning - the dominant cost (seconds on large canvases).
// Instead we publish the cheap CF_DIBV5 bitmap immediately and *promise* the
// "PNG" format with a NULL handle (delayed rendering). PNG is only encoded if a
// paste target actually asks for it (WM_RENDERFORMAT), which happens in the
// consumer app at paste time - never during our copy click, and never at all
// for consumers that take the DIB (Photoshop, Krita, Office, Paint).
//
// A dedicated message-only window owns the clipboard so it receives the render
// requests; its messages are dispatched by Qt's normal event loop.
// ---------------------------------------------------------------------------

HWND g_clipboardOwner = nullptr;
UINT g_pngFormat = 0;
QImage g_pendingImage; // straight-alpha source kept alive for lazy PNG encode

HGLOBAL buildPngGlobal(const QImage& image)
{
    QByteArray pngBytes;
    QBuffer pngBuffer(&pngBytes);
    if (!pngBuffer.open(QIODevice::WriteOnly) || !image.save(&pngBuffer, "PNG")
        || pngBytes.isEmpty()) {
        return nullptr;
    }
    HGLOBAL handle = ::GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>(pngBytes.size()));
    if (!handle) {
        return nullptr;
    }
    void* ptr = ::GlobalLock(handle);
    if (!ptr) {
        ::GlobalFree(handle);
        return nullptr;
    }
    std::memcpy(ptr, pngBytes.constData(), static_cast<size_t>(pngBytes.size()));
    ::GlobalUnlock(handle);
    return handle;
}

LRESULT CALLBACK clipboardOwnerProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_RENDERFORMAT:
        // A consumer is pasting and asked for our promised format. The clipboard
        // is already open by the requester here - just supply the data.
        if (static_cast<UINT>(wParam) == g_pngFormat && !g_pendingImage.isNull()) {
            if (HGLOBAL h = buildPngGlobal(g_pendingImage)) {
                ::SetClipboardData(g_pngFormat, h);
            }
        }
        return 0;
    case WM_RENDERALLFORMATS:
        // We are about to lose ownership (e.g. app exit) - materialize anything
        // still promised so it survives on the clipboard.
        if (::OpenClipboard(hwnd)) {
            if (::GetClipboardOwner() == hwnd && g_pngFormat != 0 && !g_pendingImage.isNull()) {
                if (HGLOBAL h = buildPngGlobal(g_pendingImage)) {
                    ::SetClipboardData(g_pngFormat, h);
                }
            }
            ::CloseClipboard();
        }
        return 0;
    case WM_DESTROYCLIPBOARD:
        // Another app took the clipboard (or we are replacing our own content):
        // drop the source so it can be freed.
        g_pendingImage = QImage();
        return 0;
    default:
        break;
    }
    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

HWND ensureClipboardOwner()
{
    if (g_clipboardOwner) {
        return g_clipboardOwner;
    }
    static const wchar_t* kClassName = L"RuwaClipboardOwner";
    HINSTANCE instance = ::GetModuleHandleW(nullptr);

    WNDCLASSEXW wc {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = clipboardOwnerProc;
    wc.hInstance = instance;
    wc.lpszClassName = kClassName;
    ::RegisterClassExW(&wc); // harmless if already registered

    g_clipboardOwner = ::CreateWindowExW(
        0, kClassName, kClassName, 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
    return g_clipboardOwner;
}

HGLOBAL buildDibV5Global(const QImage& image)
{
    const SIZE_T pixelBytes = static_cast<SIZE_T>(image.sizeInBytes());
    const SIZE_T dibBytes = sizeof(BITMAPV5HEADER) + pixelBytes;
    HGLOBAL dibHandle = ::GlobalAlloc(GMEM_MOVEABLE, dibBytes);
    if (!dibHandle) {
        return nullptr;
    }
    void* dibPtr = ::GlobalLock(dibHandle);
    if (!dibPtr) {
        ::GlobalFree(dibHandle);
        return nullptr;
    }
    auto* header = static_cast<BITMAPV5HEADER*>(dibPtr);
    std::memset(header, 0, sizeof(BITMAPV5HEADER));
    header->bV5Size = sizeof(BITMAPV5HEADER);
    header->bV5Width = image.width();
    header->bV5Height = -image.height();
    header->bV5Planes = 1;
    header->bV5BitCount = 32;
    header->bV5Compression = BI_BITFIELDS;
    header->bV5SizeImage = static_cast<DWORD>(pixelBytes);
    header->bV5RedMask = 0x00FF0000;
    header->bV5GreenMask = 0x0000FF00;
    header->bV5BlueMask = 0x000000FF;
    header->bV5AlphaMask = 0xFF000000;
    header->bV5CSType = LCS_sRGB;
    std::memcpy(static_cast<char*>(dibPtr) + sizeof(BITMAPV5HEADER), image.constBits(), pixelBytes);
    ::GlobalUnlock(dibHandle);
    return dibHandle;
}

} // namespace
#endif // Q_OS_WIN

namespace ruwa::platform {

void WindowsPlatform::disableWindowAnimations(QWidget* window)
{
#if defined(Q_OS_WIN)
    if (!window || !window->winId())
        return;

    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (!hwnd)
        return;

    ANIMATIONINFO ai = { sizeof(ANIMATIONINFO), 0 };
    ai.cbSize = sizeof(ai);
    if (::SystemParametersInfoW(RUWA_SPI_GETANIMATION, sizeof(ai), &ai, 0)) {
        if (ai.iMinAnimate != 0) {
            ai.iMinAnimate = 0;
            ::SystemParametersInfoW(RUWA_SPI_SETANIMATION, sizeof(ai), &ai, SPIF_SENDCHANGE);
        }
    }
#else
    Q_UNUSED(window);
#endif
}

void WindowsPlatform::enableWindowAnimations(QWidget* window)
{
#if defined(Q_OS_WIN)
    if (!window)
        return;

    ANIMATIONINFO ai = { sizeof(ANIMATIONINFO), 0 };
    ai.cbSize = sizeof(ai);
    ai.iMinAnimate = 1;
    ::SystemParametersInfoW(RUWA_SPI_SETANIMATION, sizeof(ai), &ai, SPIF_SENDCHANGE);
#else
    Q_UNUSED(window);
#endif
}

bool WindowsPlatform::copyImageToClipboard(const QImage& sourceImage)
{
#if defined(Q_OS_WIN)
    if (sourceImage.isNull()) {
        return false;
    }

    // Build the cheap CF_DIBV5 bitmap eagerly (a couple of memcpys). The
    // expensive PNG encode is deferred via delayed rendering - see the
    // anonymous-namespace helpers above.
    const QImage image = sourceImage.convertToFormat(QImage::Format_ARGB32);
    HGLOBAL dibHandle = buildDibV5Global(image);
    if (!dibHandle) {
        return false;
    }

    HWND owner = ensureClipboardOwner();
    if (!owner || !::OpenClipboard(owner)) {
        ::GlobalFree(dibHandle);
        return false;
    }

    bool success = false;
    if (::EmptyClipboard()) {
        // EmptyClipboard() synchronously delivered WM_DESTROYCLIPBOARD to us,
        // clearing any previous g_pendingImage. Stash the source for lazy PNG
        // *after* that, so it survives.
        g_pendingImage = sourceImage;
        g_pngFormat = ::RegisterClipboardFormatW(L"PNG");

        success = (::SetClipboardData(CF_DIBV5, dibHandle) != nullptr);
        if (success) {
            dibHandle = nullptr;
            // Promise PNG with a NULL handle: encoded on demand in WM_RENDERFORMAT.
            if (g_pngFormat != 0) {
                ::SetClipboardData(g_pngFormat, nullptr);
            }
        } else {
            g_pendingImage = QImage();
        }
    }

    ::CloseClipboard();

    if (dibHandle) {
        ::GlobalFree(dibHandle);
    }
    return success;
#else
    auto* mimeData = new QMimeData();
    QByteArray pngBytes;
    QBuffer pngBuffer(&pngBytes);
    if (pngBuffer.open(QIODevice::WriteOnly)) {
        sourceImage.save(&pngBuffer, "PNG");
    }
    if (!pngBytes.isEmpty()) {
        mimeData->setData(QStringLiteral("image/png"), pngBytes);
    } else {
        mimeData->setImageData(sourceImage);
    }
    QGuiApplication::clipboard()->setMimeData(mimeData);
    return true;
#endif
}

} // namespace ruwa::platform
