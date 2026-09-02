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

        /** @brief 현재 플랫폼에 맞는 ISplashWindow 인스턴스를 생성하여 반환합니다. */
        static unique_ptr<ISplashWindow> createPlatformSplash();

    protected:
        /**
         * @brief 스플래시 이미지(textures/splash.dds)를 로드합니다.
         */
        bool loadSplashImage();

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
