/**
 * @file FrameRainOcclusion.h
 * @brief 비 가림 맵의 GPU 자원과 이번 프레임의 상태입니다 — 위에서 내려다본 깊이 한 장, 그 컬링 입력, GPU 강수가 읽는 R32 사본.
 * @details 맵은 프레임에 한 번, 컬링 뒤 · 뷰 그래프 앞에 프리패스 리스트로 그린다(`FramePassExecutor::drawRainOcclusion` — 그림자 PSO 와 자기 컬링 슬롯). 그래서 이번 프레임의 모든 뷰가 같은 맵 · 같은 행렬을 읽는다.
 *          GPU 강수는 프레임 그래프 맨 앞(컬링 앞)에서 돌아 이번 맵이 아직 없다 — 지난 프레임의 맵을 깊이 히스토리 패스가 R32 로 옮겨 읽는다(장면 깊이 충돌과 같은 길, `DepthHistory` 의 뷰 다음 칸).
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Environment/Rain/RainOcclusionSettings.h"
#include "Engine/Graphics/RHI/RHITextureSlot.h"
#include "Engine/Renderer/Frame/View/RenderView.h"

namespace sw
{
    struct VFXDepthHistoryInput;

    class IRHIDevice;

    /** @class FrameRainOcclusion @brief 비 가림 맵의 주인입니다(파일 머리말 참고). 렌더 스레드만 만집니다. */
    class FrameRainOcclusion
    {
    public:
        FrameRainOcclusion();
        FrameRainOcclusion( const FrameRainOcclusion& )            = delete;
        FrameRainOcclusion& operator=( const FrameRainOcclusion& ) = delete;

        /**
         * @brief 이번 프레임의 맵을 준비합니다(기록 전) — 지난 프레임에 그린 맵을 강수의 입력으로 넘기고, 필요하면 깊이 · 사본 · 컬링 상수 버퍼를 갖춘다.
         * @param depthFormat 그림자 PSO 의 깊이 형식 — Unknown 이면(그림자 패스가 없는 파이프라인) 맵이 없다
         * @param bNeeded     맵을 읽는 것이 있다(날씨 값이 0 이 아니다 · 맵을 읽는 강수). 없으면 그리지 않는다
         * @param bParticles  맵을 읽는 GPU 강수가 있다 — R32 사본을 둔다
         */
        void beginFrame( IRHIDevice* pDevice, const RainOcclusionSettings& settings, const float3& viewPosition, RHIFormat depthFormat, bool bNeeded, bool bParticles );
        /** @brief 텍스처 · 상수 버퍼를 놓습니다. */
        void release( IRHIDevice* pDevice );

        /** @brief 이번 프레임에 맵을 그린다(자원이 모두 있다). */
        [[nodiscard]] bool isActive() const { return _bActive == SW_TRUE; }
        /** @brief 이번 프레임의 맵을 그렸다 — 이 뒤의 뷰 그래프가 읽는다. */
        [[nodiscard]] bool isDrawn() const { return _bDrawn == SW_TRUE; }
        void               markDrawn() { _bDrawn = SW_TRUE; }
        /** @brief 지난 프레임의 맵을 강수의 R32 사본으로 옮길 수 있다 — 등록 · 복사 · 강수 입력이 같은 판정을 본다. */
        [[nodiscard]] bool hasParticleDepth() const { return _bContentValid == SW_TRUE && _particleDepth.isValid(); }
        /** @brief 강수가 읽는 지난 프레임 맵의 입력입니다(`hasParticleDepth` 일 때만). */
        [[nodiscard]] VFXDepthHistoryInput makeParticleInput() const;

        [[nodiscard]] const RHITextureSlot& getDepth() const { return _depth; }
        [[nodiscard]] const RHITextureSlot& getParticleDepth() const { return _particleDepth; }
        [[nodiscard]] const RenderView&     getCullInput() const { return _cullInput; }
        [[nodiscard]] const float4x4&       getViewProjection() const { return _cullInput._viewProj; }
        /** @brief `g_RainOcclusionParams` 입니다. 맵이 없는 프레임은 0 이라 셰이더가 읽지 않는다. */
        [[nodiscard]] const float4& getShaderParams() const { return _shaderParams; }
        [[nodiscard]] uint32        getCullSlot() const { return _cullSlot; }
        void                        setCullSlot( uint32 cullSlot ) { _cullSlot = cullSlot; }

    private:
        RHITextureSlot _depth;         ///< 위에서 내려다본 깊이(DSV · SRV)
        RHITextureSlot _particleDepth; ///< 지난 맵의 R32 실수 사본(RT · RW) — 맵을 읽는 강수가 있을 때만
        RenderView     _cullInput;     ///< 맵의 뷰-투영과 컬링 상수 버퍼
        float4x4       _contentViewProj;
        float3         _contentEye;
        float4         _shaderParams;
        uint32         _cullSlot;
        uint8          _bActive       : 1;
        uint8          _bDrawn        : 1;
        uint8          _bContentValid : 1; ///< `_depth` 에 지난 프레임에 그린 맵이 남아 있다(`_contentViewProj`)
    };
} // namespace sw
