/**
 * @file RoundSeries.h
 * @brief 여러 라운드로 한 판을 가르는 규칙 — 라운드 순위에 따른 점수(선승은 1 위 1 점), 먼저 목표 점수에 닿은 쪽 우승, 동점 규칙, 라운드 시간 · 라운드 사이 대기입니다.
 * @details 격투의 2 선승(라운드 승 = 1 위 1 점, 둘이 함께 닿으면 무승부)과 파티의 순위 점수 묶음(3 · 2 · 1 · 0 점, 먼저 5 점, 같으면 서든 데스)이 설정만 바꿔 씁니다.
 *          시간은 정수 걸음(고정 스텝 · 프레임)으로 셉니다 — 롤백 · 락스텝 키트가 그대로 결정적입니다. 한 라운드 안의 점수 · 부활 · 시간 제한은 `MatchState` 의 일입니다
 *          (파티는 라운드마다 다른 시간을 그쪽에 준다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class BitReader;
    class BitWriter;

    /** @brief 라운드 묶음의 단계입니다. 상태 바이트에 번호로 실리므로 뒤에만 덧붙입니다. */
    enum class RoundSeriesPhase : uint8
    {
        Waiting = 0, ///< `start` 전
        RoundActive,
        Intermission, ///< 라운드가 끝나고 다음 라운드를 기다린다
        Finished
    };

    /** @brief 목표에 닿은 참가자 둘 이상이 총점까지 같을 때입니다. */
    enum class RoundSeriesTieRule : uint8
    {
        Draw = 0,   ///< 무승부로 끝난다(격투 — 마지막 라운드가 무승부)
        SuddenDeath ///< 아무도 이기지 않고 다음 라운드로 간다(파티)
    };

    /** @brief `advanceTick` 이 알리는 것입니다. */
    enum class RoundSeriesTick : uint8
    {
        None = 0,
        TimeUp,      ///< 라운드 시간이 다 됐다 — 키트가 결과를 정해 `reportRound*` 로 넘긴다(넘길 때까지 걸음마다 다시 알린다)
        RoundStarted ///< 대기가 끝나 다음 라운드가 열렸다
    };

    /** @brief 라운드 결과를 넣은 뒤입니다. */
    enum class RoundSeriesOutcome : uint8
    {
        Rejected = 0, ///< 라운드 중이 아니거나 참가자가 맞지 않아 받지 않았다
        NextRound,    ///< 대기 없이 다음 라운드가 열렸다
        Intermission, ///< 다음 라운드를 기다린다
        Finished
    };
} // namespace sw

namespace sw
{
    /** @brief 라운드 묶음 설정입니다. 시간은 정수 걸음이고 0 은 "없음" 입니다. */
    struct RoundSeriesSettings
    {
        vector<int32>      _listPlacementPoint{};   ///< 순위 점수([0] = 1 위). 비면 1 위만 1 점(선승제), 목록보다 낮은 순위는 0 점
        int32              _winScore{ 1 };          ///< 먼저 이만큼 모으면 우승(1 보다 작으면 1)
        int32              _roundTicks{ 0 };        ///< 라운드 시간
        int32              _intermissionTicks{ 0 }; ///< 라운드 사이 대기(0 = 결과를 넣자마자 다음 라운드)
        RoundSeriesTieRule _tieRule{ RoundSeriesTieRule::SuddenDeath };
    };
} // namespace sw

namespace sw
{
    /**
     * @class RoundSeries
     * @brief 여러 라운드의 점수표입니다. 라운드 결과는 키트가 `reportRound`(라운드 점수 — 높을수록 앞 순위) · `reportRoundWinner`(이긴 쪽 하나 또는 무승부)로 넣습니다.
     * @details 같은 라운드 점수는 같은 순위 · 같은 순위 점수입니다(무승부 라운드 = 모두 1 위). 목표 점수에 닿은 참가자 중 총점이 가장 높은 한 명이 우승이고,
     *          그 총점이 같으면 `_tieRule` 을 따릅니다. 라운드 번호는 0 부터이고 라운드 목록(미니 게임 · 무대)은 키트가 번호로 고릅니다.
     *          알림은 내지 않습니다 — 돌려준 결과(`RoundSeriesOutcome` · `RoundSeriesTick`)로 키트가 제 이벤트를 냅니다(격투 이벤트는 롤백 상태 밖이다).
     *          상태 바이트(`writeState`)는 단계 · 라운드 번호 · 남은 라운드 시간 · 남은 대기 · 우승자 · 참가자 수 · 총점 순서이고, 롤백 키트가 자기 상태에 그대로 싣습니다.
     */
    class SW_GF_API RoundSeries
    {
    public:
        static constexpr int32 kNoWinner = -1; ///< 우승자 없음(끝나지 않음 · 무승부), `reportRoundWinner` 에서는 무승부 라운드

        RoundSeries();

        /** @brief 설정을 받고 `Waiting` 으로 비웁니다. */
        void initialize( const RoundSeriesSettings& settings );
        /** @brief @p participantCount 명으로 총점을 비우고 첫 라운드(0)를 엽니다. 1 명보다 적으면 false 입니다. */
        [[nodiscard]] bool start( int32 participantCount );
        /** @brief 한 걸음 진행합니다 — 라운드 중이면 라운드 시간을, 대기 중이면 대기를 셉니다. */
        RoundSeriesTick advanceTick();
        /** @brief 라운드 점수(참가자 순서)로 순위를 매겨 순위 점수를 쌓습니다. */
        [[nodiscard]] RoundSeriesOutcome reportRound( const vector<int32>& listRoundScore );
        /** @brief 이긴 쪽 하나는 1 위, 나머지는 2 위입니다. @p winner 가 `kNoWinner` 면 무승부 라운드(모두 1 위)입니다. */
        [[nodiscard]] RoundSeriesOutcome reportRoundWinner( int32 winner );

        void writeState( BitWriter& outWriter ) const;
        /** @brief `writeState` 의 바이트로 되돌립니다. 같은 참가자 수로 시작한 묶음이어야 하고, 깨졌거나 맞지 않으면 false 이고 바꾸지 않습니다. */
        [[nodiscard]] bool readState( BitReader& reader );

        /** @brief 라운드 점수로 매긴 순위(1 부터)입니다 — 1 + 나보다 점수가 높은 참가자 수(같은 점수는 같은 순위). 범위 밖이면 0 입니다. */
        static int32 computeRank( const vector<int32>& listRoundScore, int32 participant );
        /** @brief 순위 @p rank 가 받는 점수입니다. */
        int32 getPlacementPoint( int32 rank ) const;

        const RoundSeriesSettings& getSettings() const { return _settings; }
        RoundSeriesPhase           getPhase() const { return _phase; }
        /** @brief 라운드 중이거나 대기 중인가입니다(`start` 뒤, 끝나기 전). */
        bool  isRunning() const { return _phase == RoundSeriesPhase::RoundActive || _phase == RoundSeriesPhase::Intermission; }
        bool  isFinished() const { return _phase == RoundSeriesPhase::Finished; }
        int32 getRoundIndex() const { return _roundIndex; }
        int32 getRoundTicksRemaining() const { return _roundTicksRemaining; }
        int32 getIntermissionTicksRemaining() const { return _intermissionTicksRemaining; }
        /** @brief 우승자입니다. 끝나지 않았거나 무승부면 `kNoWinner` 입니다. */
        int32 getWinner() const { return _winner; }
        int32 getParticipantCount() const { return static_cast<int32>( _listTotal.size() ); }
        int32 getTotal( int32 participant ) const;

    private:
        bool               isValidParticipant( int32 participant ) const { return 0 <= participant && participant < getParticipantCount(); }
        void               openRound( int32 roundIndex );
        RoundSeriesOutcome finishRound();

        RoundSeriesSettings _settings;
        vector<int32>       _listTotal;
        int32               _roundIndex;
        int32               _roundTicksRemaining;
        int32               _intermissionTicksRemaining;
        int32               _winner;
        RoundSeriesPhase    _phase;
    };
} // namespace sw
