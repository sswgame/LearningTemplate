/**
 * @file DebugDrawCanvas.h
 * @brief 디버그 드로우 큐(`DebugDrawQueue`)의 선 · 구 · 글자를 게임 화면 위에 겹쳐 그립니다(캔버스 — 화면 2D 그리기 목록).
 * @details 에디터 씬 뷰는 큐를 ImGui 오버레이로 그리지만 그 그림은 게임 화면(App 창 · 에디터 게임 뷰 RT · 스크린샷)에 담기지 않는다.
 *          새 렌더 패스 · 셰이더를 두지 않고, 이미 주 출력의 마지막에 그려지는 캔버스 패스(UI)에 얇은 사각형으로 싣는다 — 선분은 게임 스레드에서
 *          주 시점 행렬로 투영하고(근평면에서 자른다), 글자는 캔버스 글리프로 칠한다. 깊이 테스트가 없어 가려진 선도 보인다(디버그 도형의 뜻에 맞다).
 *          켜는 것은 `-gv_debugDrawGameView=1`(에디터 게임 뷰 툴바의 단추)이고, 테스트 변수라 Shipping 에는 없다.
 *          언리얼 `DrawDebugLine` 이 게임 뷰포트에 그려지는 자리와 같다(그쪽은 배치 선 렌더러로 깊이를 쓴다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    struct CanvasDrawList;

    class DebugDrawQueue;
    class FontService;

    /**
     * @struct DebugDrawCanvasUtil
     * @brief 큐가 확정한 도형을 그리기 목록 뒤에 칠합니다(물리 픽셀 = UI 단위).
     */
    struct SW_API DebugDrawCanvasUtil
    {
        /** @brief 선 두께(픽셀)입니다. 에디터 씬 뷰의 선과 같은 두께입니다. */
        static constexpr float32 kLineWidthPixels = 1.5f;
        /** @brief 글자 크기(픽셀)입니다. */
        static constexpr float32 kTextFontSize = 14.0f;
        /** @brief 이 w 보다 가까운(뒤쪽 포함) 점은 근평면으로 당깁니다. */
        static constexpr float32 kNearW = 0.001f;

        /** @brief 게임 화면 겹치기가 켜져 있는가입니다(`-gv_debugDrawGameView`). Shipping 은 늘 false 입니다. */
        static bool isGameViewOverlayEnabled();
        /** @brief 게임 화면 겹치기를 켜고 끕니다(에디터 게임 뷰 툴바의 단추). 자식 프로세스는 개발 콘솔 줄 `gv_debugDrawGameView 1` 로 같은 값을 받는다. */
        static void setGameViewOverlayEnabled( bool bEnabled );

        /**
         * @brief 보이는 선분(구는 대원으로 펼친다)과 글자를 @p viewProj 로 투영해 @p outCanvas 뒤에 칠합니다. 반환값은 칠한 선분 수 + 글자 수입니다.
         * @param targetSize 주 출력의 픽셀 크기입니다(`outCanvas._targetSize` 와 같아야 가위가 맞는다).
         * @param pFontService 없거나 초기화 전이면 글자는 칠하지 않는다(선만).
         */
        static uint32 paint( const DebugDrawQueue& queue, const float4x4& viewProj, const float2& targetSize, FontService* pFontService, uint64 frameIndex,
                             CanvasDrawList& outCanvas );
        /**
         * @brief 월드 선분을 화면 픽셀(왼쪽 위 원점, 아래로 +y)로 투영합니다. 한쪽이 카메라 뒤면 근평면에서 자르고, 둘 다 뒤면 false 입니다.
         * @details 에디터 씬 뷰의 투영(`EditorViewportProjectionUtil::projectSegment`)과 같은 규칙이다.
         */
        [[nodiscard]] static bool projectSegment( const float4x4& viewProj, const float3& from, const float3& to, const float2& targetSize, float2& outFrom, float2& outTo );
    };
} // namespace sw
