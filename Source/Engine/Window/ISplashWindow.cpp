#include "pch.h"

#include "Engine/Window/ISplashWindow.h"

#include "Core/Log/Logger.h"

#include "Engine/Resource/DdsLoader.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Window/Linux/X11SplashWindow.h"
#include "Engine/Window/Mac/CocoaSplashWindow.h"
#include "Engine/Window/Windows/Win32SplashWindow.h"

namespace sw
{
    ISplashWindow::ISplashWindow()
        : _status{}
        , _splashData{}
        , _progress{ 0.0f }
        , _width{ 480 }
        , _height{ 280 }
        , _bOpen{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    ISplashWindow::~ISplashWindow() = default;

    unique_ptr<ISplashWindow> ISplashWindow::createPlatformSplash()
    {
#if defined( SW_PLATFORM_WINDOWS )
        return make_unique<Win32SplashWindow>();
#elif defined( SW_PLATFORM_MACOS )
        return make_unique<CocoaSplashWindow>();
#elif defined( SW_PLATFORM_LINUX )
        return make_unique<X11SplashWindow>();
#else
        return nullptr;
#endif
    }

    bool ISplashWindow::loadSplashImage()
    {
        if ( DdsLoader::loadFromResource( "textures/splash.dds", _splashData ) == false || _splashData.isValid() == false )
            return false;

        // 스플래시를 그리는 두 경로는 **압축 없는 32bpp** 를 전제한다 — Win32 는 StretchDIBits 에
        // biBitCount=32 로 넘기고 그 전에 폭×높이 개의 픽셀을 4바이트씩 제자리에서 뒤집으며,
        // X11 은 XCreateImage 에 depth 32 로 넘긴다. 압축 텍스처가 들어오면(BC1 은 같은 크기의
        // 1/8 이다) 그 뒤집기 루프가 버퍼 밖을 **쓴다**. 지금 들어 있는 splash.dds 는
        // B8G8R8A8 이라 맞지만, 아트를 갈아 끼우며 압축으로 저장하는 것은 흔한 일이다.
        const size_t requiredBytes = static_cast<size_t>( _splashData._width ) * static_cast<size_t>( _splashData._height ) * 4u;
        if ( _splashData._bCompressed != SW_FALSE || _splashData._bytes.size() < requiredBytes )
        {
            SW_LOG_ERROR(
                "Splash image must be uncompressed 32bpp — got %#x%#, %# bytes (need %#), compressed=%#. Skipping splash.",
                _splashData._width, _splashData._height, _splashData._bytes.size(), requiredBytes,
                static_cast<uint32>( _splashData._bCompressed ) );
            _splashData = DdsImageData{};
            return false;
        }

        return true;
    }
} // namespace sw
