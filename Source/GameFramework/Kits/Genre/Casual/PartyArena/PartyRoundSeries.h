/**
 * @file PartyRoundSeries.h
 * @brief 파티 게임의 라운드 시리즈을 진행합니다. 라운드 목록(미니 게임, 시간, 점수 제한)을 차례로 돌리고, 라운드 순위로 점수표에 점수를 쌓아 먼저 N 점에 닿은 사람이 우승합니다.
 * @details 라운드 안에서 무슨 일이 있었는지는 모른다. 미니 게임이 라운드 점수(사람 순서)를 넘기면 순위를 매겨 순위 점수를 준다.
 *          같은 라운드 점수는 같은 순위이고 같은 순위 점수이다. 한 라운드에 여럿이 목표에 닿으면 총점이 가장 높은 한 명이 우승하고,
 *          그 총점도 같으면 아무도 우승하지 않고 다음 라운드로 간다(서든 데스). 점수표는 기반 `RoundSeries` 가 가지고 있고, 이 클래스는 라운드 목록, XML, 이벤트를 가진다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/EventBuffer.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Foundation/Data/XMLCatalog.h"
#include "GameFramework/Base/Gameplay/Match/RoundSeries.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XMLNode;

    /** @brief 라운드 하나입니다. */
    struct PartyRoundDef
    {
        hashed_string _id{};              ///< 미니 게임 이름이다(게임이 읽는다)
        float32       _timeLimit{ 0.0f }; ///< 0 이면 제한이 없다
        int32         _scoreLimit{ 0 };   ///< 0 이면 제한이 없다
    };
} // namespace sw

namespace sw
{
    /** @brief 라운드 시리즈에서 일어난 일을 알리는 이벤트입니다. */
    struct PartySeriesEvent
    {
        enum class Kind : uint8
        {
            RoundStarted = 0, ///< `_value` 는 라운드 번호(0부터)이다
            RoundRanked,      ///< `_player` 는 사람, `_value` 는 순위(1부터), `_points` 는 받은 순위 점수이다
            SeriesWon         ///< `_player` 는 우승한 사람이다
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
     * @details 라운드 목록이 끝나면 처음부터 다시 돈다.
     */
    class SW_KIT_API PartyRoundSeries : public XMLCatalog<PartyRoundSeries>
    {
        friend class XMLCatalog<PartyRoundSeries>;

    public:
        PartyRoundSeries();

        void addRound( const PartyRoundDef& round ) { _listRound.push_back( round ); }
        /** @brief 순위 점수를 정합니다([0] 이 1위). 목록보다 낮은 순위는 0점이며, 다음 `start` 부터 적용합니다. */
        void setPlacementPoints( const vector<int32>& listPoint ) { _seriesSettings._listPlacementPoint = listPoint; }
        void setWinScore( int32 winScore ) { _seriesSettings._winScore = winScore > 0 ? winScore : 1; }

        /** @brief 2명 이상으로 점수표를 비우고 첫 라운드를 엽니다. 라운드가 없거나 사람이 모자라면 false 를 반환합니다. */
        [[nodiscard]] bool start( int32 playerCount );
        /**
         * @brief 지금 라운드의 결과(사람 순서의 라운드 점수)를 받습니다. 순위 점수를 쌓고 우승 여부를 확인하며, 끝나지 않았으면 다음 라운드를 엽니다.
         * @return 받아들였으면 true 이다(사람 수가 다르거나 이미 끝났으면 false).
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
        static constexpr const utf8* kXMLRootName = "PartySeries"; ///< XML 루트 원소 이름이다(`XMLCatalog` 이 사용한다)
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );
        void                         pushEvent( PartySeriesEvent::Kind kind, int32 player, int32 value, int32 points );

        vector<PartyRoundDef>         _listRound;
        RoundSeriesSettings           _seriesSettings; ///< 다음 `start` 가 시리즈에 넘길 순위 점수, 목표, 동점 규칙(서든 데스)이다
        RoundSeries                   _series;
        EventBuffer<PartySeriesEvent> _eventBuffer;
    };
} // namespace sw
