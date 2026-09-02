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
        _pImpl = ISplashWindow::createPlatformSplash();
        if ( _pImpl == nullptr )
            return false;

        return _pImpl->initialize( pTitle, pInitialStatus, width, height );
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
