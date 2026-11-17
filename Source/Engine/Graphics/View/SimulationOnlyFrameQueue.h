/**
 * @file Engine/Graphics/View/SimulationOnlyFrameQueue.h
 * @brief 그리지 않고 진행하는 고정 걸음(`SimulationOnlyFrameQueue`)입니다 — 시나리오의 고정 대기가 프레임마다 그리기를 건너뛰어 시간을 줄인다.
 * @details 고정 델타(`DeterministicRunPolicy::_fixedFrameDelta`) 실행 전용이다. 게임 스레드 프레임(입력 · 씬 틱 · UI · 관찰자 · 렌더 패킷 짓기)은 그대로 돌고,
 *          렌더 스레드는 그 패킷의 상태(텍스처 업로드 · GPU 장면 업로드)만 받고 패스를 기록하지 않으며 Present 하지 않는다 — 시뮬레이션 상태가 그리는 실행과 비트까지 같다
 *          (패킷 짓기가 게임 상태를 건드리는 몫 — LOD 뷰 · 그려진 화면비 · 컷 소비 — 도 같이 돈다). 언리얼 `-fixedtimestep` 헤드리스 틱 · 유니티 `Physics.Simulate` 와 같은 자리.
 *          렌더 프레임에 묶인 것(TAA · 모션 블러 · 노출 적응 · 깊이 히스토리 · 가림)은 다음에 그리는 프레임이 기록을 버린다(`FrameViewTarget::markFrameSkipped`).
 *          GPU 입자는 누적 단계 요청이라 그리는 프레임이 따라잡고, 다 따라잡을 때까지 캡처 준비(`CaptureReadiness`)가 남은 일로 센다 — 따라잡기 전 그림을 찍지 않는다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Subsystem/ISubsystem.h"
#include "Core/Subsystem/ServiceSlot.h"

#include "Engine/Presentation/FrameDrawMode.h"

namespace sw
{
    /**
     * @class SimulationOnlyFrameQueue
     * @brief 그리지 않고 진행할 프레임 수 · 따라잡기 상태 · 누적 통계입니다. 게임 스레드가 요청하고 프레임마다 고르며, 렌더 스레드가 따라잡기를 알린다.
     */
    class SW_API SimulationOnlyFrameQueue final : public ISubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "SimulationOnlyFrameQueue";

        SimulationOnlyFrameQueue()                                             = default;
        ~SimulationOnlyFrameQueue() override                                   = default;
        SimulationOnlyFrameQueue( const SimulationOnlyFrameQueue& )            = delete;
        SimulationOnlyFrameQueue& operator=( const SimulationOnlyFrameQueue& ) = delete;

        SubsystemInitResult        initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                       shutdownSubsystem() override {}
        const utf8*                getSubsystemName() const override { return kSubsystemName; }
        static constexpr MemoryTag kSubsystemMemoryTag = MemoryTag::RenderCPU;

        /**
         * @brief (게임 스레드) 다음 @p frameCount 프레임을 그리지 않고 진행합니다. 앞 요청이 남았으면 더한다.
         * @param fixedFrameDelta 지금 실행의 프레임 고정 시간(`DeterministicRunPolicy::_fixedFrameDelta`)입니다. 0 이하면 벽시계 실행이라 거절하고 false 다 —
         *        벽시계 실행에서 그리기를 건너뛰면 프레임 시간이 바뀌어 결과가 달라진다(부르는 쪽이 오류로 멈춘다).
         */
        [[nodiscard]] bool request( uint32 frameCount, float32 fixedFrameDelta );
        /**
         * @brief (게임 스레드, `Engine::update` 가 프레임마다 한 번) 이번 프레임을 그릴지 정하고, 그리지 않으면 남은 수를 줄이고 건너뛴 번호를 올립니다.
         * @param fixedFrameDelta 지금 실행의 프레임 고정 시간입니다. 남은 요청이 있는데 0 이하면(재생 중 정책이 풀렸다) 남은 요청을 버리고 그린다.
         */
        FrameDrawMode beginFrame( float32 fixedFrameDelta );
        /** @brief (게임 스레드) 마지막으로 그리지 않은 프레임의 번호입니다(패킷에 싣는다 — `RenderFramePacket::_simulationOnlySerial`). 0 이면 그런 프레임이 없었다. */
        uint64 getLastSkippedSerial() const { return _skippedSerial.load( std::memory_order_acquire ); }
        /**
         * @brief (렌더 스레드) 그린 프레임 하나가 끝났습니다 — 그 패킷이 실은 건너뛴 번호 @p packetSkippedSerial 까지를, 남은 따라잡기(@p pendingCatchUpSteps — GPU 입자의 남은 단계)가 0 이면 다 따라잡은 것으로 적습니다.
         */
        void reportDrawnFrame( uint64 packetSkippedSerial, uint64 pendingCatchUpSteps );
        /** @brief (어느 스레드든) 건너뛴 프레임 뒤의 따라잡기가 남았으면 true 입니다 — 캡처 준비가 남은 일로 센다(`CaptureReadiness::countPendingRenderJobs`). */
        bool isCatchUpPending() const { return _completedSerial.load( std::memory_order_acquire ) < _skippedSerial.load( std::memory_order_acquire ); }
        /** @brief (게임 스레드) 아직 그리지 않고 진행할 프레임 수입니다. */
        uint32 getRemainingCount() const { return _remainingCount; }
        /** @brief (게임 스레드) 지난 `beginFrame` 이 그리지 않는 프레임이었으면 true 입니다(시나리오 시간 기록이 프레임 종류를 가른다). */
        bool wasLastFrameSimulationOnly() const { return _bLastSimulationOnly != 0; }

    private:
        uint32         _remainingCount{ 0 };
        uint8          _bLastSimulationOnly{ 0 };
        atomic<uint64> _skippedSerial{ 0 };   ///< 그리지 않은 프레임 수(누적 — 번호로 쓴다)
        atomic<uint64> _completedSerial{ 0 }; ///< 다 따라잡은 마지막 건너뛴 번호

        SW_SERVICE_SLOT( SimulationOnlyFrameQueue, Optional );
    };
} // namespace sw
