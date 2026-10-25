/**
 * @file RoundSeries.h
 * @brief 여러 라운드로 한 판을 가르는 규칙입니다. 라운드 순위에 따른 점수(선승제는 1 위 1 점), 먼저 목표 점수에 닿은 쪽의 우승, 동점 규칙, 라운드 시간, 라운드 사이 대기를 다룹니다.
 * @details 격투의 2 선승제(라운드 승은 1 위 1 점이고, 둘이 함께 닿으면 무승부)와 파티 게임의 순위 점수제(3, 2, 1, 0 점이고, 먼저 5 점이면 우승, 같으면 서든 데스)가 설정만 바꿔서 쓴다.
 *          시간은 정수 스텝(고정 스텝이나 프레임)으로 센다. 그래서 롤백이나 락스텝 키트에서도 결정적이다. 한 라운드 안의 점수, 부활, 시간 제한은 `MatchState` 가 맡는다.
 *          파티 게임은 라운드마다 다른 시간을 `MatchState` 에 준다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class ValidationContext;

    /** @brief 라운드 시리즈의 단계입니다. 값이 상태에 번호로 저장되므로 뒤에만 추가합니다. */
    ENUM()
    enum class RoundSeriesPhase : uint8
    {
        Waiting = 0, ///< `start` 전
        RoundActive,
        Intermission, ///< 라운드가 끝나고 다음 라운드를 기다린다
        Finished
    };

    /** @brief 목표에 닿은 참가자 둘 이상이 총점까지 같을 때의 처리 방식입니다. */
    ENUM()
    enum class RoundSeriesTieRule : uint8
    {
        Draw = 0,   ///< 무승부로 끝난다(격투에서 마지막 라운드가 무승부인 경우)
        SuddenDeath ///< 아무도 이기지 않고 다음 라운드로 넘어간다(파티 게임)
    };

    /** @brief `advanceTick` 이 알려 주는 결과입니다. */
    enum class RoundSeriesTick : uint8
    {
        None = 0,
        TimeUp,      ///< 라운드 시간이 다 됐다. 키트가 결과를 정해 `reportRound*` 로 넘긴다. 넘길 때까지 스텝마다 다시 알린다
        RoundStarted ///< 대기가 끝나 다음 라운드가 열렸다
    };

    /** @brief 라운드 결과를 넣은 뒤의 상태입니다. */
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
    /**
     * @brief 라운드 시리즈 설정입니다. 시간은 정수 스텝이고 0 은 "없음" 입니다.
     * @details 범위는 필드의 `PROPERTY( Min )` 이고, 벗어난 값은 `RoundSeries::initialize` 가 오류로 거절한다(`ConfigRangeUtil`). 값을 조용히 고치지 않는다(폴백 금지 원칙).
     */
    REFLECT()
    struct SW_GF_API RoundSeriesSettings
    {
        REFLECT_BODY();
        PROPERTY()
        vector<int32> _listPlacementPoint{}; ///< 순위 점수([0] 이 1 위). 비어 있으면 1 위만 1 점(선승제)이고, 목록보다 낮은 순위는 0 점이다
        PROPERTY( Min = 1 )
        int32 _winScore{ 1 }; ///< 먼저 이만큼 모으면 우승한다
        PROPERTY( Min = 0 )
        int32 _roundTicks{ 0 }; ///< 라운드 시간
        PROPERTY( Min = 0 )
        int32 _intermissionTicks{ 0 }; ///< 라운드 사이의 대기. 0 이면 결과를 넣자마자 다음 라운드가 열린다
        PROPERTY()
        RoundSeriesTieRule _tieRule{ RoundSeriesTieRule::SuddenDeath };
    };
} // namespace sw

namespace sw
{
    /**
     * @class RoundSeries
     * @brief 여러 라운드의 점수표입니다. 라운드 결과는 키트가 `reportRound`(라운드 점수이며 높을수록 앞 순위)나 `reportRoundWinner`(이긴 쪽 하나 또는 무승부)로 넣습니다.
     * @details 같은 라운드 점수는 같은 순위이고 같은 순위 점수를 받는다. 무승부 라운드는 모두 1 위이다. 목표 점수에 닿은 참가자 중 총점이 가장 높은 한 명이 우승하고,
     *          그 총점이 같으면 `_tieRule` 을 따른다. 라운드 번호는 0 부터 시작하고, 라운드 목록(미니 게임, 무대)은 키트가 번호로 고른다.
     *          알림은 내지 않는다. 반환한 결과(`RoundSeriesOutcome`, `RoundSeriesTick`)를 보고 키트가 자기 이벤트를 낸다. 격투 이벤트는 롤백 상태에 포함되지 않기 때문이다.
     *          상태(총점, 라운드 번호, 남은 라운드 시간, 남은 대기, 우승자, 단계)는 리플렉션 프로퍼티 한 벌이다. 손 코덱은 두지 않는다.
     *          롤백 키트는 고정 배치형(`FixedLayout`)으로, 세이브는 자기 기술형(`BinarySerializer`)으로 싣는다.
     *          설정(`_settings`)은 `initialize` 가 주는 값이라 싣지 않는다. 참가자 수 대조는 싣는 키트가 한다.
     */
    REFLECT( Validate = validateState )
    class SW_GF_API RoundSeries
    {
    public:
        REFLECT_BODY();
        static constexpr int32  kNoWinner            = -1; ///< 우승자 없음을 뜻한다(끝나지 않았거나 무승부). `reportRoundWinner` 에서는 무승부 라운드를 뜻한다
        static constexpr uint32 kMaxParticipantCount = 64; ///< 고정 배치형이 싣는 참가자 수의 상한

        RoundSeries();

        /** @brief 설정을 받고 `Waiting` 상태로 초기화합니다. 설정이 선언된 범위 밖이면 오류를 남기고 false 를 반환하며, 상태는 그대로입니다. */
        [[nodiscard]] bool initialize( const RoundSeriesSettings& settings );
        /** @brief @p participantCount 명으로 총점을 비우고 첫 라운드(0)를 엽니다. 1 명보다 적으면 false 입니다. */
        [[nodiscard]] bool start( int32 participantCount );
        /** @brief 한 스텝 진행합니다. 라운드 중이면 라운드 시간을, 대기 중이면 대기 시간을 셉니다. */
        RoundSeriesTick advanceTick();
        /** @brief 라운드 점수(참가자 순서)로 순위를 매기고 순위 점수를 누적합니다. */
        [[nodiscard]] RoundSeriesOutcome reportRound( const vector<int32>& listRoundScore );
        /** @brief 이긴 쪽 하나는 1 위이고 나머지는 2 위입니다. @p winner 가 `kNoWinner` 이면 무승부 라운드이며 모두 1 위입니다. */
        [[nodiscard]] RoundSeriesOutcome reportRoundWinner( int32 winner );

        /** @brief 라운드 점수로 매긴 순위(1 부터)입니다. 1 에 나보다 점수가 높은 참가자 수를 더한 값이며, 같은 점수는 같은 순위입니다. 범위 밖이면 0 입니다. */
        static int32 computeRank( const vector<int32>& listRoundScore, int32 participant );
        /** @brief 순위 @p rank 가 받는 점수입니다. */
        int32 getPlacementPoint( int32 rank ) const;

        const RoundSeriesSettings& getSettings() const { return _settings; }
        RoundSeriesPhase           getPhase() const { return _phase; }
        /** @brief 라운드 중이거나 대기 중이면 true 입니다. `start` 뒤이고 끝나기 전입니다. */
        bool  isRunning() const { return _phase == RoundSeriesPhase::RoundActive || _phase == RoundSeriesPhase::Intermission; }
        bool  isFinished() const { return _phase == RoundSeriesPhase::Finished; }
        int32 getRoundIndex() const { return _roundIndex; }
        int32 getRoundTicksRemaining() const { return _roundTicksRemaining; }
        int32 getIntermissionTicksRemaining() const { return _intermissionTicksRemaining; }
        /** @brief 우승자입니다. 끝나지 않았거나 무승부이면 `kNoWinner` 입니다. */
        int32 getWinner() const { return _winner; }
        int32 getParticipantCount() const { return static_cast<int32>( _listTotal.size() ); }
        int32 getTotal( int32 participant ) const;
        /** @brief 읽은 우승자가 참가자 안에 있는지 봅니다(`REFLECT( Validate )`). */
        void validateState( ValidationContext& context ) const;

    private:
        bool               isValidParticipant( int32 participant ) const { return 0 <= participant && participant < getParticipantCount(); }
        void               openRound( int32 roundIndex );
        RoundSeriesOutcome finishRound();

        RoundSeriesSettings _settings;
        PROPERTY( MaxCount = kMaxParticipantCount )
        vector<int32> _listTotal; ///< 참가자마다 총점
        PROPERTY( Min = 0 )
        int32 _roundIndex;
        PROPERTY( Min = 0 )
        int32 _roundTicksRemaining;
        PROPERTY( Min = 0 )
        int32 _intermissionTicksRemaining;
        PROPERTY( Min = -1 )
        int32 _winner;
        PROPERTY()
        RoundSeriesPhase _phase;
    };
} // namespace sw
