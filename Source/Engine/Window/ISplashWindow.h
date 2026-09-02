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

        /** @brief 현재 진행 상황 텍스트를 업데이트하고 화면을 즉시 갱신합니다. */
        virtual void updateStatus( const utf8* pStatus ) = 0;

        /** @brief 스플래시 창을 닫고 리소스를 해제합니다. */
        virtual void dismiss() = 0;

        /** @brief 스플래시 창이 현재 열려 있는지 확인합니다. */
        virtual bool isOpen() const { return _bOpen; }

        /** @brief 타이틀 문자열을 반환합니다. */
        const string& getTitle() const { return _title; }

        /** @brief 현재 상태 문자열을 반환합니다. */
        const string& getStatus() const { return _status; }

        /** @brief 현재 플랫폼에 맞는 ISplashWindow 인스턴스를 생성하여 반환합니다. */
        static unique_ptr<ISplashWindow> createPlatformSplash();

    protected:
        string _title;
        string _status;
        uint32 _width;
        uint32 _height;
        bool   _bOpen;
    };
} // namespace sw
