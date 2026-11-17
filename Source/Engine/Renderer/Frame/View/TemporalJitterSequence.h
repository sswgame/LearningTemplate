/**
 * @file TemporalJitterSequence.h
 * @brief TAA 의 서브픽셀 지터(`TemporalJitterSequence`)입니다 — 프레임마다 Halton(2, 3) 표본 하나만큼 투영을 옮겨, 시간 누적이 픽셀 안의 여러 자리를 모으게 합니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

#include "Engine/EngineMinimal.h"

namespace sw
{
    /**
     * @class TemporalJitterSequence
     * @brief 뷰 하나의 지터 순서(표본 번호)와 그것을 뷰-투영에 싣는 계산입니다. 언리얼 TAA · 유니티 URP/HDRP TAA · Godot TAA 와 같은 Halton(2, 3) 8 표본입니다.
     * @details 지터는 래스터(`g_ViewProj` · `g_InvViewProj`)에만 싣는다. 컬링 · 지난 뷰-투영 · 그림자는 지터 없는 행렬을 받고(`FramePassExecutor::applyViewProjection`),
     *          화면 속도는 이번 지터(`g_TemporalJitter`)를 빼서 물체 · 카메라 움직임만 남긴다(material_shader_main.hlsli · binding.hlsli `computeCameraPreviousUv`).
     */
    class SW_API TemporalJitterSequence
    {
    public:
        /** @brief 한 바퀴의 표본 수입니다(언리얼 `r.TemporalAASamples` 기본 8 과 같다). */
        static constexpr uint32 kSampleCount = 8;

        TemporalJitterSequence();

        /**
         * @brief 다음 표본으로 넘기고, 그 오프셋을 렌더 크기 @p width × @p height 의 NDC 오프셋으로 돌려줍니다. 크기가 0 이면 지터가 없습니다.
         */
        [[nodiscard]] float2 advance( uint32 width, uint32 height );

        /** @brief 표본 @p sampleIndex(0 부터, `kSampleCount` 로 돈다)의 픽셀 오프셋입니다. 각 성분은 -0.5 .. 0.5 입니다. */
        [[nodiscard]] static float2 getPixelOffset( uint32 sampleIndex );
        /**
         * @brief 뷰-투영 @p viewProj 에 NDC 오프셋 @p ndcOffset 을 싣습니다(클립 xy 에 오프셋 × 클립 w 를 더한다 — 원근 · 직교 모두 화면에서 같은 거리만큼 옮겨진다).
         * @details 행벡터 규약(`mul( v, M )`)이라 클립 x · y 는 M 의 0 · 1 열과의 내적, w 는 3 열과의 내적이다.
         */
        [[nodiscard]] static float4x4 applyToViewProjection( const float4x4& viewProj, const float2& ndcOffset );

    private:
        uint32 _sampleIndex;
    };
} // namespace sw
