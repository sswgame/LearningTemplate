#include "pch.h"

#include "Engine/Window/Mac/CocoaSplashWindow.h"

#include "Core/String/StringUtil.h"

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

    bool CocoaSplashWindow::initialize( const utf8* /*pTitle*/, const utf8* pInitialStatus, uint32 width, uint32 height )
    {
        _status = StringUtil::isNullOrEmpty( pInitialStatus ) ? "Initializing..." : pInitialStatus;
        _width  = width;
        _height = height;
        loadSplashImage();
        _bOpen = SW_TRUE;
        return true;
    }

    void CocoaSplashWindow::updateStatus( const utf8* pStatus, float32 progress )
    {
        if ( _bOpen == SW_FALSE )
            return;
        if ( StringUtil::isNullOrEmpty( pStatus ) == false )
            _status = pStatus;
        if ( progress >= 0.0f )
            _progress = ( progress > 1.0f ) ? 1.0f : progress;
    }

    void CocoaSplashWindow::setProgress( float32 progress )
    {
        updateStatus( nullptr, progress );
    }

    void CocoaSplashWindow::dismiss()
    {
        _bOpen = SW_FALSE;
    }
} // namespace sw
