/**
 * @file RealtimeProbeUpdater.h
 * @brief 실시간 라이트 프로브 갱신의 렌더 스레드 쪽입니다 — 복셀 · 기록(history) · 입력 행 버퍼를 갖추고 컴퓨트(`probe_update.hlsl`)를 기록합니다.
 * @details 흐름: 게임 스레드의 `collectSceneLights` 가 실시간 볼륨마다 `RealtimeProbeRequest`(볼륨 원소 사본, 갱신 값, 복셀 격자)를 만든다. 이 클래스는
 *          (1) `prepare` 가 기록 전에 버퍼를 갖춘다 - 복셀 칠하기(장면이 바뀐 프레임에 바뀐 구간만 — `SceneVoxelizer::planPaint`), 기록(볼륨 정체마다 구간 — 새로 든 볼륨만 0 에서 쌓는다),
 *          입력 행(프레임마다);
 *          (2) `recordUploads` 가 프레임 업로드 단계에서 칠할 메시의 위치 흐름(렌더 프록시 버퍼)을 위치 버퍼로 GPU 복사하고, `dispatchVoxelClear` · `dispatchVoxelPaint` 가
 *          `scene_voxelize.hlsl` 로 칸을 지우고 칠한다 — CPU 정점을 읽지 않는다(R-15);
 *          (3) `dispatch` 가 라이트 업로드 뒤 · 조명 전에 컴퓨트 한 번을 돈다. 컴퓨트는 모든 실시간 프로브를 한 번에 돈다 - 프로브마다 이번 프레임의 조면 광선을 쏘아 기록을 갱신하고,
 *          기록을 소비 셰이더 배치로 바꿔 프로브 버퍼(t13)에 쓴다. 프로브 버퍼는 CPU 가 프레임마다 0 으로 올리므로 갱신하지 않는 프레임에도 전부 다시 쓴다.
 *          하늘은 `SkyGradient::evaluateRadiance` 를 SH9 로 투영해 입력 행에 싣는다(광선이 닿지 않는 방향의 복사휘도 — 베이커와 같은 규칙).
 *          입력 행 배치(`probe_update.hlsl` 의 kRow*)는 `makeParamRows` 가 한 곳에서 짠다.
 */
#pragma once
#include "Core/Container/SpanAllocator.h"
#include "Core/Container/span.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
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
    struct ComputePipeline;
    struct SceneVoxelPaintPlan;
    struct SkyGradient;
    class IRHICommandList;
    class IRHIPassCommandList;
    class IRHIDevice;
    class Mesh;

    /** @brief 요청 목록을 컴퓨트 버퍼에 놓는 자리입니다(볼륨마다 복셀 단어 · 기록 · 스레드 범위의 시작). 순수 계산이라 테스트가 직접 본다. */
    struct RealtimeProbeLayout
    {
        vector<uint32> _listVoxelWordOffset;       ///< 볼륨마다 복셀 단어 버퍼 안의 첫 단어
        vector<uint32> _listEmissionWordOffset;    ///< 볼륨마다 발광 칸(순위) 단어의 첫 단어 — 점유 단어 바로 뒤. 발광하는 메시가 없으면 `SceneVoxelizer::kNoEmission`
        vector<uint32> _listStaticWordOffset;      ///< 볼륨마다 정적 사본의 점유 첫 단어(읽는 자리들 뒤). 움직이는 층이 없는 장면이면 `SceneVoxelizer::kNoStaticCopy`
        vector<uint32> _listStaticEmissionOffset;  ///< 볼륨마다 정적 사본의 발광 첫 단어. 발광하는 메시가 없거나 정적 사본이 없으면 `SceneVoxelizer::kNoEmission`
        vector<uint32> _listPaletteRow;            ///< 볼륨마다 발광 목록(`SceneVoxelScene::_listEmissionPalette`)의 첫 입력 행 — 볼륨 행들 뒤
        vector<uint32> _listHistoryOffset;         ///< 볼륨마다 기록 버퍼 안의 첫 float4(`RealtimeProbeHistoryAllocator` 가 볼륨 정체로 매긴 구간 — 이어지지 않을 수 있다)
        vector<uint32> _listFirstThread;           ///< 볼륨마다 첫 스레드 번호(= 앞 볼륨들의 프로브 수의 합)
        uint32         _totalVoxelWords{ 0 };      ///< 점유 단어와 발광 단어의 합(정적 사본 포함)
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
         * @details 늘어난 용량과 새 구간은 그 프레임을 갱신했다고 알릴 때(`advanceFrame`)까지 **남아 있다** — 부르는 쪽이 중간에 그만두면(버퍼 할당 실패) 다음 `assign` 이 다시 알린다.
         *          한 번만 알리면 0 으로 채우지 않은 구간과 옛 크기 버퍼를 다음 프레임이 그대로 쓴다.
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
        /** @brief 이번 프레임을 갱신했음(새 구간을 0 으로 채우고 버퍼를 갖췄음)을 적습니다 — 서 있는 볼륨마다 누적 프레임이 하나 는다. */
        void advanceFrame();
        /** @brief 모든 볼륨이 다음 `assign` 에서 새로 시작하게 합니다(버퍼를 놓거나 다시 만들지 못했을 때). */
        void clear();

    private:
        /** @brief 볼륨 하나의 기록 구간입니다. */
        struct HistorySlot
        {
            uint32 _offset{ 0 };
            uint32 _count{ 0 };
            uint32 _framesSinceReset{ 0 }; ///< 0 이면 아직 한 프레임도 갱신하지 않은 구간이다(`isFresh`)
        };

    private:
        unordered_map<uint64, HistorySlot> _mapSlot;                  ///< 볼륨 정체 → 구간
        SpanAllocator                      _span;                     ///< 기록 버퍼의 빈 구간
        vector<uint64>                     _listActiveID;             ///< 이번 프레임 요청의 정체(차례대로)
        vector<uint32>                     _listOffset;               ///< 요청마다 구간 시작
        vector<uint32>                     _listFramesSinceReset;     ///< 요청마다 누적 프레임
        vector<uint8>                      _listFresh;                ///< 요청마다 새로 시작하는지
        uint8                              _bGrowPending{ SW_FALSE }; ///< 용량이 늘었고 아직 갱신한 프레임이 없다(버퍼를 다시 만들어야 한다)
    };
} // namespace sw

namespace sw
{
    /**
     * @struct RealtimeProbeAccumulation
     * @brief 프로브 기록이 지금 입력에 수렴한 프레임 수입니다 — 사진 앞 정착 조건 `RenderRealtimeGI` 의 진척. 갱신한 프레임마다 한 번 `advance` 를 부릅니다(순수 값 — 단위 테스트가 바로 부른다).
     * @details 되돌림(기록이 0 에서 시작)은 처음부터 센다. 입력의 변화(가리개 · 발광 · 빛 · 하늘 — `RealtimeProbeUpdater::prepare`)는 두 때를 가른다.
     *          - 변화가 이어지는 동안: 이어진 변화가 시작한 뒤의 프레임 수다. 멈추지 않는 변화(도는 기믹 · 깜빡이는 등 · 흐르는 시각)는 기록이 같은 시차로 따라가는 정상 상태에 이르면 다 된 것이다 —
     *            끝나지 않는 변화가 사진을 막지 않는다(카메라만 보는 TAA 정지 프레임과 같은 까닭). 고정 프레임 시간의 결정적 실행에서는 같은 프레임이면 같은 그림이다.
     *          - 멈춘 뒤: 마지막 변화 뒤의 프레임 수다. 막 멈춘 문 · 막 켠 등의 시차가 가라앉을 때까지 기다린다.
     *
     *          이어진 변화인지는 정한 값이 아니라 본 틈으로 정한다. 정착하기 전에 다시 바뀌면, 지금까지 본 가장 긴 틈 이하일 때 이어진 것이고 더 길면 그 틈을 새로 배워 처음부터 센다(한 번뿐인 다음 사건은
     *          제 시차를 처음부터 기다린다). 조용한 프레임은 이어진 변화 **안에서** 본 틈 이하일 때만 아직 이어진 것이다 — 되풀이하는 변화는 틈에서도 정착해 있고, 멈춘 것은 그 틈을 넘는 프레임에 드러난다.
     *          틈은 정착에 걸리는 프레임보다 짧은 것만 배우고(정착할 만큼 조용했으면 잊는다) 처음부터 세는 일은 틈이 늘 때뿐이라, 되풀이하는 변화는 주기와 무관하게 끝내 정착한다. 시한 · 고정 대기는 없다.
     */
    struct SW_API RealtimeProbeAccumulation
    {
        uint32 _framesSinceReset{ 0 };                                 ///< 되돌린 뒤의 갱신 프레임 수
        uint32 _framesSinceChange{ constant::invalid_index::kUint32 }; ///< 마지막 변화 뒤의 갱신 프레임 수(바뀐 프레임이 1). 바뀐 적이 없으면 무한
        uint32 _framesSinceStart{ 0 };                                 ///< 이어진 변화가 시작한 뒤의 갱신 프레임 수
        uint32 _longestGap{ 1 };                                       ///< 마지막으로 정착한 뒤 본 가장 긴 틈(프레임마다 바뀌면 1) — 이보다 긴 틈 뒤의 변화는 처음부터 센다
        uint32 _longestInnerGap{ 1 };                                  ///< 지금 이어진 변화 안에서 본 가장 긴 틈 — 이보다 조용하면 멈춘 것이다

        /**
         * @brief 갱신한 프레임 하나를 셉니다.
         * @param bReset 기록이 0 에서 시작한다
         * @param bChanged 앞 프레임과 입력이 다르다
         * @param bStable 이 프레임 앞까지 조용했던 프레임(`_framesSinceChange`)만으로 이미 정착했다 — 이때의 변화는 앞 변화와 이어지지 않는다
         */
        void advance( bool bReset, bool bChanged, bool bStable );
        /** @brief 지금 입력에 수렴한 프레임 수입니다. */
        [[nodiscard]] uint32 getAccumulatedFrames() const;
    };

    /** @brief 실시간 프로브 갱신 컴퓨트의 렌더 스레드 소유자입니다. */
    class SW_API RealtimeProbeUpdater
    {
    public:
        static constexpr uint32 kHeaderRowCount = 8;  ///< 머리 행 1 + 하늘 SH 행 7 (probe_update.hlsl kRowFirstVolume)
        static constexpr uint32 kVolumeRowCount = 8;  ///< 볼륨마다 행 수 (probe_update.hlsl kVolumeRowCount)
        static constexpr uint32 kSkySampleCount = 64; ///< 하늘 SH 투영의 방향 수
        static constexpr uint32 kSkyRowCount    = 7;  ///< 하늘 SH 원시 계수 27 개가 float4 일곱(28 칸)에 들어간다

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
         * @brief 광선이 읽는 입력(하늘 SH · 볼륨의 격자와 배율 · 닿은 면을 비추는 앞쪽 빛)을 행으로 폅니다 — 앞 프레임의 행과 다르면 입력이 바뀐 것이다(복셀과 발광 목록은 칠할 일이 알린다).
         * @details 셰이더가 읽지 않는 값(버퍼 안의 자리 · 그림자 칸 · 2D 빛)과 프레임 번호는 싣지 않는다 — 그 변화는 기록이 향하는 값을 바꾸지 않는다.
         */
        [[nodiscard]] static vector<float4> makeInputRows( const vector<RealtimeProbeRequest>& listRequest, const SH9Color& sky, vector_reference<const GPULight> lights );

        /**
         * @brief 기록 전에 부릅니다 - 버퍼를 갖추고 이번 프레임의 입력을 올립니다. 요청이 없거나 프로브 버퍼를 쓸 수 없으면 `isActive` 가 false 입니다.
         * @details 못 켜는 이유(컴퓨트 PSO 없음 · 프로브 버퍼 UAV 없음 · 버퍼 할당 실패)는 한 번만 오류로 남긴다 - 실시간 볼륨은 그동안 검게 남는다(베이크로 대신하지 않는다).
         * @param bCreateMissingMeshes 업로드 큐가 넘기기 전에 메시를 만들지 않는 구성이면 true 입니다 — 칠할 메시의 GPU 버퍼가 없으면 여기서 만든다(만드는 쪽은 하나다, `GPUUploadQueue::shouldCreateMeshes`)
         * @param stableError 정착으로 보는 오차(`RenderStabilitySample::readMaxGIError` — 정착 조건과 같은 문턱). 정착을 보는 시나리오가 없으면 0 이다
         */
        void prepare( IRHIDevice* pDevice, const vector<RealtimeProbeRequest>& listRequest, const GPULightBuffer& lightBuffer, const SkyGradient& sky, bool bHasSky, bool bPipelineReady,
                      bool bCreateMissingMeshes, float32 stableError );
        /** @brief 프레임 업로드 단계에서 부릅니다 - `prepare` 가 쌓은 위치 흐름의 GPU 복사(렌더 프록시 → 위치 버퍼)를 전이와 함께 기록합니다. 쌓인 것이 없으면 아무것도 하지 않습니다. */
        void recordUploads( IRHICommandList& cmd );
        /** @brief 이번 프레임에 복셀을 다시 칠하는지입니다(바뀐 구간이 있다). */
        [[nodiscard]] bool hasPendingPaint() const { return _bPaintPending == SW_TRUE && _bActive == SW_TRUE; }
        /** @brief 복셀 버퍼(점유 비트 · 발광 순위)입니다. 프레임 그래프 표가 칠하는 프레임에 등록한다(`SceneVoxels`). */
        const RHIStructuredBuffer& getVoxelBuffer() const { return _voxelBuffer; }
        /** @brief 복셀 칠하기의 디스패치 수입니다(지우기 → 칠하기 → 되돌리기 → 움직이는 것 칠하기 — `SceneVoxelPaintPlan`). */
        static constexpr uint32 kVoxelDispatchCount = 4;
        /** @brief @p dispatchIndex 번째 디스패치에 할 일이 있는지입니다. 없으면 배리어도 걸지 않는다. */
        [[nodiscard]] bool hasVoxelDispatch( uint32 dispatchIndex ) const { return _arrVoxelThreadCount[dispatchIndex] != 0; }
        /** @brief @p dispatchIndex 번째 디스패치를 기록합니다(`scene_voxelize.hlsl`, 앞 디스패치와 사이에 UAV 배리어). */
        void dispatchVoxelPass( IRHIPassCommandList& cmd, const ComputePipeline& pipeline, uint32 dispatchIndex ) const;
        /** @brief 디스패치를 모두 기록했음을 적습니다 — 이 장면이 칠해진 것이다. */
        void finishVoxelPaint() { _bPaintPending = SW_FALSE; }
        /** @brief 이번 프레임에 컴퓨트가 도는지입니다. */
        [[nodiscard]] bool isActive() const { return _bActive == SW_TRUE; }
        /** @brief 프로브 기록 버퍼(컴퓨트 u0 — 프레임을 넘어 남는다)입니다. 프레임 그래프 표가 크기와 함께 등록한다(`ProbeHistory`). */
        const RHIStructuredBuffer& getHistoryBuffer() const { return _historyBuffer; }
        /**
         * @brief 프로브 기록이 지금 입력(가리개 · 발광 · 빛 · 하늘 — `RealtimeProbeAccumulation`)에 수렴한 프레임 수입니다(그린 프레임만 센다). 이번 프레임에 돌지 않으면 0 입니다 — 렌더 정착 조건 `RenderRealtimeGI` 의 진척.
         * @details 입력이 바뀌어도 기록은 0 으로 돌리지 않고 섞어 따라간다 — 그래서 섞는 비율의 초기 가속(`_framesSinceReset`)과 따로 센다.
         */
        [[nodiscard]] uint32 getAccumulatedFrames() const { return _bActive == SW_TRUE ? _accumulation.getAccumulatedFrames() : 0u; }
        /** @brief 기록에 남은 옛 값의 몫입니다(`computeError` — 이번 프레임까지). 이번 프레임에 돌지 않으면 0 입니다 — 감시 값 `Render.GIError`. */
        [[nodiscard]] float32 getError() const { return _bActive == SW_TRUE ? _error : 0.0f; }
        /**
         * @brief 입력이 바뀐 뒤 @p accumulatedFrames 프레임을 갱신했을 때 기록에 남은 옛 값의 몫입니다 — 볼륨마다 (1 − 섞는 비율)^(갱신 횟수), 그 가운데 가장 큰 값. 요청이 없으면 0 입니다.
         * @details 갱신 횟수는 프레임 수 ÷ 갱신 주기(내림)다. 새로 시작한 볼륨은 실제로 더 빨리 채워지므로(섞는 비율의 초기 가속) 상한이다.
         */
        [[nodiscard]] static float32 computeError( const vector<RealtimeProbeRequest>& listRequest, uint32 accumulatedFrames );
        /** @brief 컴퓨트를 기록합니다(라이트 업로드 뒤 · 조명 패스 앞). @p placeholderSRV 는 라이트가 없는 프레임에 t0 에 거는 자리표입니다. */
        void dispatch( IRHIPassCommandList& cmd, const ComputePipeline& pipeline, const GPULightBuffer& lightBuffer, RHIBufferSRVIndex placeholderSRV );
        /** @brief 버퍼를 놓습니다. */
        void release( IRHIDevice* pDevice );

    private:
        /** @brief 복셀 버퍼의 볼륨 자리 하나와 거기 칠한 장면입니다(포인터 비교가 믿을 수 있도록 장면을 붙들고 있는다). */
        struct PaintedVolume
        {
            uint64                            _volumeID{ 0 };
            uint32                            _wordOffset{ 0 };
            uint32                            _emissionOffset{ 0 };
            uint32                            _staticWordOffset{ 0 };
            uint32                            _staticEmissionOffset{ 0 };
            shared_ptr<const SceneVoxelScene> _pScene{};
        };
        /** @brief 위치 흐름의 GPU 복사 하나입니다(렌더 프록시 정점 버퍼의 앞 구간 → 위치 버퍼). 기록할 때까지 메시를 쥔다. */
        struct PendingPositionCopy
        {
            shared_ptr<Mesh> _mesh{};
            uint64           _destinationOffset{ 0 };
            uint64           _sizeBytes{ 0 };
        };

        /** @brief 바뀐 장면을 칠할 일을 짜고 버퍼를 갖춥니다. 버퍼를 만들지 못하면 오류를 남기고 false 입니다. @p outChanged 는 다시 칠할 것(가리개 · 발광의 변화)이 있는지입니다. */
        [[nodiscard]] bool prepareVoxelPaint( IRHIDevice* pDevice, const vector<RealtimeProbeRequest>& listRequest, const RealtimeProbeLayout& layout, bool bCreateMissingMeshes, bool& outChanged );
        /** @brief 칠할 일의 입력 행 · 인덱스를 올리고 위치 복사를 쌓습니다. 버퍼를 만들지 못하면 false 입니다. */
        [[nodiscard]] bool uploadPaint( IRHIDevice* pDevice, const SceneVoxelPaintPlan& plan, bool bCreateMissingMeshes );
        /** @brief 칠하기 디스패치 하나를 기록합니다(@p jobBuffer 의 입력 행, 스레드 @p threadCount 개). */
        void dispatchVoxelJobs( IRHIPassCommandList& cmd, const ComputePipeline& pipeline, const RHIStructuredBuffer& jobBuffer, uint32 threadCount ) const;

    private:
        RHIStructuredBuffer           _voxelBuffer;                            ///< 볼륨들의 점유 비트 · 발광 순위 단어(uint, 프로브 갱신 t2, 칠하기 u0, 프레임을 넘어 남는다)
        RHIStructuredBuffer           _arrVoxelJobBuffer[kVoxelDispatchCount]; ///< 디스패치마다의 입력 행(칠하기 t0)
        RHIStructuredBuffer           _voxelPositionBuffer;                    ///< 칠할 메시들의 LOD 0 위치(float 셋씩, 칠하기 t1) — 렌더 프록시 위치 흐름의 GPU 복사
        RHIStructuredBuffer           _voxelIndexBuffer;                       ///< 칠할 메시들의 LOD 0 인덱스(칠하기 t2)
        RHIStructuredBuffer           _historyBuffer;                          ///< 프로브마다 원시 SH9 계수 float4 일곱과 가시성 모먼트(`RealtimeProbeHistoryAllocator::kFloat4PerProbe`, 컴퓨트 u0, 프레임을 넘어 남는다)
        RHIStructuredBuffer           _paramBuffer;                            ///< 입력 행(컴퓨트 t1)
        vector<float4>                _listRowScratch;                         ///< 입력 행 업로드 임시 버퍼
        vector<float4>                _listInputRow;                           ///< 앞 프레임의 입력(`makeInputRows`) — 이번 것과 견준다
        vector<PaintedVolume>         _listPainted;                            ///< 지금 복셀 버퍼에 칠해진 볼륨들(요청 차례)
        vector<PendingPositionCopy>   _listPendingCopy;                        ///< 이번 프레임 업로드 단계가 기록할 위치 복사
        vector<uint32>                _listIndexScratch;                       ///< 인덱스 업로드 임시 버퍼
        vector<float32>               _listPositionScratch;                    ///< GPU 버퍼가 없는 메시(동적 메시)의 위치 업로드 임시 버퍼
        RealtimeProbeHistoryAllocator _historyAllocator;                       ///< 볼륨 정체마다의 기록 구간(드나든 볼륨만 새로 시작한다)
        RealtimeProbeAccumulation     _accumulation;                           ///< 정착 진척(`getAccumulatedFrames`)
        uint32                        _frameCounter{ 0 };
        float32                       _error{ 0.0f }; ///< `getError`
        uint32                        _totalProbes{ 0 };
        uint32                        _arrVoxelThreadCount[kVoxelDispatchCount]{};
        uint8                         _bActive{ SW_FALSE };
        uint8                         _bReportedUnavailable{ SW_FALSE };
        uint8                         _bPaintPending{ SW_FALSE }; ///< 짠 칠하기를 아직 기록하지 않았다
    };
} // namespace sw
