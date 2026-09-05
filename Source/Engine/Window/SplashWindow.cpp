#include "pch.h"

#include "Engine/Window/SplashWindow.h"

#include "Engine/Window/ISplashWindow.h"

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

    bool SplashWindow::initialize( const utf8* pTitle, const utf8* pInitialStatus, uint32 width, uint32 height )
    {
#if defined( SW_SHIPPING )
        // 스플래시는 에디터 초기화 표시용 기능이고 이미지도 editor 도메인 자산이다
        // (Resource/editor/textures/splash.dds). Shipping 은 에디터를 빼고 editor.pack 도
        // 만들지 않으므로 아예 띄우지 않는다 — 예전엔 그대로 띄우려다 매 기동마다
        // "Failed to read DDS resource: textures/splash.dds" 만 남겼다.
        // _pImpl 이 널로 남으므로 이후 updateStatus/setProgress/dismiss 는 모두 무해한 no-op 이다.
        (void)pTitle;
        (void)pInitialStatus;
        (void)width;
        (void)height;
        return false;
#else
        _pImpl = ISplashWindow::createPlatformSplash();
        if ( _pImpl == nullptr )
            return false;

        return _pImpl->initialize( pTitle, pInitialStatus, width, height );
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
