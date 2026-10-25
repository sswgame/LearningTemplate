#include "pch.h"

#include "Engine/Window/SplashWindow.h"

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
#if defined( SW_SHIPPING )
        // 스플래시는 에디터 초기화 표시용 기능이고 이미지도 editor 도메인 에셋이다
        // (Resource/editor/textures/splash.dds). Shipping 은 에디터를 빼고 editor.pack 도
        // 만들지 않으므로 아예 띄우지 않는다(띄우려 하면 매 기동마다 DDS 읽기 실패만 남는다).
        // _pImpl 이 널로 남으므로 이후 updateStatus/setProgress/dismiss 는 모두 무해한 no-op 이다.
        (void)pTitle;
        (void)pInitialStatus;
        (void)width;
        (void)height;
        return false;
#else
        // 스플래시가 없는 세션은 폴백 없이 스플래시 없이 기동하는 것이 정상 경로이지만 조용히 지나가지 않고 한 번 알린다.
        if ( ISplashWindow::isPlatformSupported() == false )
        {
            SW_LOG_INFO( "Splash window: this session cannot show one (Linux needs an X server - DISPLAY; a Wayland-only session has no splash window yet) - starting without it" );
            return false;
        }
        _pImpl = ISplashWindow::createPlatformSplash();
        if ( _pImpl == nullptr )
            return false;

        const bool bOpened = _pImpl->initialize( pTitle, pInitialStatus, width, height );
        if ( bOpened == false )
            SW_LOG_INFO( "Splash window: could not be opened (no display connection) - starting without it" );
        return bOpened;
#endif
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
