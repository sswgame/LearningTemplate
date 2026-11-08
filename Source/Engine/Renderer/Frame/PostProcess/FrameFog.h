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
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Graphics/RHI/RHIStructuredBuffer.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Reflection/ReflectionMacros.h"

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
     * @brief 주 시점의 볼륨 안개가 이번 프레임에 어떤 상태인지 — 켜졌거나 왜 꺼졌는지입니다(에디터 · 자동화가 이유를 보인다).
     * @details 볼륨을 못 켜면 같은 안개 패스가 평범한 안개로 그린다. 그 그림을 "볼륨 안개가 된다" 로 오인하지 않게 이유를 값으로 낸다(언리얼 Volumetric Fog 의 Show 플래그 · 경고와 같은 자리).
     */
    ENUM()
    enum class FogVolumeState : uint8
    {
        NoFog,           ///< 씬에 안개(`FogComponent` · 물속)가 없다
        Disabled,        ///< 안개 설정이 볼륨 안개를 켜지 않았다
        Underwater,      ///< 물속 안개는 볼륨이 없다
        NoDevice,        ///< 디바이스가 없다(그리지 않는 프레임)
        MissingPipeline, ///< 볼륨 안개 컴퓨트 PSO 가 없다(셰이더 쿠킹 · 백엔드 컴퓨트 지원)
        BufferFailed,    ///< froxel 버퍼나 입력 버퍼를 만들지 못했다
        Active,          ///< 컴퓨트가 돌고 안개 패스가 볼륨을 읽는다
    };

    /**
     * @brief 안개 패스의 진단 보기입니다(개발 빌드 `gv_fogDebugView`). 저작 값은 바꾸지 않는다 — 같은 프레임 조건에서 그림만 다르게 낸다.
     * @details 언리얼의 Buffer Visualization · 분할 비교와 같은 자리다. 진단 보기는 볼륨이 켜진 주 시점에서만 뜻이 있고, 꺼졌으면 단면은 검정이다(이유는 `FogVolumeState`).
     */
    ENUM()
    enum class FogDebugView : uint8
    {
        None,          ///< 보통 그림
        Compare,       ///< 화면 왼쪽 반은 볼륨 안개, 오른쪽 반은 평범한 안개(같은 프레임 · 같은 설정)
        Scattering,    ///< froxel 단면(`gv_fogDebugSlice` 번째 거리 조각까지 누적한 산란 rgb)을 화면 칸 그대로
        Transmittance, ///< 같은 단면의 투과율(흰색 1 = 안개 없음)
    };

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
        const RHIStructuredBuffer& getFogVolumeBuffer() const { return _fogVolumeBuffer; }
        /** @brief 이번 프레임의 볼륨 상태(켜졌거나 왜 꺼졌는지)입니다. `prepareVolumetricFog` 가 정한다. */
        FogVolumeState getVolumeState() const { return _volumeState; }
        /**
         * @brief 안개 패스의 진단 상수(`g_FogDebugParams` — x 보기 번호, y 단면 조각, z 비교 경계 uv.x)입니다. 개발 빌드의 `gv_fogDebugView` · `gv_fogDebugSlice` 를 읽습니다.
         * @param bVolume 이 뷰가 볼륨을 읽는지(주 시점이고 컴퓨트가 돌았는지). 아니면 진단 보기를 끈다 — 읽을 볼륨이 없다.
         */
        static float4 makeDebugParams( bool bVolume );
        /** @brief 주 렌더러가 마지막으로 낸 볼륨 상태입니다(게임 스레드 · 에디터 · 감시 값이 읽는다). 아직 그리지 않았으면 `NoFog` 입니다. */
        static FogVolumeState getLastMainVolumeState();
        /** @brief 주 렌더러가 프레임을 그린 뒤 그 상태를 올립니다(렌더 스레드 하나가 쓴다 — 미리보기 렌더러는 올리지 않는다). */
        static void publishMainVolumeState( FogVolumeState state );

    private:
        IRHIDevice* const&     _pDevice; ///< 렌더러의 `_pDevice` 변수를 참조합니다
        const FramePSOManager& _psoManager;
        FrameFogState          _frameFog;
        /// @brief 볼륨 안개의 froxel 출력입니다. 컴퓨트가 쓰고 안개 패스가 t11 로 읽습니다(`volumetric_fog.hlsl`).
        RHIStructuredBuffer _fogVolumeBuffer;
        /// @brief 볼륨 안개 컴퓨트의 입력(float4 행, `SW_FOG_VOLUME_PARAM_FLOAT4_COUNT` 개)입니다. 프레임마다 올립니다.
        RHIStructuredBuffer _fogVolumeParamBuffer;
        vector<float4>      _listScratchFogVolumeParam; ///< 그 행의 업로드 임시 버퍼(프레임마다 다시 쓴다)
        uint8               _bHasFrameFog{ SW_FALSE };
        uint8               _bFogVolumeActive{ SW_FALSE };
        FogVolumeState      _volumeState{ FogVolumeState::NoFog };
    };

    /** @brief 안개 패스의 진단 보기입니다(`FogDebugView`). 에디터의 볼륨 안개 창이 고른다. */
    SW_EXTERN_TEST_GLOBAL_VARIABLE( FogDebugView, gv_fogDebugView );
    /** @brief 진단 단면의 거리 조각 번호(0 .. `SW_FOG_VOLUME_DIM_Z` - 1)입니다. 큰 번호가 먼 조각이고 그 조각까지 누적한 값이다. */
    SW_EXTERN_TEST_GLOBAL_VARIABLE( int32, gv_fogDebugSlice );
} // namespace sw
