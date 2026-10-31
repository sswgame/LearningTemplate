/**
 * @file FrameFog.h
 * @brief 안개와 볼륨 안개(fog.hlsl · volumetric_fog.hlsl)입니다 — 이번 프레임의 안개 상수, 볼륨 안개 버퍼 · 입력, 볼륨 컴퓨트 기록.
 * @details 안개 패스 자체(`RenderPassType::Fog`)는 다른 풀스크린 패스처럼 렌더러의 패스 실행이 그리고, 여기서는 그 패스가 읽을 값과 볼륨 버퍼를 준다. 렌더러가 소유한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Graphics/RHI/RHIStructuredBufferSlot.h"
#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    struct FogSettings;
    struct FramePassContext;
    struct FrameViewTarget;
    struct RenderGraphPassDesc;
    struct RenderView;

    class FramePSOManager;
    class GPULightBuffer;
    class IRHICommandList;
    class IRHIDevice;
    class RenderPipelineAsset;

    /**
     * @brief 이번 프레임의 안개를 셰이더 상수 모양으로 둔 것입니다(`g_FogColorDensity` · `g_FogHeightParams` · `g_FogInscatter` · `g_FogVolumeParams`, 배치는 `FogSettings` 머리 주석).
     * @details 볼륨 안개 값(`_volumeParams`)은 주 시점에서 볼륨이 실제로 계산된 프레임에만 x=1 로 켜진다(안개 패스가 뷰마다 덮는다 — 추가 뷰는 볼륨을 계산하지 않는다).
     */
    struct FrameFogState
    {
        float4 _colorDensity{};          ///< rgb 안개 색, w 밀도
        float4 _heightParams{};          ///< x 높이 감쇠, y 기준 높이, z 시작 거리, w 최대 불투명도
        float4 _inscatter{};             ///< rgb 방향광 산란 색 × 세기, w 지수
        float4 _volumeParams{};          ///< x 볼륨 안개를 켠 1 · 끈 0, y 볼륨 거리, z 산란 배율, w 이방성
        uint8  _bVolumetric{ SW_FALSE }; ///< 설정이 볼륨 안개를 켰다(컴퓨트가 돌지는 `FrameFog::isFogVolumeActive`)
    };

    /**
     * @class FrameFog
     * @brief 프레임 안개 상태와 볼륨 안개입니다. 파일 머리말 참고.
     */
    class SW_API FrameFog
    {
    public:
        FrameFog( IRHIDevice* const& pDevice, const FramePSOManager& psoManager );
        FrameFog( const FrameFog& )            = delete;
        FrameFog& operator=( const FrameFog& ) = delete;

        /** @brief 볼륨 안개 버퍼를 놓습니다. */
        void release();
        /** @brief 이번 프레임의 안개를 정합니다. 씬 직접 경로와 패킷 경로가 같은 규칙으로 부릅니다. */
        void applyFrameFog( bool bHasFog, const FogSettings& fog );
        /**
         * @brief 볼륨 안개의 버퍼(froxel 출력, 갱신 입력)를 갖추고 주 시점의 입력을 올립니다. **기록 전에** 부릅니다(라이트 그리드와 주 시점이 정해진 뒤).
         * @details 설정이 볼륨 안개를 켰고 컴퓨트 PSO 가 있을 때만 켠다. 못 만들면 오류를 한 번 남기고 볼륨 없이 평범한 안개로 그린다 — 같은 패스의 꺼진 상태다.
         * @param keyLightDirIntensity · keyLightColorAmbient 이번 프레임의 주 광원 값(`g_KeyLightDirIntensity` · `g_KeyLightColor`)
         * @param mainLightGridIndex 주 시점의 라이트 그리드 번호(`FrameViewTarget::_lightGridIndex`)
         */
        void prepareVolumetricFog( IRHIDevice* pDevice, const RenderView& mainView, const float4& keyLightDirIntensity, const float4& keyLightColorAmbient,
                                   const GPULightBuffer& lightBuffer, uint32 mainLightGridIndex );
        /**
         * @brief 볼륨 안개 컴퓨트를 @p cmd 에 기록합니다(라이트 그리드 디스패치 뒤). `isFogVolumeActive` 일 때만 돕니다.
         * @param placeholderSRV 라이트 버퍼 · 그리드가 없는 프레임에 t0 · t2 에 거는 자리표(DX12 는 빈 디스크립터를 읽으면 폴트)
         */
        void dispatchVolumetricFog( IRHICommandList& cmd, const GPULightBuffer& lightBuffer, RHIDescriptorIndex placeholderSRV ) const;
        /** @brief 이번 프레임에 안개(`FogComponent` · 물속)가 있으면 true 입니다. 기록 전에 정하고 패스는 읽기만 합니다. */
        bool hasFrameFog() const { return _bHasFrameFog == SW_TRUE; }
        /** @brief 이번 프레임의 안개 상수입니다. `hasFrameFog` 가 false 면 값이 비어 있습니다. */
        const FrameFogState& getFrameFog() const { return _frameFog; }
        /** @brief 이번 프레임에 볼륨 안개 컴퓨트가 돈다(`prepareVolumetricFog` 가 정한다) — 안개 패스가 주 시점에서 볼륨을 읽는다. */
        bool isFogVolumeActive() const { return _bFogVolumeActive == SW_TRUE; }
        /** @brief 볼륨 안개의 froxel 출력(구조 버퍼 float4 — 산란 rgb, 투과율 a)입니다. 안개 패스가 t11 로 읽습니다. */
        const RHIStructuredBufferSlot& getFogVolumeBuffer() const { return _fogVolumeBuffer; }

    private:
        IRHIDevice* const&     _pDevice; ///< 렌더러의 `_pDevice` 변수를 참조합니다
        const FramePSOManager& _psoManager;
        FrameFogState          _frameFog;
        /// @brief 볼륨 안개의 froxel 출력입니다. 컴퓨트가 쓰고 안개 패스가 t11 로 읽습니다(`volumetric_fog.hlsl`).
        RHIStructuredBufferSlot _fogVolumeBuffer;
        /// @brief 볼륨 안개 컴퓨트의 입력(float4 행, `SW_FOG_VOLUME_PARAM_FLOAT4_COUNT` 개)입니다. 프레임마다 올립니다.
        RHIStructuredBufferSlot _fogVolumeParamBuffer;
        vector<float4>          _listScratchFogVolumeParam; ///< 그 행의 업로드 임시 버퍼(프레임마다 다시 쓴다)
        uint8                   _bHasFrameFog{ SW_FALSE };
        uint8                   _bFogVolumeActive{ SW_FALSE };
    };
} // namespace sw
