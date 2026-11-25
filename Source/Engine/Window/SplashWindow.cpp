#include "pch.h"

#include "Engine/Window/SplashWindow.h"

#include "Core/Log/Logger.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Window/SplashWindowBase.h"

namespace sw
{
    SplashWindow::SplashWindow()
        : _pImpl{ nullptr }
    {
    }

    SplashWindow::~SplashWindow()
    {
        dismiss();
    }

    bool SplashWindow::initialize( const AssetID& imageID, const utf8* pTitle, const utf8* pInitialStatus, uint32 width, uint32 height )
    {
        // 스플래시가 없는 세션은 폴백 없이 스플래시 없이 기동하는 것이 정상 경로이지만 조용히 지나가지 않고 한 번 알린다.
        if ( SplashWindowBase::isPlatformSupported() == false )
        {
            SW_LOG_INFO_ID( "SplashWindow.SplashWindowSessionCannot", "Splash window: this session cannot show one (Linux needs the display server of the chosen window backend - DISPLAY for x11, WAYLAND_DISPLAY for wayland) - starting without it" );
            return false;
        }
        _pImpl = SplashWindowBase::createPlatformSplash();
        if ( _pImpl == nullptr )
            return false;

        if ( _pImpl->loadSplashImage( imageID ) == false )
            SW_LOG_WARNING_ID( "SplashWindow.SplashWindowNoImage", "Splash window: the image '%#' could not be used - the status text and progress bar are drawn on the plain background", imageID.view() ); // 로드가 이유를 오류로 알렸다
        const bool bOpened = _pImpl->initialize( pTitle, pInitialStatus, width, height );
        if ( bOpened == false )
            SW_LOG_INFO_ID( "SplashWindow.SplashWindowCouldNot", "Splash window: could not be opened (no display connection) - starting without it" );
        return bOpened;
    }

    void SplashWindow::updateStatus( const utf8* pStatus, float32 progress )
    {
        if ( _pImpl != nullptr )
            _pImpl->updateStatus( pStatus, progress );
    }

    void SplashWindow::setProgress( float32 progress )
    {
        if ( _pImpl != nullptr )
            _pImpl->setProgress( progress );
    }

    void SplashWindow::dismiss()
    {
        if ( _pImpl != nullptr )
        {
            _pImpl->dismiss();
            _pImpl.reset();
        }
    }

    bool SplashWindow::isOpen() const
    {
        return ( _pImpl != nullptr ) && _pImpl->isOpen();
    }

    float32 SplashWindow::getProgress() const
    {
        return ( _pImpl != nullptr ) ? _pImpl->getProgress() : 0.0f;
    }
} // namespace sw
