/**
 * @file PartyRoundSeries.h
 * @brief 파티 게임의 라운드 묶음 — 라운드 목록(미니 게임 · 시간 · 점수 제한)을 차례로 돌리고, 라운드 순위로 점수표에 점수를 쌓아 먼저 N 점에 닿은 사람이 우승합니다.
 * @details 라운드 안에서 무슨 일이 있었는지는 모릅니다 — 미니 게임이 라운드 점수(사람 순서)를 넘기면 순위를 매겨 순위 점수를 줍니다.
 *          같은 라운드 점수는 같은 순위 · 같은 순위 점수입니다. 한 라운드에 여럿이 목표에 닿으면 총점이 가장 높은 한 명이 우승이고,
 *          그 총점도 같으면 아무도 우승하지 않고 다음 라운드로 갑니다(서든 데스). 점수표는 기반 `RoundSeries` 이고, 여기는 라운드 목록 · XML · 알림을 듭니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Foundation/Data/XMLCatalog.h"
#include "GameFramework/Base/Foundation/Utility/EventBuffer.h"
#include "GameFramework/Base/Gameplay/Match/RoundSeries.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XMLNode;

    /** @brief 라운드 하나입니다. */
    struct PartyRoundDef
    {
        hashed_string _id{};              ///< 미니 게임 이름(게임이 읽는다)
        float32       _timeLimit{ 0.0f }; ///< 0 = 없음
        int32         _scoreLimit{ 0 };   ///< 0 = 없음
    };
} // namespace sw

namespace sw
{
    /** @brief 묶음에 생긴 일입니다. */
    struct PartySeriesEvent
    {
        enum class Kind : uint8
        {
            RoundStarted = 0, ///< _value = 라운드 번호(0 부터)
            RoundRanked,      ///< _player, _value = 순위(1 부터), _points = 받은 순위 점수
            SeriesWon         ///< _player = 우승한 사람
        };
        hashed_string _roundID{};
        int32         _player{ -1 };
        int32         _value{ 0 };
        int32         _points{ 0 };
        Kind          _kind{ Kind::RoundStarted };
    };
} // namespace sw

namespace sw
{
    /**
     * @class PartyRoundSeries
     * @brief `<PartySeries winScore="5" placementPoints="3,2,1,0"><Round id="trampoline" time="60" scoreLimit="5"/>...</PartySeries>` 를 읽습니다.
     * @details 라운드 목록이 끝나면 처음부터 다시 돕니다.
     */
    class SW_GF_API PartyRoundSeries : public XMLCatalog<PartyRoundSeries>
    {
        friend class XMLCatalog<PartyRoundSeries>;

    public:
        PartyRoundSeries();

        void addRound( const PartyRoundDef& round ) { _listRound.push_back( round ); }
        /** @brief 순위 점수입니다([0] = 1 위). 목록보다 낮은 순위는 0 점입니다. 다음 `start` 부터 씁니다. */
        void setPlacementPoints( const vector<int32>& listPoint ) { _seriesSettings._listPlacementPoint = listPoint; }
        void setWinScore( int32 winScore ) { _seriesSettings._winScore = winScore > 0 ? winScore : 1; }

        /** @brief 2 명 이상으로 점수표를 비우고 첫 라운드를 엽니다. 라운드가 없거나 사람이 모자라면 false 입니다. */
        [[nodiscard]] bool start( int32 playerCount );
        /**
         * @brief 지금 라운드의 결과(사람 순서의 라운드 점수)를 넣습니다. 순위 점수를 쌓고 우승을 보고, 끝나지 않았으면 다음 라운드를 엽니다.
         * @return 받아들였으면 true(사람 수가 다르거나 이미 끝났으면 false).
         */
        [[nodiscard]] bool reportRound( const vector<int32>& listRoundScore );

        const PartyRoundDef*         getCurrentRound() const;
        int32                        getRoundNumber() const { return _series.getRoundIndex(); }
        int32                        getTotal( int32 player ) const { return _series.getTotal( player ); }
        int32                        getWinner() const { return _series.getWinner(); }
        bool                         isFinished() const { return _series.isFinished(); }
        int32                        getPlayerCount() const { return _series.getParticipantCount(); }
        const vector<PartyRoundDef>& getRounds() const { return _listRound; }
        const vector<int32>&         getPlacementPoints() const { return _seriesSettings._listPlacementPoint; }
        int32                        getWinScore() const { return _seriesSettings._winScore; }
        void                         drainEvents( vector<PartySeriesEvent>& outListEvent );

    private:
        static constexpr const utf8* kXMLRootName = "PartySeries"; ///< 루트 원소(`XMLCatalog`)
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );
        void                         pushEvent( PartySeriesEvent::Kind kind, int32 player, int32 value, int32 points );

        vector<PartyRoundDef>         _listRound;
        RoundSeriesSettings           _seriesSettings; ///< 다음 `start` 가 묶음에 넘길 순위 점수 · 목표 · 동점 규칙(서든 데스)
        RoundSeries                   _series;
        EventBuffer<PartySeriesEvent> _eventBuffer;
    };
} // namespace sw
