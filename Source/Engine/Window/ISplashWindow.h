/**
 * @file ISplashWindow.h
 * @brief 플랫폼 독립적인 스플래시 화면 인터페이스
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    /**
     * @struct SplashImageData
     * @brief stb_image를 통해 디코딩된 스플래시 이미지의 RGBA 픽셀 버퍼 정보
     */
    struct SplashImageData
    {
        uint8* _pPixels{ nullptr };
        int32  _width{ 0 };
        int32  _height{ 0 };
        int32  _channels{ 0 };
    };

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
        virtual bool isOpen() const { return _bOpen; }

        /** @brief 타이틀 문자열을 반환합니다. */
        const string& getTitle() const { return _title; }

        /** @brief 현재 상태 문자열을 반환합니다. */
        const string& getStatus() const { return _status; }

        /** @brief 현재 진행률(0.0f ~ 1.0f)을 반환합니다. */
        float32 getProgress() const { return _progress; }

        /** @brief 스플래시 이미지 절대 경로를 반환합니다. (없으면 빈 문자열) */
        const string& getSplashImagePath() const { return _splashImagePath; }

        /** @brief 디코딩된 스플래시 이미지 버퍼 데이터를 반환합니다. */
        const SplashImageData& getSplashImage() const { return _splashImage; }

        /** @brief 현재 플랫폼에 맞는 ISplashWindow 인스턴스를 생성하여 반환합니다. */
        static unique_ptr<ISplashWindow> createPlatformSplash();

        /** @brief 리소스 시스템에서 스플래시 이미지(textures/splash.jpg, .png)의 절대 경로를 검색합니다. */
        static string findSplashImagePath();

    protected:
        /** @brief stb_image를 사용하여 스플래시 이미지 파일을 4채널(RGBA) 버퍼로 디코딩합니다. */
        bool loadSplashImage( string_view absolutePath );

        /** @brief 로드된 스플래시 이미지 픽셀 메모리를 해제합니다. */
        void freeSplashImage();

    protected:
        string          _title;
        string          _status;
        string          _splashImagePath;
        SplashImageData _splashImage;
        float32         _progress;
        uint32          _width;
        uint32          _height;
        bool            _bOpen;
    };
} // namespace sw
