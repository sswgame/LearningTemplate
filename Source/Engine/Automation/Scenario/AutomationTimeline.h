/**
 * @file AutomationTimeline.h
 * @brief 시나리오 한 판의 벽시계가 어디에 쓰였는지 프레임 종류별로 모읍니다 — 고정 프레임 대기가 실제로 얼마나 드는지 보고(`timeline`)로 남깁니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @enum AutomationFrameKind
     * @brief 실행기가 본 프레임 하나가 무엇을 하느라 지났는지입니다. 프레임마다 정확히 하나로 센다.
     */
    enum class AutomationFrameKind : uint8
    {
        Start,          ///< 시나리오가 시작하기 전(시작 조건 · 로딩 화면 · 감시 울타리를 기다리는 프레임과 기동 시간)
        BackgroundHold, ///< 앞 실행이 남긴 백그라운드 작업(저장 검증 · 색인 …)이 시계를 붙잡은 프레임
        ConditionHold,  ///< `<WaitUntil>` · `<Screenshot>` 같은 조건 대기가 시계를 붙잡은 프레임(조건이 참이 될 때까지)
        StepRan,        ///< 단계가 하나 이상 돈 프레임
        ScheduledWait,  ///< 돌 단계 없이 `<Wait frames>` 만 흘린 프레임 — 고정 프레임 대기다
    };

    /** @brief 프레임 종류 하나의 합계입니다. */
    struct AutomationTimelineSlice
    {
        uint32  _frameCount{ 0 };
        float64 _seconds{ 0.0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 단계 앞에서 기다린(고정 대기 + 조건 대기) 한 덩어리입니다 — 가장 긴 몇 개만 남긴다. */
    struct AutomationSlowWait
    {
        string  _stepKind;        ///< 대기가 끝나고 처음 돈 단계의 이름
        uint32  _stepNumber{ 0 }; ///< 그 단계의 파일 순번
        uint32  _frameIndex{ 0 }; ///< 그 단계의 시나리오 프레임
        uint32  _waitedFrameCount{ 0 };
        float64 _seconds{ 0.0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class AutomationTimeline
     * @brief 프레임마다 걸린 벽시계를 종류별로 더하고, 단계 앞 대기 가운데 가장 긴 것을 남깁니다(`AutomationRunner` 의 값 멤버).
     * @details 같은 단계 목록을 가진 두 시나리오라도 프레임 비용은 백엔드 · 에디터 여부로 다르다 — 그래서 파일의 `<At>` 간격(정적 어림)이 아니라 잰 값을 남긴다.
     *          `ScheduledWait` 와 `ConditionHold` 가 "단계 없이 기다린" 시간이고, 둘을 더한 것이 단계 앞 대기(`closeWait` 가 가장 긴 것을 남긴다).
     */
    class SW_API AutomationTimeline
    {
    public:
        static constexpr uint32 kFrameKindCount  = 5;
        static constexpr uint32 kSlowestWaitKeep = 5;

        AutomationTimeline();

        /** @brief 프레임 하나가 @p kind 로 @p seconds 걸렸음을 더합니다. 대기 프레임(`ScheduledWait` · `ConditionHold`)은 아직 닫지 않은 대기에도 쌓인다. */
        void recordFrame( AutomationFrameKind kind, float64 seconds );
        /**
         * @brief 단계 하나가 돌기 시작했음을 알려 지금까지 쌓인 대기를 닫습니다. 쌓인 것이 없으면 아무것도 하지 않고, 있으면 가장 긴 몇 개 안에 들 때만 남깁니다.
         * @param stepKind 단계 이름
         * @param stepNumber 단계의 파일 순번
         * @param frameIndex 단계가 도는 시나리오 프레임
         */
        void closeWait( const string& stepKind, uint32 stepNumber, uint32 frameIndex );

        [[nodiscard]] const AutomationTimelineSlice& getSlice( AutomationFrameKind kind ) const { return _arrSlice[static_cast<uint32>( kind )]; }
        /** @brief 단계 앞 대기, 긴 순입니다(최대 `kSlowestWaitKeep` 개). */
        [[nodiscard]] const vector<AutomationSlowWait>& getSlowestWaits() const { return _listSlowestWait; }

        /** @brief 보고 JSON 칸 이름의 앞머리입니다(`held` → `heldFrames` · `heldMilliseconds`). */
        [[nodiscard]] static const utf8* getKindKey( AutomationFrameKind kind );

    private:
        AutomationTimelineSlice    _arrSlice[kFrameKindCount];
        vector<AutomationSlowWait> _listSlowestWait;
        uint32                     _pendingWaitFrameCount;
        float64                    _pendingWaitSeconds;
    };
} // namespace sw
