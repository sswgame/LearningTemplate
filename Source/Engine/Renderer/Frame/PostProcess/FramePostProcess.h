/**
 * @file FramePostProcess.h
 * @brief 렌더러의 후처리 효과 셋(자동 노출 · FSR 1 업스케일 · 안개와 볼륨 안개)의 상태와 기록입니다.
 * @details 효과마다 뷰 상태(`FrameViewTarget` 의 노출 기록 · 업스케일 중간 RT)와 프레임 상태(안개 상수 · 볼륨 버퍼)를 셋업에서 갖추고, 패스 실행은 정해 둔 것을 읽기만 한다.
 *          안개 패스 자체(`RenderPassType::Fog`)는 다른 풀스크린 패스처럼 렌더러의 패스 실행이 그리고, 여기서는 그 패스가 읽을 값과 볼륨 버퍼를 준다.
 *          언리얼이 `PostProcessing.cpp` 의 효과별 `AddXxxPass` 를 장면 렌더러 밖 함수로 두고 뷰 상태(`FViewInfo`)만 받는 것과 같은 자리다.
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
        uint8  _bVolumetric{ SW_FALSE }; ///< 설정이 볼륨 안개를 켰다(컴퓨트가 돌지는 `FramePostProcess::isFogVolumeActive`)
    };

    /**
     * @class FramePostProcess
     * @brief 후처리 효과(자동 노출 · FSR 1 · 안개)의 셋업과 기록을 렌더러에서 떼어 둔 협력 객체입니다. 렌더러가 소유합니다.
     * @details 디바이스 · 파이프라인 에셋 · PSO 관리자는 렌더러의 것을 읽기만 한다(디바이스는 렌더러의 `_pDevice` 변수를 참조한다 — `FrameCapture` 와 같다).
     *          지금 그리는 뷰는 인자로 받는다 — 이 객체는 "어느 뷰인가" 를 기억하지 않는다(병렬 기록에서 패스끼리 덮어쓰지 않게).
     */
    class SW_API FramePostProcess
    {
    public:
        FramePostProcess( IRHIDevice* const& pDevice, const RenderPipelineAsset& pipeline, const FramePSOManager& psoManager );
        FramePostProcess( const FramePostProcess& )            = delete;
        FramePostProcess& operator=( const FramePostProcess& ) = delete;

        /** @brief 프레임 단위 GPU 자원(볼륨 안개 버퍼)을 놓습니다. 뷰 자원은 `releaseAutoExposureHistory` · `releaseUpscaleIntermediate` 가 뷰마다 놓습니다. */
        void release();

        /**
         * @brief HDR 색을 톤맵하는 첫 패스입니다(Tonemap, 또는 퍼뮤테이션 `SW_POST_TONEMAP` 을 켠 후처리 체인 Present — `passAppliesTonemap`). 없으면(2D 파이프라인) nullptr 입니다.
         * @details 포인터를 들고 있지 않고 지금 파이프라인에서 찾는다 — 파이프라인을 다시 읽으면 패스 목록이 바뀐다.
         */
        const RenderGraphPassDesc* findTonemapPass() const;

        // --- 자동 노출(auto_exposure.hlsl) ---
        /** @brief 뷰의 자동 노출 기록 둘(1x1 R32 실수 RT + SRV)을 **셋업 단계에서** 만들어 둡니다. 톤맵하는 패스가 없으면 만들지 않습니다. */
        void ensureAutoExposureHistory( FrameViewTarget& view );
        /** @brief 뷰의 자동 노출 기록을 놓습니다. 디바이스가 없으면 핸들만 잊습니다. */
        void releaseAutoExposureHistory( FrameViewTarget& view );
        /**
         * @brief 뷰 @p view 의 이번 프레임 노출을 정합니다 — 잴지, 측정 상수(EV 범위 · 다가가는 비율), 쓰는 기록 칸입니다. 셋업에서 뷰마다 한 번 부릅니다.
         * @details 카메라의 최소 EV 와 최대 EV 가 같으면 재지 않고 고정 노출 2^-EV 를 `g_CameraGrade.x` 가 될 @p inoutExposureScale 에 곱한다.
         * @param now 렌더러 시계(초, `FrameRenderState::getAnimationTime`)입니다. 쉬던 추가 뷰는 그동안의 시간만큼 한 번에 다가간다.
         */
        void prepareAutoExposure( FrameViewTarget& view, float32 now, float32& inoutExposureScale ) const;
        /**
         * @brief 톤맵하는 패스 앞에서 자동 노출 측정 · 적응 풀스크린을 같은 리스트에 기록하고, 그 패스가 읽을 이번 EV 를 `AutoExposure` 이름으로 겁니다.
         * @details @p passDesc 가 톤맵하는 패스가 아니거나 이번 프레임에 재지 않는 뷰면 아무것도 하지 않는다. 기록 중(병렬)에 불리므로 뷰 상태를 쓰지 않는다.
         */
        void recordAutoExposure( FramePassContext& context, const FrameViewTarget& view, const RenderGraphPassDesc& passDesc ) const;

        // --- FSR 1 업스케일(fsr1.hlsl) ---
        /**
         * @brief 뷰 @p view 가 이번 프레임에 Present 에서 FSR 1 로 키울지 정하고, 키우면 중간 RT 를 **셋업 단계에서** 뷰포트 크기로 맞춥니다.
         * @details Present 가 톤맵하는 파이프라인(후처리 체인)에서 켜면 한 번 오류를 남기고 선형으로 키운다 — 그 파이프라인에는 톤맵한 중간 그림이 없다.
         */
        void prepareUpscale( FrameViewTarget& view, uint32 viewportWidth, uint32 viewportHeight );
        /** @brief 뷰의 FSR 중간 RT 를 놓습니다. 디바이스가 없으면 핸들만 잊습니다. */
        void releaseUpscaleIntermediate( FrameViewTarget& view );
        /**
         * @brief Present 렌더 패스를 열기 **전에** EASU(배율 크기 원본 → 중간 RT)를 기록합니다. 이번 프레임에 FSR 로 키우지 않는 뷰면 false 이고 아무것도 하지 않습니다.
         * @return true 면 Present 는 블릿 대신 `drawUpscaleRcas` 로 그린다
         */
        bool recordUpscaleEasu( FramePassContext& context, const FrameViewTarget& view, string_view sourceName ) const;
        /** @brief 열린 Present 렌더 패스(뷰포트를 건 뒤)에 RCAS(중간 RT → 대상)를 그립니다. @p viewportX · @p viewportY 는 대상 안의 뷰포트 왼쪽 위(px)입니다. */
        void drawUpscaleRcas( FramePassContext& context, const FrameViewTarget& view, RHIFormat targetFormat, float32 viewportX, float32 viewportY ) const;

        // --- 안개 · 볼륨 안개(fog.hlsl · volumetric_fog.hlsl) ---
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
        IRHIDevice* const&         _pDevice; ///< 렌더러의 `_pDevice` 변수를 참조합니다(생성자 참고)
        const RenderPipelineAsset& _pipeline;
        const FramePSOManager&     _psoManager;
        FrameFogState              _frameFog;
        /// @brief 볼륨 안개의 froxel 출력입니다. 컴퓨트가 쓰고 안개 패스가 t11 로 읽습니다(`volumetric_fog.hlsl`).
        RHIStructuredBufferSlot _fogVolumeBuffer;
        /// @brief 볼륨 안개 컴퓨트의 입력(float4 행, `SW_FOG_VOLUME_PARAM_FLOAT4_COUNT` 개)입니다. 프레임마다 올립니다.
        RHIStructuredBufferSlot _fogVolumeParamBuffer;
        vector<float4>          _listScratchFogVolumeParam; ///< 그 행의 업로드 임시 버퍼(프레임마다 다시 쓴다)
        uint8                   _bHasFrameFog{ SW_FALSE };
        uint8                   _bFogVolumeActive{ SW_FALSE };
        /// @brief Present 가 톤맵하는 파이프라인에서 FSR 1 을 켰다는 오류를 남겼다(셋업에서만 읽고 쓴다 — 한 번만 알린다).
        uint8 _bUpscalePostChainLogged{ SW_FALSE };
    };
} // namespace sw
