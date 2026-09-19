#include "pch.h"

#include "Engine/Window/ISplashWindow.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

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

    void ISplashWindow::scaleBgraImage( const uint8* pSource, uint32 sourceWidth, uint32 sourceHeight,
                                        uint8* pOutPixel, uint32 destWidth, uint32 destHeight )
    {
        if ( pSource == nullptr || pOutPixel == nullptr )
            return;
        if ( sourceWidth == 0 || sourceHeight == 0 || destWidth == 0 || destHeight == 0 )
            return;

        const size_t sourceStride = static_cast<size_t>( sourceWidth ) * 4u;

        for ( uint32 destY = 0; destY < destHeight; ++destY )
        {
            // 목적지 한 픽셀이 덮는 원본 구간 [y0, y1). 정수로만 나눠 배율을 따로 들지 않는다.
            const uint32 sourceY0 = ( destY * sourceHeight ) / destHeight;
            const uint32 sourceY1 = MathUtil::max( sourceY0 + 1u, ( ( destY + 1u ) * sourceHeight ) / destHeight );

            for ( uint32 destX = 0; destX < destWidth; ++destX )
            {
                const uint32 sourceX0 = ( destX * sourceWidth ) / destWidth;
                const uint32 sourceX1 = MathUtil::max( sourceX0 + 1u, ( ( destX + 1u ) * sourceWidth ) / destWidth );

                uint32 sumB        = 0;
                uint32 sumG        = 0;
                uint32 sumR        = 0;
                uint32 sumA        = 0;
                uint32 sampleCount = 0;

                for ( uint32 sourceY = sourceY0; sourceY < sourceY1 && sourceY < sourceHeight; ++sourceY )
                {
                    const uint8* pRow = pSource + ( static_cast<size_t>( sourceY ) * sourceStride );
                    for ( uint32 sourceX = sourceX0; sourceX < sourceX1 && sourceX < sourceWidth; ++sourceX )
                    {
                        const uint8* pPixel = pRow + ( static_cast<size_t>( sourceX ) * 4u );
                        sumB += pPixel[0];
                        sumG += pPixel[1];
                        sumR += pPixel[2];
                        sumA += pPixel[3];
                        ++sampleCount;
                    }
                }

                if ( sampleCount == 0 )
                    continue;

                uint8* pDest = pOutPixel + ( ( static_cast<size_t>( destY ) * destWidth + destX ) * 4u );
                pDest[0]     = static_cast<uint8>( sumB / sampleCount );
                pDest[1]     = static_cast<uint8>( sumG / sampleCount );
                pDest[2]     = static_cast<uint8>( sumR / sampleCount );
                pDest[3]     = static_cast<uint8>( sumA / sampleCount );
            }
        }
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

        normalizeSplashToBgra();
        return true;
    }

    void ISplashWindow::normalizeSplashToBgra()
    {
        // **채널 순서를 여기서 한 번에 맞춘다.** 예전에는 이 뒤집기가 `Win32SplashWindow` 안에만
        // 있었다 — 그래서 아트를 `R8G8B8A8` 로 다시 내보내는 날 **윈도우만 맞고 리눅스는 빨강과
        // 파랑이 뒤바뀐다.** 지금 들어 있는 `splash.dds` 가 마침 `B8G8R8A8` 이라 뒤집기가 건너뛰어져
        // 그 차이가 드러나지 않았을 뿐이다.
        //
        // 두 그리는 경로가 모두 BGRA 를 원한다: Win32 의 `BI_RGB` 32bpp DIB 는 메모리에서 B,G,R,X
        // 순서이고, X11 의 리틀엔디언 TrueColor 비주얼(0x00RRGGBB)도 같은 순서다.
        if ( _splashData._bIsBgra != SW_FALSE || _splashData.getPixels() == nullptr )
            return;

        // 픽셀 수와 바이트 오프셋을 size_t 로 센다. int 로 곱하면 큰 이미지에서 넘치고,
        // 그 값이 포인터 오프셋으로 쓰이므로 버퍼 밖을 가리킨다.
        const size_t totalPixels = static_cast<size_t>( _splashData._width ) * static_cast<size_t>( _splashData._height );
        for ( size_t pixelIndex = 0; pixelIndex < totalPixels; ++pixelIndex )
        {
            uint8* pPixel = _splashData.getPixels() + ( pixelIndex * 4 );
            std::swap( pPixel[0], pPixel[2] );
        }
        _splashData._bIsBgra = SW_TRUE;
    }
} // namespace sw
