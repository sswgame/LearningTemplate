/**
 * @file LensFlareSpriteBuffer.h
 * @brief 빛마다의 렌즈 플레어(`LensFlareAsset`)를 이번 프레임 주 시점의 스프라이트로 펼쳐 구조 버퍼에 올립니다. 렌즈 플레어 패스(lens_flare.hlsl)가 읽습니다.
 * @details 흐름: 게임 스레드의 수집기가 플레어를 단 빛마다 `LensFlareRequest` 를 패킷에 싣는다(`RenderPacketSceneCollector::collectLensFlares`). 렌더 스레드는 기록 전에
 *          `prepare` 로 빛을 주 시점에 투영하고(화면 밖 · 카메라 뒤는 뺀다), 가장자리 감쇠를 곱해 원소마다 스프라이트 하나를 만든다. 가림은 GPU 가 본다 — 픽셀 셰이더가
 *          빛 자리 둘레의 장면 깊이 표본 중 가리지 않은 몫을 곱한다(`computeVisibility` 와 같은 식). 블룸 기반 고스트(화면 전체 효과)와 별개의 패스다.
 *          스프라이트 배치는 셰이더 `LensFlareSprite` 와 같다(float4 넷).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/Lighting/LensFlareAsset.h"
#include "Engine/Graphics/RHI/RHIStructuredBuffer.h"

namespace sw
{
    class IRHIDevice;

    /** @brief 플레어를 단 빛 하나입니다(게임 스레드가 만들고 렌더 스레드가 읽는다). */
    struct LensFlareRequest
    {
        float3                           _position{};               ///< 점광 · 스폿의 월드 위치
        float3                           _direction{};              ///< 방향광이 나아가는 방향(단위) — 빛은 그 반대쪽 무한히 먼 곳이다
        float3                           _color{};                  ///< 빛 색 × 세기
        uint8                            _bDirectional{ SW_FALSE }; ///< 방향광이면 참
        shared_ptr<const LensFlareAsset> _pAsset;                   ///< 플레어 데이터(읽지 못한 빛은 요청을 만들지 않는다)
    };

    /** @brief GPU 스프라이트 하나입니다(lens_flare.hlsl `LensFlareSprite` 와 같은 배치). */
    struct LensFlareSprite
    {
        float4 _centerHalfSize{}; ///< xy NDC 가운데, zw NDC 반 크기
        float4 _colorShape{};     ///< rgb 색, a 모양(`LensFlareShape`)
        float4 _lightUVKernel{};  ///< xy 빛의 화면 UV, zw 가림 표본 반지름(UV)
        float4 _depthFade{};      ///< x 빛의 장치 깊이(방향광 1), y 가장자리 감쇠, zw 0
    };

    /** @brief 이번 프레임 렌즈 플레어 스프라이트의 버퍼 소유자입니다(렌더 스레드). */
    class SW_API LensFlareSpriteBuffer
    {
    public:
        /// @brief 스프라이트 하나의 float4 수입니다(lens_flare.hlsl `LensFlareSprite`).
        static constexpr uint32 kSpriteFloat4Count = 4;
        static_assert( sizeof( LensFlareSprite ) == sizeof( float4 ) * kSpriteFloat4Count, "LensFlareSprite must match lens_flare.hlsl (four float4)" );
        /// @brief 가림 판정의 깊이 여유 — 빛 깊이보다 이만큼 앞의 표본까지는 가리지 않은 것으로 본다(셰이더 kLensFlareDepthBias 와 같다).
        static constexpr float32 kDepthBias = 1.0e-4f;

        /**
         * @brief 빛을 뷰-투영으로 투영합니다. 결과는 NDC xy 와 장치 깊이(방향광 1)입니다.
         * @return 카메라 뒤이거나 화면(NDC [-1, 1]²) 밖 · 깊이 범위 밖이면 false — 그 빛은 플레어가 없다.
         */
        [[nodiscard]] static bool projectLight( const LensFlareRequest& request, const float4x4& viewProj, float3& outNdcDepth );
        /** @brief 가장자리 감쇠입니다 — 화면 끝까지의 거리(1 - max(|x|, |y|))를 폭 @p edgeFade 로 나눠 [0, 1] 로. 폭이 0 이면 화면 안은 1 이다. */
        [[nodiscard]] static float32 computeEdgeFade( const float2& ndc, float32 edgeFade );
        /** @brief 원소의 NDC 가운데입니다 — 빛(0)에서 화면 가운데(1)를 지나는 축 위의 자리. */
        [[nodiscard]] static float2 computeElementCenter( const float2& lightNdc, float32 axisPosition );
        /** @brief 깊이 표본 중 빛을 가리지 않은 몫(0 ~ 1)입니다 — 표본 깊이가 빛 깊이 - 여유 이상이면 가리지 않는다. 셰이더의 같은 이름 함수와 같은 식이다. */
        [[nodiscard]] static float32 computeVisibility( const float32* pSampleDepth, uint32 sampleCount, float32 lightDepth );
        /**
         * @brief 요청들을 스프라이트로 펼칩니다(빛마다 원소 수만큼, 화면 밖 · 카메라 뒤 · 감쇠 0 인 빛은 뺀다).
         * @param aspect 주 시점의 가로 / 세로 — 크기(화면 높이 비율)를 NDC 로 옮긴다
         */
        static void buildSprites( const vector<LensFlareRequest>& listRequest, const float4x4& viewProj, float32 aspect, vector<LensFlareSprite>& outListSprite );

        /** @brief 기록 전에 부릅니다 — 스프라이트를 펼쳐 올립니다. 버퍼를 못 만들면 오류를 한 번 남기고 이번 프레임은 그리지 않는다. */
        void prepare( IRHIDevice* pDevice, const vector<LensFlareRequest>& listRequest, const float4x4& viewProj, float32 aspect );
        /** @brief 이번 프레임에 그릴 스프라이트 수입니다(0 이면 패스가 그리지 않는다). */
        uint32 getSpriteCount() const { return _spriteCount; }
        /** @brief 스프라이트 구조 버퍼입니다(패스 버퍼 슬롯 `shaderslot::kPassBuffer` 로 건다). */
        const RHIStructuredBuffer& getBuffer() const { return _buffer; }
        /** @brief 버퍼를 놓습니다. */
        void release( IRHIDevice* pDevice );

    private:
        RHIStructuredBuffer     _buffer;
        vector<LensFlareSprite> _listSpriteScratch;
        uint32                  _spriteCount{ 0 };
        uint8                   _bReportedFailure{ SW_FALSE };
    };
} // namespace sw
