/**
 * @file Engine/Presentation/IEngineUIDriver.h
 * @brief 엔진 루프 · 모듈 관리자가 런타임 UI 를 돌리는 창구입니다 — 엔진 루프는 UI 헤더를 읽지 않고 이 창구만 부릅니다(`IEngineRenderDriver` 와 같은 모양).
 * @details 구현은 `UISystem` 이고 기동 부품(`StartupParts::_pUIDriver`)과 엔진 서비스 칸으로 건다. 전용 서버는 UI 폴더를 짓지 않아 창구가 없고 엔진은 그 단계를 건너뛴다.
 *          언리얼 `FSlateApplication` 의 틱을 엔진 루프가 묻지 않고 호스트가 붙이는 것과 같은 자리다(기억).
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/Subsystem/ServiceSlot.h"

namespace sw
{
    /** @struct EngineUIFrame @brief UI 한 프레임의 입력입니다(엔진 루프가 주 출력에서 채운다). */
    struct EngineUIFrame
    {
        uint32  _width{ 0 };                   ///< 게임이 그려지는 픽셀 너비(주 출력 · 없으면 백버퍼)
        uint32  _height{ 0 };                  ///< 픽셀 높이
        int2    _pointerOrigin{ 0, 0 };        ///< 주 출력의 창 안 왼쪽 위(백버퍼면 0)
        float32 _contentScale{ 1.0f };         ///< 주 창의 DPI 배율
        float4  _safeInsets{};                 ///< 플랫폼 안전 여백(왼 · 위 · 오른 · 아래 픽셀 — 창 전체에 그릴 때만, 아니면 0)
        float32 _unscaledDeltaSeconds{ 0.0f }; ///< 실제 경과 — 정지 메뉴 아래서도 애니메이션 · 자막 · 알림이 흐른다
    };

    /** @class IEngineUIDriver @brief 런타임 UI 의 프레임 단계입니다. 게임 스레드에서 부릅니다. */
    class SW_API IEngineUIDriver
    {
    public:
        virtual ~IEngineUIDriver() = default;

        /** @brief 게임 틱보다 먼저 입력을 받습니다 — UI 가 먹은 행동은 이번 프레임 폰의 의도에 들지 않는다. 서지 않았으면 아무 일도 하지 않습니다. */
        virtual void processFrameInput( float32 unscaledDeltaSeconds ) = 0;
        /** @brief 씬 틱 뒤 UI 를 갱신합니다(음성 자막 신호 · 화면 틱 · 레이아웃). 서지 않았으면 아무 일도 하지 않습니다. */
        virtual void updateFrame( const EngineUIFrame& frame ) = 0;
        /** @brief 게임 update 가 돌지 않는 동안(에디터 멈춤 · Simulate) 화면 그리기와 포인터 입력을 막습니다. */
        virtual void setOnScreenSuppressed( bool bSuppressed ) = 0;

        SW_SERVICE_SLOT( IEngineUIDriver, Optional );
    };
} // namespace sw
