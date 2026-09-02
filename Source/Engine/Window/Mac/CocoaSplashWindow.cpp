#include "pch.h"

#include "Engine/Window/Mac/CocoaSplashWindow.h"

namespace sw
{
    CocoaSplashWindow::CocoaSplashWindow()
        : ISplashWindow{}
        , _pCocoaWindow{ nullptr }
    {
    }

    CocoaSplashWindow::~CocoaSplashWindow()
    {
        dismiss();
    }

    bool CocoaSplashWindow::initialize( const utf8* pTitle, const utf8* pInitialStatus, uint32 width, uint32 height )
    {
        _title  = ( pTitle != nullptr && pTitle[0] != '\0' ) ? string{ pTitle } : "SW Engine";
        _status = ( pInitialStatus != nullptr && pInitialStatus[0] != '\0' ) ? string{ pInitialStatus } : "Initializing...";
        _width  = width;
        _height = height;
        _bOpen  = true;
        return true;
    }

    void CocoaSplashWindow::updateStatus( const utf8* pStatus )
    {
        if ( _bOpen == false )
            return;
        _status = ( pStatus != nullptr ) ? string{ pStatus } : "";
    }

    void CocoaSplashWindow::dismiss()
    {
        _bOpen = false;
    }
} // namespace sw
