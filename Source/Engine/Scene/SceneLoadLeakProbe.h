/**
 * @file SceneLoadLeakProbe.h
 * @brief 진짜 씬 로드를 되풀이해 누수 곡선(`MemoryProfiler::judgeLeakCurve`)을 재는 감시 값입니다(누수 탐지 L3).
 * @details 언리얼의 `memreport` 를 로드 전후로 되풀이해 보는 일, 유니티 메모리 프로파일러의 스냅샷 비교에 해당하는 일을 한 명령으로 묶는다.
 *          한 회차는 "창 열기 → 씬 로드 → 빈 씬으로 교체(로드한 씬 언로드) → 몇 프레임 기다림 → 창 닫기" 이고, 창이 닫힐 때 아직 살아 있는 블록이 그 회차의 남은 양이다.
 *          빈 씬 자체도 창 안에서 태어나므로 로드 없이 빈 씬만 만든 **대조 회차**를 먼저 재어 그 양을 뺀다. 점은 대조를 뺀 남은 양의 누적 합이라 회차마다 같은 양이 새면
 *          기울기가 그 양으로 나온다(회차 하나씩만 보면 일정한 누수가 평평하게 보인다). 로드가 프레임을 돌아야 끝나므로(스트리밍 · 업로드) 프레임마다 `update` 가 상태를 한 칸씩 옮긴다.
 *          기준선 창(`captureMemoryLeakBaseline`)이 열려 있으면 닫고 시작하며, 끝나면 `TagTotals` 창을 다시 열어 종료 보고가 비지 않게 한다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Diagnostics/MemoryProfiler.h"
#include "Core/Task/TaskFuture.h"

namespace sw
{
    class SceneManager;
    class Scene;

    /**
     * @class SceneLoadLeakProbe
     * @brief 씬 로드 곡선 감시 값입니다. `SceneManager` 가 하나 들고 프레임마다 `update` 를 부릅니다(메인 스레드).
     */
    class SW_API SceneLoadLeakProbe
    {
    public:
        static constexpr uint32 kSettleFrames = 3; ///< 빈 씬으로 바꾼 뒤 지연 해제(스트리밍 · 업로드 완료)를 기다리는 프레임 수

        SceneLoadLeakProbe();

        /**
         * @brief 측정을 시작합니다. 워밍업(`LeakCurveCriteria::kDefaultWarmupRounds`)과 @p measuredRounds 회의 로드를 되풀이합니다.
         * @return 이미 도는 중이거나 프로파일러 · 경로가 없으면 false 입니다(이유는 로그).
         */
        [[nodiscard]] bool start( string_view scenePath, uint32 measuredRounds );
        /** @brief 측정이 도는 중이면 true 입니다. */
        bool isRunning() const { return _phase != Phase::Idle; }
        /** @brief 프레임마다 한 칸 진행합니다. 끝나면 판정을 로그로 남기고 `TagTotals` 창을 다시 엽니다. */
        void update( SceneManager& manager );
        /** @brief 마지막으로 끝난 측정의 판정입니다(`_bValid` 가 false 이면 아직 없다). */
        const LeakCurveVerdict& getVerdict() const { return _verdict; }

    private:
        enum class Phase : uint8
        {
            Idle,
            Loading,  ///< 씬 로드 요청이 끝나기를 기다린다(대조 회차는 건너뛴다)
            Settling, ///< 빈 씬으로 바꾼 뒤 @c kSettleFrames 프레임을 기다린다
        };

        /** @brief 회차 하나를 시작합니다 — 창을 열고(대조 회차가 아니면) 로드를 요청합니다. */
        void beginRound( SceneManager& manager );
        /** @brief 회차 하나를 마칩니다 — 창을 닫아 남은 양을 점으로 모으고 다음 회차나 끝으로 갑니다. */
        void endRound();
        /** @brief 모든 회차가 끝났다 — 판정하고 로그를 남기고 `TagTotals` 창을 다시 엽니다. */
        void finish();

        Phase                  _phase;
        string                 _scenePath;
        uint32                 _totalRounds;   ///< 워밍업 + 측정 회차 수(대조 회차는 따로)
        uint32                 _round;         ///< 지금 회차(0 이 대조 회차, 1 부터 로드 회차)
        uint32                 _settleFrames;  ///< `Settling` 에서 지난 프레임 수
        uint64                 _controlBytes;  ///< 대조 회차에서 남은 바이트(빈 씬 몫)
        uint64                 _controlBlocks; ///< 대조 회차에서 남은 블록 수
        uint64                 _sumBytes;      ///< 대조를 뺀 남은 바이트의 누적 합
        uint64                 _sumBlocks;     ///< 대조를 뺀 남은 블록 수의 누적 합
        TaskFuture<Scene*>     _loadFuture;
        vector<LeakCurvePoint> _listPoint;
        LeakCurveVerdict       _verdict;
    };
} // namespace sw
