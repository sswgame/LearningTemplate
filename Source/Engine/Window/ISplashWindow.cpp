#include "pch.h"

#include "Engine/Window/ISplashWindow.h"

#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Window/Linux/X11SplashWindow.h"
#include "Engine/Window/Mac/CocoaSplashWindow.h"
#include "Engine/Window/Windows/Win32SplashWindow.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace sw
{
    ISplashWindow::ISplashWindow()
        : _title{}
        , _status{}
        , _splashImagePath{}
        , _splashImage{}
        , _progress{ 0.0f }
        , _width{ 480 }
        , _height{ 280 }
        , _bOpen{ false }
    {
    }

    ISplashWindow::~ISplashWindow()
    {
        freeSplashImage();
    }

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

    string ISplashWindow::findSplashImagePath()
    {
        const string_view arrPath[] = {
            "textures/splash.jpg",
            "textures/splash.png",
        };

        for ( const auto& rel : arrPath )
        {
            const string absPath = ResourceUtil::getResourcePath( rel );
            if ( absPath.empty() == false )
                return absPath;
        }

        return {};
    }

    bool ISplashWindow::loadSplashImage( string_view absolutePath )
    {
        freeSplashImage();

        if ( absolutePath.empty() )
            return false;

        const string nullTerminatedPath{ absolutePath };
        int32        width    = 0;
        int32        height   = 0;
        int32        channels = 0;
        uint8*       pPixels  = stbi_load( nullTerminatedPath.c_str(), &width, &height, &channels, 4 );
        if ( pPixels == nullptr )
            return false;

        _splashImage._pPixels  = pPixels;
        _splashImage._width    = width;
        _splashImage._height   = height;
        _splashImage._channels = 4;
        return true;
    }

    void ISplashWindow::freeSplashImage()
    {
        if ( _splashImage._pPixels != nullptr )
        {
            stbi_image_free( _splashImage._pPixels );
            _splashImage._pPixels  = nullptr;
            _splashImage._width    = 0;
            _splashImage._height   = 0;
            _splashImage._channels = 0;
        }
    }
} // namespace sw
