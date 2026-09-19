/**
 * @file ISplashWindow.h
 * @brief 플랫폼 독립적인 스플래시 화면 인터페이스
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"

#include "Engine/Resource/DdsLoader.h"

namespace sw
{
    /**
     * @class ISplashWindow
     * @brief 플랫폼별(Windows, Linux, macOS) 스플래시 창 구현을 위한 추상 기본 클래스
     */
    class SW_API ISplashWindow
    {
    public:
        ISplashWindow();
        virtual ~ISplashWindow();

        ISplashWindow( const ISplashWindow& )            = delete;
        ISplashWindow& operator=( const ISplashWindow& ) = delete;

        /**
         * @brief 스플래시 창을 생성하여 화면 중앙에 즉시 표시합니다.
         */
        virtual bool initialize( const utf8* pTitle, const utf8* pInitialStatus, uint32 width, uint32 height ) = 0;

        /** @brief 현재 진행 상황 텍스트 및 진행률(0.0f ~ 1.0f)을 업데이트하고 화면을 즉시 갱신합니다. */
        virtual void updateStatus( const utf8* pStatus, float32 progress = -1.0f ) = 0;

        /** @brief 현재 진행률(0.0f ~ 1.0f)을 설정하고 화면을 즉시 갱신합니다. */
        virtual void setProgress( float32 progress ) = 0;

        /** @brief 스플래시 창을 닫고 리소스를 해제합니다. */
        virtual void dismiss() = 0;

        /** @brief 스플래시 창이 현재 열려 있는지 확인합니다. */
        virtual bool isOpen() const { return _bOpen == SW_TRUE; }

        /** @brief 현재 상태 문자열을 반환합니다. */
        const string& getStatus() const { return _status; }

        /** @brief 현재 진행률(0.0f ~ 1.0f)을 반환합니다. */
        float32 getProgress() const { return _progress; }

        /** @brief 로드된 DDS 스플래시 이미지 데이터를 반환합니다. */
        const DdsImageData& getSplashImage() const { return _splashData; }

        /**
         * @brief BGRA 32bpp 이미지를 **상자 평균**으로 다른 크기에 옮겨 담습니다.
         *
         * @param pSource      원본 픽셀 (BGRA, 위에서 아래로).
         * @param sourceWidth  원본 가로. 0 이면 아무것도 하지 않습니다.
         * @param sourceHeight 원본 세로.
         * @param pOutPixel    결과를 담을 곳 — `destWidth * destHeight * 4` 바이트여야 합니다.
         * @param destWidth    결과 가로.
         * @param destHeight   결과 세로.
         *
         * @details 플랫폼마다 늘리는 방법이 다르다 — Win32 는 `StretchDIBits`(HALFTONE)가 해 주지만
         *          **X11 의 `XPutImage` 는 1:1 로만 찍는다.** 그래서 리눅스에서는 1376×768 원본이
         *          480×280 창에 좌상단만 그려지고 있었다. 줄이는 배율이 크므로(약 2.9배) 최근접이
         *          아니라 상자 평균을 쓴다 — 글자와 로고 가장자리가 부서지지 않게.
         *          늘릴 때는 원본 한 픽셀만 들어오므로 자연히 최근접이 된다.
         */
        static void scaleBgraImage( const uint8* pSource, uint32 sourceWidth, uint32 sourceHeight,
                                    uint8* pOutPixel, uint32 destWidth, uint32 destHeight );

        /** @brief 현재 플랫폼에 맞는 ISplashWindow 인스턴스를 생성하여 반환합니다. */
        static unique_ptr<ISplashWindow> createPlatformSplash();

    protected:
        /**
         * @brief 스플래시 이미지(textures/splash.dds)를 로드합니다.
         * @details 성공하면 픽셀은 **항상 BGRA 32bpp** 다 — 플랫폼 구현이 채널 순서를 다시 묻지
         *          않아도 되도록 여기서 맞춰 준다(`normalizeSplashToBgra`).
         */
        bool loadSplashImage();

        /**
         * @brief 로드한 스플래시 픽셀을 **BGRA 순서로 맞춥니다.** 이미 BGRA 면 아무것도 하지 않습니다.
         * @details Win32 의 `BI_RGB` 32bpp DIB 와 X11 의 리틀엔디언 TrueColor 비주얼이 **같은 순서**
         *          (메모리에서 B,G,R,X)를 원하므로, 플랫폼마다 따로 뒤집을 이유가 없다.
         */
        void normalizeSplashToBgra();

    protected:
        string                 _status;
        DdsImageData           _splashData;
        float32                _progress;
        uint32                 _width;
        uint32                 _height;
        uint8                  _bOpen    : 1;
        [[maybe_unused]] uint8 _reserved : 7;
    };
} // namespace sw
