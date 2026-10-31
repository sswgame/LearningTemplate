#include "pch.h"

#include "Engine/Window/SplashWindow.h"

#include "Core/Foundation/BuildInfo.h"

#include "Engine/Window/ISplashWindow.h"

namespace sw
{
    SW_LOG_CALLER( "SplashWindow" );

    SplashWindow::SplashWindow()
        : _pImpl{ nullptr }
    {
    }

    SplashWindow::~SplashWindow()
    {
        dismiss();
    }

    bool SplashWindow::initialize( const utf8* pTitle, const utf8* pInitialStatus, uint32 width, uint32 height )
    {
        // 스플래시는 에디터 초기화 표시용이고 이미지도 editor 도메인 에셋(Content/editor/textures/splash.dds)이다. 배포는 editor.pack 을 만들지 않으므로
        // 띄우지 않는다. _pImpl 이 널로 남아 이후 updateStatus · setProgress · dismiss 는 아무것도 하지 않는다.
        if constexpr ( build::kShipping )
            return false;

        // 스플래시가 없는 세션은 폴백 없이 스플래시 없이 기동하는 것이 정상 경로이지만 조용히 지나가지 않고 한 번 알린다.
        if ( ISplashWindow::isPlatformSupported() == false )
        {
            SW_LOG_INFO( "Splash window: this session cannot show one (Linux needs the display server of the chosen window backend - DISPLAY for x11, WAYLAND_DISPLAY for wayland) - starting without it" );
            return false;
        }
        _pImpl = ISplashWindow::createPlatformSplash();
        if ( _pImpl == nullptr )
            return false;

        const bool bOpened = _pImpl->initialize( pTitle, pInitialStatus, width, height );
        if ( bOpened == false )
            SW_LOG_INFO( "Splash window: could not be opened (no display connection) - starting without it" );
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
