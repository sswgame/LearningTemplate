/**
 * @file Engine/Graphics/Debug/IVisualLogger.h
 * @brief 비주얼 로거의 쓰기 창구입니다 — 게임 코드가 판단의 근거를 글과 도형으로 적습니다(언리얼 `UE_VLOG` 자리).
 * @details 기록(고리 버퍼) · 질의 · 다시 보기 · 파일은 개발 도구(`DevTools/DebugDraw/VisualLogger`)가 들고 이 칸에 건다. 배포 구성은 개발 도구를 짓지 않아 칸이 비고,
 *          입구는 인라인 빈 함수다(언리얼 `ENABLE_VISUAL_LOG` 가 배포에서 호출부째 지우는 자리를 매크로 대신 형식으로 둔다). 문자열을 만들기 전에 `isEnabled` 로 먼저 묻는다.
 *          자리는 절대 자리(`AbsolutePosition`)다 — 적은 뒤 원점이 옮겨져도 같은 곳이다. 적기는 어느 스레드에서 해도 된다(병렬 틱).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/BuildConfig.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/Name/hashed_string.h"
#include "Core/Subsystem/SubsystemSlot.h"

#include "Engine/Foundation/WorldOrigin.h"

namespace sw
{
    /**
     * @class IVisualLogger
     * @brief 파일 머리말 참고. 꺼져 있으면 적는 호출은 아무것도 하지 않는다.
     */
    class SW_API IVisualLogger
    {
    public:
        /** @brief 기록을 받는 구성인가. 배포 구성은 입구가 빈 함수다. */
        static constexpr bool kEnabled = build::kShipping == false;

        virtual ~IVisualLogger() = default;

        /** @brief 기록이 켜져 있는가(`visuallog.on`). 배포 구성은 늘 false 다. */
        [[nodiscard]] bool isEnabled() const
        {
            if constexpr ( kEnabled )
                return isEnabledInternal();
            else
                return false;
        }
        void logText( uint64 objectID, const hashed_string& category, const AbsolutePosition& position, string_view text, const float4& color )
        {
            if constexpr ( kEnabled )
                logTextInternal( objectID, category, position, text, color );
        }
        void logLine( uint64 objectID, const hashed_string& category, const AbsolutePosition& from, const AbsolutePosition& to, const float4& color, string_view note = {} )
        {
            if constexpr ( kEnabled )
                logLineInternal( objectID, category, from, to, color, note );
        }
        void logSphere( uint64 objectID, const hashed_string& category, const AbsolutePosition& center, float32 radius, const float4& color, string_view note = {} )
        {
            if constexpr ( kEnabled )
                logSphereInternal( objectID, category, center, radius, color, note );
        }
        void logBox( uint64 objectID, const hashed_string& category, const AbsolutePosition& center, const float3& halfExtent, const float4& color, string_view note = {} )
        {
            if constexpr ( kEnabled )
                logBoxInternal( objectID, category, center, halfExtent, color, note );
        }
        void logArrow( uint64 objectID, const hashed_string& category, const AbsolutePosition& from, const AbsolutePosition& to, const float4& color, string_view note = {} )
        {
            if constexpr ( kEnabled )
                logArrowInternal( objectID, category, from, to, color, note );
        }
        /** @brief 프레임 번호를 하나 올립니다. 엔진 루프가 프레임 끝에 한 번 부릅니다. */
        void endFrame()
        {
            if constexpr ( kEnabled )
                endFrameInternal();
        }

    protected:
        [[nodiscard]] virtual bool isEnabledInternal() const                                                                                                                                           = 0;
        virtual void               logTextInternal( uint64 objectID, const hashed_string& category, const AbsolutePosition& position, string_view text, const float4& color )                          = 0;
        virtual void               logLineInternal( uint64 objectID, const hashed_string& category, const AbsolutePosition& from, const AbsolutePosition& to, const float4& color, string_view note )  = 0;
        virtual void               logSphereInternal( uint64 objectID, const hashed_string& category, const AbsolutePosition& center, float32 radius, const float4& color, string_view note )          = 0;
        virtual void               logBoxInternal( uint64 objectID, const hashed_string& category, const AbsolutePosition& center, const float3& halfExtent, const float4& color, string_view note )   = 0;
        virtual void               logArrowInternal( uint64 objectID, const hashed_string& category, const AbsolutePosition& from, const AbsolutePosition& to, const float4& color, string_view note ) = 0;
        virtual void               endFrameInternal()                                                                                                                                                  = 0;

    private:
        SW_SUBSYSTEM_SLOT( IVisualLogger, Optional );
    };
} // namespace sw
