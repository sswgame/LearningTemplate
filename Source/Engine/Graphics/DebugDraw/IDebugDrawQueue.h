/**
 * @file Engine/Graphics/DebugDraw/IDebugDrawQueue.h
 * @brief 디버그 지오메트리 큐의 엔진 창구입니다 — 엔진 코드가 선 · 구 · 상자 · 화살표 · 글을 넣고, 엔진 루프가 프레임을 닫고 월드 원점 이동을 알립니다.
 * @details 큐 · 보이는 목록 · 카테고리 토글 · 그리기는 개발 도구(`DevTools/DebugDraw/DebugDrawQueue`)가 들고 이 칸에 건다. 배포 구성은 개발 도구를 짓지 않아 칸이 비고,
 *          입구는 인라인 빈 함수라 부르는 비용도 없다. 언리얼 `UE_ENABLE_DEBUG_DRAWING` 이 배포에서 `DrawDebug*` 를 빈 인라인으로 만드는 자리를 매크로 대신 형식으로 둔다.
 *          자리는 씬 자리(`float3` — 월드 원점 기준 로컬)다. 넣기는 어느 스레드에서 해도 된다(구현이 잠근다). 칸의 정의는 엔진에 있어 개발 도구 없이도 읽는 쪽이 링크된다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/BuildConfig.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/Name/hashed_string.h"
#include "Core/Subsystem/SubsystemSlot.h"

namespace sw
{
    /**
     * @class IDebugDrawQueue
     * @brief 파일 머리말 참고. 지속 시간 0 은 한 프레임, 그보다 크면 그 시간(초) 동안 남는다. 카테고리를 비우면 `Default` 다.
     */
    class SW_API IDebugDrawQueue
    {
    public:
        /** @brief 넣기를 받는 구성인가. 배포 구성은 입구가 빈 함수다. */
        static constexpr bool kEnabled = build::kShipping == false;

        virtual ~IDebugDrawQueue() = default;

        void drawLine( const float3& from, const float3& to, const float4& color, float32 durationSeconds = 0.0f, const hashed_string& category = {} )
        {
            if constexpr ( kEnabled )
                drawLineInternal( from, to, color, durationSeconds, category );
        }
        void drawSphere( const float3& center, float32 radius, const float4& color, float32 durationSeconds = 0.0f, const hashed_string& category = {} )
        {
            if constexpr ( kEnabled )
                drawSphereInternal( center, radius, color, durationSeconds, category );
        }
        void drawBox( const float3& center, const float3& halfExtent, const float4& color, float32 durationSeconds = 0.0f, const hashed_string& category = {} )
        {
            if constexpr ( kEnabled )
                drawBoxInternal( center, halfExtent, color, durationSeconds, category );
        }
        void drawArrow( const float3& from, const float3& to, const float4& color, float32 durationSeconds = 0.0f, const hashed_string& category = {} )
        {
            if constexpr ( kEnabled )
                drawArrowInternal( from, to, color, durationSeconds, category );
        }
        void drawText( const float3& position, string_view text, const float4& color, float32 durationSeconds = 0.0f, const hashed_string& category = {} )
        {
            if constexpr ( kEnabled )
                drawTextInternal( position, text, color, durationSeconds, category );
        }

        /** @brief 이번 프레임에 넣은 것과 남은 것을 보이는 목록으로 확정하고 시간을 흘립니다. 엔진 루프가 프레임 끝에 한 번 부릅니다(게임 스레드). 0 이면(일시정지) 아무것도 빼지 않는다. */
        void endFrame( float32 deltaSeconds )
        {
            if constexpr ( kEnabled )
                endFrameInternal( deltaSeconds );
        }
        /** @brief 남은 도형을 @p delta 만큼 옮깁니다(월드 원점이 옮겨졌을 때 — 게임 스레드). */
        void shiftOrigin( const float3& delta )
        {
            if constexpr ( kEnabled )
                shiftOriginInternal( delta );
        }

    protected:
        virtual void drawLineInternal( const float3& from, const float3& to, const float4& color, float32 durationSeconds, const hashed_string& category )          = 0;
        virtual void drawSphereInternal( const float3& center, float32 radius, const float4& color, float32 durationSeconds, const hashed_string& category )        = 0;
        virtual void drawBoxInternal( const float3& center, const float3& halfExtent, const float4& color, float32 durationSeconds, const hashed_string& category ) = 0;
        virtual void drawArrowInternal( const float3& from, const float3& to, const float4& color, float32 durationSeconds, const hashed_string& category )         = 0;
        virtual void drawTextInternal( const float3& position, string_view text, const float4& color, float32 durationSeconds, const hashed_string& category )      = 0;
        virtual void endFrameInternal( float32 deltaSeconds )                                                                                                       = 0;
        virtual void shiftOriginInternal( const float3& delta )                                                                                                     = 0;

    private:
        SW_SUBSYSTEM_SLOT( IDebugDrawQueue, Optional );
    };
} // namespace sw
