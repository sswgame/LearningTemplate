/**
 * @file RealtimeProbeUpdater.h
 * @brief 실시간 라이트 프로브 갱신의 렌더 스레드 쪽입니다 — 복셀 · 기록(history) · 입력 행 버퍼를 갖추고 컴퓨트(`probeupdate.hlsl`)를 기록합니다.
 * @details 흐름: 게임 스레드의 `collectSceneLights` 가 실시간 볼륨마다 `RealtimeProbeRequest`(볼륨 원소 사본, 갱신 값, 복셀 격자)를 만든다. 이 클래스는
 *          (1) `prepare` 가 기록 전에 버퍼를 갖춘다 - 복셀 단어(격자가 바뀐 프레임에만 올린다), 기록(볼륨 구성이 바뀌면 0 으로 되돌린다), 입력 행(프레임마다);
 *          (2) `dispatch` 가 라이트 업로드 뒤 · 조명 전에 컴퓨트 한 번을 돈다. 컴퓨트는 모든 실시간 프로브를 한 번에 돈다 - 프로브마다 이번 프레임의 조면 광선을 쏘아 기록을 갱신하고,
 *          기록을 소비 셰이더 배치로 바꿔 프로브 버퍼(t13)에 쓴다. 프로브 버퍼는 CPU 가 프레임마다 0 으로 올리므로 갱신하지 않는 프레임에도 전부 다시 쓴다.
 *          하늘은 `SkyGradient::evaluateRadiance` 를 SH9 로 투영해 입력 행에 싣는다(광선이 닿지 않는 방향의 복사휘도 — 베이커와 같은 규칙).
 *          입력 행 배치(`probeupdate.hlsl` 의 kRow*)는 `makeParamRows` 가 한 곳에서 짠다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/SphericalHarmonics.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"

#include "Engine/Environment/Sky/SkyGradient.h"
#include "Engine/Graphics/RHI/RHIStructuredBufferSlot.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Renderer/Lighting/GPULightBuffer.h"

namespace sw
{
    class IRHICommandList;
    class IRHIDevice;

    /** @brief 요청 목록을 컴퓨트 버퍼에 놓는 자리입니다(볼륨마다 복셀 단어 · 기록 · 스레드 범위의 시작). 순수 계산이라 테스트가 직접 본다. */
    struct RealtimeProbeLayout
    {
        vector<uint32> _listVoxelWordOffset; ///< 볼륨마다 복셀 단어 버퍼 안의 첫 단어
        vector<uint32> _listHistoryOffset;   ///< 볼륨마다 기록 버퍼 안의 첫 float4
        vector<uint32> _listFirstThread;     ///< 볼륨마다 첫 스레드 번호(= 앞 볼륨들의 프로브 수의 합)
        uint32         _totalVoxelWords{ 0 };
        uint32         _totalHistoryElements{ 0 };
        uint32         _totalProbes{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 실시간 프로브 갱신 컴퓨트의 렌더 스레드 소유자입니다. */
    class SW_API RealtimeProbeUpdater
    {
    public:
        static constexpr uint32 kThreadGroupSize = 64; ///< probeupdate.hlsl SW_NUMTHREADS 와 같다
        static constexpr uint32 kHeaderRowCount  = 8;  ///< 머리 행 1 + 하늘 SH 행 7 (probeupdate.hlsl kRowFirstVolume)
        static constexpr uint32 kVolumeRowCount  = 7;  ///< 볼륨마다 행 수 (probeupdate.hlsl kVolumeRowCount)
        static constexpr uint32 kSkySampleCount  = 64; ///< 하늘 SH 투영의 방향 수
        static constexpr uint32 kSkyRowCount     = 7;  ///< 하늘 SH 원시 계수 27 개가 float4 일곱(28 칸)에 들어간다

        /** @brief 요청들의 버퍼 배치를 계산합니다. */
        [[nodiscard]] static RealtimeProbeLayout planLayout( const vector<RealtimeProbeRequest>& listRequest );
        /**
         * @brief 하늘을 SH9 로 투영합니다(균일 구 방향 @p kSkySampleCount 개, 결정적). @p bHasSky 가 false 면 0 입니다 - 하늘이 없는 씬의 빈 방향은 어둡다(폴백 없음).
         */
        [[nodiscard]] static SH9Color projectSky( const SkyGradient& sky, bool bHasSky );
        /**
         * @brief 컴퓨트 입력 행을 짭니다(배치는 `probeupdate.hlsl` 머리 주석).
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
        /** @brief 컴퓨트를 기록합니다(라이트 업로드 뒤 · 조명 패스 앞). @p placeholderSrv 는 라이트가 없는 프레임에 t0 에 거는 자리표입니다. */
        void dispatch( IRHICommandList& cmd, RHIPipelineStateHandle pso, const GPULightBuffer& lightBuffer, RHIDescriptorIndex placeholderSrv );
        /** @brief 버퍼를 놓습니다. */
        void release( IRHIDevice* pDevice );

    private:
        RHIStructuredBufferSlot                  _voxelBuffer;    ///< 볼륨들의 점유 복셀 단어(uint, 컴퓨트 t2)
        RHIStructuredBufferSlot                  _historyBuffer;  ///< 프로브마다 원시 SH9 계수 float4 일곱(컴퓨트 u0, 프레임을 넘어 남는다)
        RHIStructuredBufferSlot                  _paramBuffer;    ///< 입력 행(컴퓨트 t1)
        vector<float4>                           _listRowScratch; ///< 입력 행 업로드 임시 버퍼
        vector<shared_ptr<const SceneVoxelGrid>> _listHeldGrid;   ///< 지금 복셀 버퍼에 올라간 격자들(포인터 비교가 믿을 수 있도록 붙들고 있는다)
        vector<uint64>                           _listHistoryKey; ///< 기록이 만들어진 볼륨 구성(볼륨 번호 · 프로브 수가 번갈아) - 달라지면 기록을 0 으로 되돌린다
        uint32                                   _framesSinceReset{ 0 };
        uint32                                   _frameCounter{ 0 };
        uint32                                   _totalProbes{ 0 };
        uint8                                    _bActive{ SW_FALSE };
        uint8                                    _bReportedUnavailable{ SW_FALSE };
    };
} // namespace sw
