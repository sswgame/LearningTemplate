/**
 * @file RealtimeProbeUpdater.h
 * @brief 실시간 라이트 프로브 갱신의 렌더 스레드 쪽입니다 — 복셀 · 기록(history) · 입력 행 버퍼를 갖추고 컴퓨트(`probe_update.hlsl`)를 기록합니다.
 * @details 흐름: 게임 스레드의 `collectSceneLights` 가 실시간 볼륨마다 `RealtimeProbeRequest`(볼륨 원소 사본, 갱신 값, 복셀 격자)를 만든다. 이 클래스는
 *          (1) `prepare` 가 기록 전에 버퍼를 갖춘다 - 복셀 단어(격자가 바뀐 프레임에만 올린다), 기록(볼륨 정체마다 구간 — 새로 든 볼륨만 0 에서 쌓는다), 입력 행(프레임마다);
 *          (2) `dispatch` 가 라이트 업로드 뒤 · 조명 전에 컴퓨트 한 번을 돈다. 컴퓨트는 모든 실시간 프로브를 한 번에 돈다 - 프로브마다 이번 프레임의 조면 광선을 쏘아 기록을 갱신하고,
 *          기록을 소비 셰이더 배치로 바꿔 프로브 버퍼(t13)에 쓴다. 프로브 버퍼는 CPU 가 프레임마다 0 으로 올리므로 갱신하지 않는 프레임에도 전부 다시 쓴다.
 *          하늘은 `SkyGradient::evaluateRadiance` 를 SH9 로 투영해 입력 행에 싣는다(광선이 닿지 않는 방향의 복사휘도 — 베이커와 같은 규칙).
 *          입력 행 배치(`probe_update.hlsl` 의 kRow*)는 `makeParamRows` 가 한 곳에서 짠다.
 */
#pragma once
#include "Core/Container/SpanAllocator.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/SphericalHarmonics.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/RHI/RHIStructuredBuffer.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Renderer/Lighting/GPULightBuffer.h"

namespace sw
{
    struct SkyGradient;
    class IRHICommandList;
    class IRHIDevice;

    /** @brief 요청 목록을 컴퓨트 버퍼에 놓는 자리입니다(볼륨마다 복셀 단어 · 기록 · 스레드 범위의 시작). 순수 계산이라 테스트가 직접 본다. */
    struct RealtimeProbeLayout
    {
        vector<uint32> _listVoxelWordOffset;       ///< 볼륨마다 복셀 단어 버퍼 안의 첫 단어
        vector<uint32> _listEmissionWordOffset;    ///< 볼륨마다 발광 칸(RGB9E5) 단어의 첫 단어 — 점유 단어 바로 뒤. 발광 칸이 없으면 `kNoEmission`
        vector<uint32> _listHistoryOffset;         ///< 볼륨마다 기록 버퍼 안의 첫 float4(`RealtimeProbeHistoryAllocator` 가 볼륨 정체로 매긴 구간 — 이어지지 않을 수 있다)
        vector<uint32> _listFirstThread;           ///< 볼륨마다 첫 스레드 번호(= 앞 볼륨들의 프로브 수의 합)
        uint32         _totalVoxelWords{ 0 };      ///< 점유 단어와 발광 단어의 합
        uint32         _totalHistoryElements{ 0 }; ///< 기록 버퍼 용량(float4, 할당기의 용량 — 빈 구간 포함)
        uint32         _totalProbes{ 0 };
    };

    /**
     * @class RealtimeProbeHistoryAllocator
     * @brief 실시간 볼륨마다 기록 버퍼 구간을 볼륨 정체(`RealtimeProbeRequest::_volumeID` — 컴포넌트 id)로 매기는 할당기입니다(구간 프리리스트 `SpanAllocator`).
     * @details 예전에는 (볼륨 번호, 프로브 수) 목록을 순서째 비교해 볼륨 하나가 드나들어도 모든 볼륨의 기록을 0 으로 되돌렸다. 지금은 서 있는 볼륨(같은 정체 · 같은 프로브 수)이
     *          구간과 누적 프레임을 지키고, 빠진 볼륨의 구간은 프리리스트로 돌아가며, 새로 든 볼륨만 0 에서 쌓는다(언리얼 Lumen 의 프로브 아틀라스가 볼륨마다 자리를 지키는 것과 같다).
     *          용량이 모자라면 두 배로 늘린다 — 버퍼를 다시 만들므로 그 프레임만 모든 볼륨이 새로 시작한다.
     */
    class SW_API RealtimeProbeHistoryAllocator
    {
    public:
        /// @brief 프로브 하나의 기록 float4 수 — 원시 SH9 계수 일곱 뒤에 가시성 모먼트 묶음(probe_update.hlsl kHistoryFloat4Count).
        static constexpr uint32 kFloat4PerProbe = SH9Color::kGPUFloat4Count + shaderslot::kProbeVisibilityFloat4Count;

        /**
         * @brief 이번 프레임 요청들의 구간을 정합니다.
         * @return 용량이 늘어 기록 버퍼를 다시 만들어야 하면 true 입니다(그때는 모든 볼륨이 새로 시작한다 — `isFresh` 가 모두 true).
         */
        [[nodiscard]] bool assign( const vector<RealtimeProbeRequest>& listRequest );
        /** @brief 요청 @p requestIndex 의 기록 첫 float4 입니다. */
        uint32 getOffset( uint32 requestIndex ) const { return _listOffset[requestIndex]; }
        /** @brief 요청 @p requestIndex 의 기록이 이번 프레임에 새로 시작하면(0 으로 채워야 하면) true 입니다. */
        bool isFresh( uint32 requestIndex ) const { return _listFresh[requestIndex] == SW_TRUE; }
        /** @brief 요청 @p requestIndex 의 기록이 시작한 뒤 갱신한 프레임 수입니다(섞는 비율의 초기 가속). */
        uint32 getFramesSinceReset( uint32 requestIndex ) const { return _listFramesSinceReset[requestIndex]; }
        /** @brief 기록 버퍼 용량(float4)입니다. */
        uint32 getCapacity() const { return _span.getCapacity(); }
        /** @brief 이번 프레임을 갱신했음을 적습니다 — 서 있는 볼륨마다 누적 프레임이 하나 는다. */
        void advanceFrame();
        /** @brief 모든 볼륨이 다음 `assign` 에서 새로 시작하게 합니다(버퍼를 놓거나 다시 만들지 못했을 때). */
        void clear();

    private:
        /** @brief 볼륨 하나의 기록 구간입니다. */
        struct HistorySlot
        {
            uint32 _offset{ 0 };
            uint32 _count{ 0 };
            uint32 _framesSinceReset{ 0 };
        };

    private:
        unordered_map<uint64, HistorySlot> _mapSlot;              ///< 볼륨 정체 → 구간
        SpanAllocator                      _span;                 ///< 기록 버퍼의 빈 구간
        vector<uint64>                     _listActiveID;         ///< 이번 프레임 요청의 정체(차례대로)
        vector<uint32>                     _listOffset;           ///< 요청마다 구간 시작
        vector<uint32>                     _listFramesSinceReset; ///< 요청마다 누적 프레임
        vector<uint8>                      _listFresh;            ///< 요청마다 새로 시작하는지
    };
} // namespace sw

namespace sw
{
    /** @brief 실시간 프로브 갱신 컴퓨트의 렌더 스레드 소유자입니다. */
    class SW_API RealtimeProbeUpdater
    {
    public:
        static constexpr uint32 kThreadGroupSize = 64;          ///< probe_update.hlsl SW_NUMTHREADS 와 같다
        static constexpr uint32 kHeaderRowCount  = 8;           ///< 머리 행 1 + 하늘 SH 행 7 (probe_update.hlsl kRowFirstVolume)
        static constexpr uint32 kVolumeRowCount  = 8;           ///< 볼륨마다 행 수 (probe_update.hlsl kVolumeRowCount)
        static constexpr uint32 kSkySampleCount  = 64;          ///< 하늘 SH 투영의 방향 수
        static constexpr uint32 kSkyRowCount     = 7;           ///< 하늘 SH 원시 계수 27 개가 float4 일곱(28 칸)에 들어간다
        static constexpr uint32 kNoEmission      = 0xFFFFFFFFu; ///< 발광 칸이 없는 볼륨의 발광 단어 번호(probe_update.hlsl kNoEmission)

        /** @brief 요청들의 버퍼 배치를 계산합니다. 기록 구간은 @p history(이번 프레임에 `assign` 한 것)가 매긴 자리다. */
        [[nodiscard]] static RealtimeProbeLayout planLayout( const vector<RealtimeProbeRequest>& listRequest, const RealtimeProbeHistoryAllocator& history );
        /**
         * @brief 하늘을 SH9 로 투영합니다(균일 구 방향 @p kSkySampleCount 개, 결정적). @p bHasSky 가 false 면 0 입니다 - 하늘이 없는 씬의 빈 방향은 어둡다(폴백 없음).
         */
        [[nodiscard]] static SH9Color projectSky( const SkyGradient& sky, bool bHasSky );
        /**
         * @brief 컴퓨트 입력 행을 짭니다(배치는 `probe_update.hlsl` 머리 주석).
         * @param listWarmup 볼륨마다 프로브당 지금까지 갱신된 횟수(섞는 비율의 초기 가속에 쓴다)
         * @param lightCount 직접광으로 읽을 CPU 라이트 수(라이트 버퍼의 앞쪽)
         */
        [[nodiscard]] static vector<float4> makeParamRows( const vector<RealtimeProbeRequest>& listRequest, const RealtimeProbeLayout& layout, const vector<uint32>& listWarmup,
                                                           const SH9Color& sky, uint32 frameIndex, uint32 lightCount );

        /**
         * @brief 기록 전에 부릅니다 - 버퍼를 갖추고 이번 프레임의 입력을 올립니다. 요청이 없거나 프로브 버퍼를 쓸 수 없으면 `isActive` 가 false 입니다.
         * @details 못 켜는 이유(컴퓨트 PSO 없음 · 프로브 버퍼 UAV 없음 · 버퍼 할당 실패)는 한 번만 오류로 남긴다 - 실시간 볼륨은 그동안 검게 남는다(베이크로 대신하지 않는다).
         */
        void prepare( IRHIDevice* pDevice, const vector<RealtimeProbeRequest>& listRequest, const GPULightBuffer& lightBuffer, const SkyGradient& sky, bool bHasSky, bool bPipelineReady );
        /** @brief 이번 프레임에 컴퓨트가 도는지입니다. */
        [[nodiscard]] bool isActive() const { return _bActive == SW_TRUE; }
        /**
         * @brief 기록을 0 에서 다시 쌓거나 복셀(점유 · 발광)이 바뀐 뒤 갱신한 프레임 수입니다(그린 프레임만 센다). 이번 프레임에 돌지 않으면 0 입니다 — 감시 값 `Render.GIAccumulatedFrames`.
         * @details 복셀이 바뀌어도 기록은 0 으로 돌리지 않고 섞어 따라간다 — 그래서 섞는 비율의 초기 가속(`_framesSinceReset`)과 따로 센다.
         */
        [[nodiscard]] uint32 getAccumulatedFrames() const { return _bActive == SW_TRUE ? _framesSinceInputChange : 0u; }
        /** @brief 컴퓨트를 기록합니다(라이트 업로드 뒤 · 조명 패스 앞). @p placeholderSRV 는 라이트가 없는 프레임에 t0 에 거는 자리표입니다. */
        void dispatch( IRHICommandList& cmd, RHIPipelineStateHandle pso, const GPULightBuffer& lightBuffer, RHIDescriptorIndex placeholderSRV );
        /** @brief 버퍼를 놓습니다. */
        void release( IRHIDevice* pDevice );

    private:
        RHIStructuredBuffer                      _voxelBuffer;                 ///< 볼륨들의 점유 복셀 단어(uint, 컴퓨트 t2)
        RHIStructuredBuffer                      _historyBuffer;               ///< 프로브마다 원시 SH9 계수 float4 일곱과 가시성 모먼트(`RealtimeProbeHistoryAllocator::kFloat4PerProbe`, 컴퓨트 u0, 프레임을 넘어 남는다)
        RHIStructuredBuffer                      _paramBuffer;                 ///< 입력 행(컴퓨트 t1)
        vector<float4>                           _listRowScratch;              ///< 입력 행 업로드 임시 버퍼
        vector<shared_ptr<const SceneVoxelGrid>> _listHeldGrid;                ///< 지금 복셀 버퍼에 올라간 격자들(포인터 비교가 믿을 수 있도록 붙들고 있는다)
        RealtimeProbeHistoryAllocator            _historyAllocator;            ///< 볼륨 정체마다의 기록 구간(드나든 볼륨만 새로 시작한다)
        uint32                                   _framesSinceInputChange{ 0 }; ///< 기록을 되돌리거나 복셀이 바뀐 뒤의 갱신 프레임 수(`getAccumulatedFrames`)
        uint32                                   _frameCounter{ 0 };
        uint32                                   _totalProbes{ 0 };
        uint8                                    _bActive{ SW_FALSE };
        uint8                                    _bReportedUnavailable{ SW_FALSE };
    };
} // namespace sw
