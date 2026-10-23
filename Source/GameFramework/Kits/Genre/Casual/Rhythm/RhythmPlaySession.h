/**
 * @file RhythmPlaySession.h
 * @brief 건반 리듬 한 판을 진행합니다. 레인 대기열, 누르기와 떼기 판정(`TimingJudge`), 롱노트, 놓침, 콤보, 점수, 라이프, 정확도, 등급, 오토플레이, 입력 기록을 다룹니다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/EventBuffer.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Casual/Rhythm/RhythmChart.h"

namespace sw
{
    struct TimingWindow;

    class Archive;
    class TimingJudge;

    /** @brief 판정 등급 하나의 라이프 증감과 정확도 가중치입니다. 점수는 판정 창(`TimingWindow::_score`)의 값을 씁니다. */
    struct RhythmGradeRule
    {
        hashed_string _grade{};
        float32       _lifeDelta{ 0.0f };
        float32       _accuracyWeight{ -1.0f }; ///< 0..1 범위이다. 음수이면 창 점수를 가장 좁은 창의 점수로 나눈 값을 쓴다
    };
} // namespace sw

namespace sw
{
    /** @brief 정확도(%)가 이 값 이상이면 이 등급입니다. */
    struct RhythmRankThreshold
    {
        hashed_string _rank{};
        float32       _minAccuracy{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 한 판의 규칙 수치입니다. */
    struct RhythmPlaySettings
    {
        vector<RhythmGradeRule>     _listGradeRule{};
        vector<RhythmRankThreshold> _listRankThreshold{}; ///< 비어 있으면 S 95, A 90, B 80, C 70, D 0 을 쓴다
        hashed_string               _failedRank{ "F" };
        float32                     _missLifeDelta{ -10.0f };
        float32                     _maxLife{ 100.0f };
        float32                     _initialLife{ 100.0f };
        float32                     _comboBonusPerCombo{ 0.0f }; ///< 맞힌 판정의 점수에 창 점수 x min( 콤보, 상한 ) x 이 값을 더한다
        int32                       _comboBonusCap{ 100 };
        float32                     _globalOffset{ 0.0f }; ///< 전역 오프셋(초)이다. 기기와 음향 지연을 보정하며, 노트의 판정 시각은 채보 시각에 이 값을 더한 것이다
        RhythmScrollMode            _scrollMode{ RhythmScrollMode::ConstantSpeed };
        uint8                       _bAutoPlay{ SW_FALSE }; ///< 판정 시각에 정확히 누르고 롱노트는 끝까지 누른다(데모와 테스트용)
    };
} // namespace sw

namespace sw
{
    /** @brief 한 판의 상태입니다. */
    enum class RhythmPlayState : uint8
    {
        Playing = 0,
        Cleared, ///< 모든 판정이 끝났고 라이프가 남았다
        Failed   ///< 라이프가 0 이 되었다. 더 판정하지 않는다
    };

    /** @brief 게임에 알리는 이벤트입니다(판정 표시, 효과음, 결과 화면에 씁니다). */
    struct RhythmEvent
    {
        enum class Kind : uint8
        {
            Judged = 0, ///< 판정 하나이다(`_grade` 가 비어 있으면 Miss)
            EmptyPress, ///< 칠 노트가 없거나 너무 이른 누름이며 소리만 낸다
            Cleared,
            Failed
        };
        hashed_string _grade{};
        float32       _offset{ 0.0f }; ///< 누른 시각에서 판정 시각을 뺀 값이다(Miss 는 0)
        int32         _noteIndex{ -1 };
        int32         _lane{ -1 };
        int32         _combo{ 0 };
        Kind          _kind{ Kind::Judged };
        uint8         _bTail{ SW_FALSE }; ///< 롱노트 끝 판정이다
    };
} // namespace sw

namespace sw
{
    /** @brief 입력 하나입니다. 기록해 두었다가 같은 순서로 다시 넣으면 같은 판이 됩니다(리플레이). */
    struct RhythmInputRecord
    {
        float32 _time{ 0.0f };
        int32   _lane{ 0 };
        uint8   _bPress{ SW_TRUE };
    };
} // namespace sw

namespace sw
{
    /**
     * @class RhythmPlaySession
     * @brief 오투잼과 비트매니아의 판정 규칙을 구현합니다. 시각은 음악 시각(초)이며 항상 늘어나는 방향으로만 넣어야 합니다.
     * @details - 누르기: 그 레인의 다음 노트(지나간 것은 이미 Miss)를 `TimingJudge::judge` 로 판정한다. 창 밖으로 너무 이르면(가장 넓은 창의 이른 폭
     *            `getEarliestWidth` 보다 앞) 누름을 노트에 쓰지 않는다.
     *          - 롱노트: 머리를 맞히면 누르고 있는 상태가 되고, 끝 시각까지 누르고 있으면 끝 판정은 가장 좁은 창이다. 끝보다 먼저 떼면 떼는 시각을 끝 시각에
     *            판정하고, 창 밖으로 이르면 끝은 Miss 이다. 머리를 놓치면 끝도 Miss 이다.
     *          - 놓침: `TimingJudge::hasExpired` 가 참이 된 노트는 Miss 이다.
     *          - 콤보는 Miss 와 `_bBreaksCombo` 창에서 끊긴다. 라이프가 0 이 되면 실패하고 더 판정하지 않는다.
     *          채보와 판정기는 빌려 쓰므로 판보다 오래 살아야 하고 바뀌지 않아야 한다. 같은 입력이면 같은 결과가 나온다(난수 없음).
     */
    class SW_KIT_API RhythmPlaySession
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "RHYP" );
        static constexpr uint32 kStateVersion = 1;

        RhythmPlaySession();

        /**
         * @brief 판을 처음부터 시작합니다. 채보의 레인 수만큼 대기열을 만듭니다.
         * @return 최대 라이프가 1 보다 작거나, 처음 라이프가 0..최대 밖이거나, 콤보 보너스 상한이 음수이면 오류를 남기고 false 입니다(맞춰 받지 않는다). 그러면 판은 그대로입니다.
         */
        [[nodiscard]] bool initialize( const RhythmChart* pChart, const TimingJudge* pJudge, const RhythmPlaySettings& settings );

        /** @brief @p now 까지 지난 일(놓침, 롱노트를 끝까지 누름, 오토플레이 누름)을 시각 순으로 처리합니다. */
        void update( float32 now );
        /** @brief 레인을 누릅니다. 먼저 `update( time )` 을 호출한 뒤 처리합니다. 오토플레이에서는 무시합니다. */
        void press( int32 lane, float32 time );
        /** @brief 레인을 뗍니다(롱노트 끝 판정). */
        void release( int32 lane, float32 time );

        /** @brief 쌓인 이벤트를 넘기고 비웁니다. */
        void drainEvents( vector<RhythmEvent>& outListEvent );

        /**
         * @brief 레인마다 판정 위치와 누르고 있는 롱노트, 등급별 판정 수, 입력 기록, 점수, 정확도 합, 라이프, 지금 시각, 콤보, 놓침, 판정 수, 상태를 씁니다.
         * @details 채보와 판정기는 빌린 것이라 저장하지 않는다(채보 정의가 카탈로그 id 를 가지고 있지 않다). 같은 채보인지는 노트 수와 레인 수로 확인한다. 설정은 `initialize` 에서 받고, 이벤트는 읽을 때 비운다.
         */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 가 쓴 바이트를 읽어 상태를 복원합니다. 채보나 판정 창의 수가 다르거나 데이터가 깨졌으면 false 를 반환하고 상태는 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

        /** @brief 정확도(%)를 반환합니다. 판정마다 가중치(Miss 는 0)의 평균이며, 판정이 없으면 100 입니다. */
        float32 computeAccuracy() const;
        /** @brief 지금 정확도의 등급을 반환합니다. 실패했으면 `_failedRank` 입니다. */
        hashed_string computeRank() const;
        /** @brief 노트가 판정선 위로 떨어진 거리를 반환합니다(전역 오프셋과 설정의 스크롤 방식을 반영합니다). */
        float32 computeNoteY( float32 noteBeat, float32 now, float32 hiSpeed ) const;
        /** @brief 등급의 판정 수를 반환합니다. */
        int32 findGradeCount( const hashed_string& grade ) const;

        RhythmPlayState                  getState() const { return _state; }
        bool                             isFinished() const { return _state != RhythmPlayState::Playing; }
        int64                            getScore() const { return _score; }
        int32                            getCombo() const { return _combo; }
        int32                            getMaxCombo() const { return _maxCombo; }
        int32                            getMissCount() const { return _missCount; }
        int32                            getJudgedCount() const { return _judgedCount; }
        float32                          getLife() const { return _life; }
        const vector<RhythmInputRecord>& getInputRecords() const { return _listInputRecord; }
        /** @brief 레인이 롱노트를 누르고 있는지 반환합니다. */
        bool isHolding( int32 lane ) const;

    private:
        /** @brief 레인 하나의 대기열입니다. */
        struct LaneQueue
        {
            vector<int32> _listNoteIndex{}; ///< 그 레인의 노트(시각 순)
            size_t        _cursor{ 0 };     ///< 아직 머리를 판정하지 않은 첫 노트이다
            int32         _holdingNote{ -1 };
        };

        float32                computeTargetTime( const RhythmNote& note ) const { return note._time + _settings._globalOffset; }
        float32                computeTargetEndTime( const RhythmNote& note ) const { return note._endTime + _settings._globalOffset; }
        void                   pressInternal( int32 lane, float32 time );
        void                   applyJudgment( int32 noteIndex, const TimingWindow* pWindow, float32 offset, bool bTail );
        const RhythmGradeRule* findGradeRule( const hashed_string& grade ) const;
        void                   finishIfDone();

        RhythmPlaySettings        _settings;
        vector<LaneQueue>         _listLane;
        vector<int32>             _listGradeCount; ///< 판정기의 창 순서(좁은 것부터)를 따른다
        EventBuffer<RhythmEvent>  _eventBuffer;
        vector<RhythmInputRecord> _listInputRecord;
        const RhythmChart*        _pChart;
        const TimingJudge*        _pJudge;
        int64                     _score;
        float64                   _accuracyWeightSum;
        float32                   _life;
        float32                   _now;
        int32                     _combo;
        int32                     _maxCombo;
        int32                     _missCount;
        int32                     _judgedCount;
        RhythmPlayState           _state;
    };
} // namespace sw
