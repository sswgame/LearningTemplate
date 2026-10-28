/**
 * @file MatgoGame.h
 * @brief 맞고(2인)와 고스톱(3인)을 구현합니다. 손패와 바닥, 같은 월 먹기, 뻑, 따닥, 쪽, 싹쓸이(피 뺏기), 점수, 고와 스톱, 고 배수, 피박, 광박, 멍박을 다룹니다.
 * @details 한 엔진이 인원, 손패, 바닥, 기준 점수만 바꿔 맞고와 고스톱을 모두 처리한다(`MatgoSettings::makeMatgo`, `makeGoStop`). 규칙 변형은 설정 구조체로 정한다.
 *          한 차례는 손패 한 장을 내고(같은 월 바닥 패를 먹거나 바닥에 놓는다), 더미 맨 위를 뒤집어 같은 식으로 맞추는 것이다.
 *          - 쪽: 낸 패가 바닥에 놓였는데 뒤집은 패가 같은 월이면, 둘 다 먹고 피를 한 장씩 뺏는다.
 *          - 뻑: 낸 패가 바닥 한 장과 맞았는데 뒤집은 패도 같은 월이면, 셋 다 바닥에 남는다. 그 월의 넷째 장으로 한꺼번에 먹을 수 있다.
 *          - 따닥: 낸 패가 바닥 두 장 중 하나와 맞았는데 뒤집은 패가 넷째 장이면, 넷 다 먹고 피를 뺏는다.
 *          - 뻑 먹기: 바닥에 같은 월 셋(뻑)이 있을 때 넷째 장으로 다 먹고 피를 뺏는다.
 *          - 싹쓸이: 차례가 끝났을 때 바닥이 비면 피를 뺏는다.
 *          흔들기, 폭탄, 총통, 보너스 패, 고박, 첫 뻑 보너스는 아직 구현하지 않았다.
 */
#pragma once
#include "Core/Container/EventBuffer.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/FourCcUtil.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Casual/CardGame/CardDeck.h"
#include "GameFramework/Kits/Genre/Casual/CardGame/Matgo/HwatuDeck.h"

namespace sw
{
    class Archive;

    /**
     * @brief 규칙 설정입니다.
     * @details 범위는 필드의 `PROPERTY( Min · Max )` 이고, 벗어난 값은 `MatgoGame::initialize*` 가 오류로 거절한다(`ConfigRangeUtil`). 인원 × 손패 + 바닥이 덱(48장)을 채우는 짝도 거절한다.
     *          값을 조용히 맞추지 않는다(폴백 금지 원칙).
     */
    REFLECT()
    struct SW_KIT_API MatgoSettings
    {
        REFLECT_BODY();
        PROPERTY( Min = 2, Max = 3 )
        int32 _playerCount{ 2 };
        PROPERTY( Min = 1 )
        int32 _handSize{ 10 };
        PROPERTY( Min = 0 )
        int32 _floorSize{ 8 };
        PROPERTY( Min = 1 )
        int32 _goThreshold{ 7 }; ///< 이 점수가 나면 고와 스톱을 고른다(맞고 7, 고스톱 3)
        PROPERTY( Min = 0 )
        int32 _gwangThreeScore{ 3 }; ///< 비광 없는 3광의 점수이다
        PROPERTY( Min = 0 )
        int32 _gwangRainThreeScore{ 2 };
        PROPERTY( Min = 0 )
        int32 _gwangFourScore{ 4 };
        PROPERTY( Min = 0 )
        int32 _gwangFiveScore{ 15 };
        PROPERTY( Min = 0 )
        int32 _godoriScore{ 5 };
        PROPERTY( Min = 0 )
        int32 _danScore{ 3 }; ///< 홍단, 청단, 초단 각각의 점수이다
        PROPERTY( Min = 1 )
        int32 _yeolThreshold{ 5 }; ///< 열끗이 이 장수부터 1점이고 한 장마다 1점씩 늘어난다
        PROPERTY( Min = 1 )
        int32 _ttiThreshold{ 5 }; ///< 띠가 이 장수부터 1점이다
        PROPERTY( Min = 1 )
        int32 _piThreshold{ 10 }; ///< 피가 이 장수부터 1점이다
        PROPERTY( Min = 0 )
        int32 _pibakBelow{ 6 }; ///< 진 쪽의 피가 이보다 적으면 피박이다(이긴 쪽이 피로 점수를 냈을 때)
        PROPERTY( Min = 1 )
        int32 _meongbakYeolCount{ 7 }; ///< 이긴 쪽의 열끗이 이 장수이면 멍박(멍따)이다
        uint8 _bPibak{ SW_TRUE };
        uint8 _bGwangbak{ SW_TRUE };
        uint8 _bMeongbak{ SW_FALSE };
        uint8 _bSeptemberYeolAsDoublePi{ SW_FALSE }; ///< 국진 열끗을 쌍피로 센다

        /** @brief 맞고 설정입니다(2인, 손패 10장, 바닥 8장, 7점). */
        static MatgoSettings makeMatgo() { return MatgoSettings{}; }
        /** @brief 고스톱 설정입니다(3인, 손패 7장, 바닥 6장, 3점). */
        static MatgoSettings makeGoStop()
        {
            MatgoSettings settings;
            settings._playerCount = 3;
            settings._handSize    = 7;
            settings._floorSize   = 6;
            settings._goThreshold = 3;
            return settings;
        }
    };
} // namespace sw

namespace sw
{
    /** @brief 먹은 패의 점수 내역입니다. */
    struct HwatuScore
    {
        int32 _gwang{ 0 };
        int32 _godori{ 0 };
        int32 _dan{ 0 }; ///< 홍단, 청단, 초단의 합이다
        int32 _yeol{ 0 };
        int32 _tti{ 0 };
        int32 _pi{ 0 };
        int32 _total{ 0 };
        int32 _gwangCount{ 0 };
        int32 _yeolCount{ 0 };
        int32 _ttiCount{ 0 };
        int32 _piCount{ 0 }; ///< 쌍피는 2장으로 센다
    };
} // namespace sw

namespace sw
{
    /** @brief 판의 단계입니다. */
    enum class MatgoPhase : uint8
    {
        Play = 0,     ///< 차례인 사람이 패를 낸다
        GoStopChoice, ///< 차례인 사람이 고와 스톱 중 하나를 고른다
        Finished      ///< 누가 스톱했거나 나가리가 되었다
    };

    /** @brief 행동의 종류입니다(`CardAction::_kind`). */
    enum class MatgoActionKind : uint8
    {
        Play = 0, ///< `_cardID` 는 낼 패이고, `_targetID` 는 같은 월 바닥 두 장 중 먹을 패이다(없으면 센 것을 먹는다)
        Go,
        Stop
    };

    /** @brief 판에서 일어난 일을 알리는 이벤트입니다. */
    struct MatgoEvent
    {
        enum class Kind : uint8
        {
            Played = 0, ///< `_cardID` 가 낸 패이다
            Flipped,    ///< `_cardID` 가 뒤집은 패이다
            Captured,   ///< 먹은 패 한 장마다 `_cardID` 로 알린다
            Ppeok,
            Jjok,
            Ttadak,
            PpeokEaten,
            Sweep,
            PiStolen,     ///< `_player` 가 뺏은 쪽, `_value` 가 뺏긴 쪽이고, `_cardID` 가 뺏은 패이다
            GoStopChoice, ///< `_value` 는 점수이다
            Go,           ///< `_value` 는 고 횟수이다
            Stop,         ///< `_value` 는 받을 점수이다(진 사람 한 명당이고, 고스톱이면 첫 패자 기준이다)
            Nagari
        };
        int32  _player{ -1 };
        int32  _value{ 0 };
        uint16 _cardID{ Card::kNoCard };
        Kind   _kind{ Kind::Played };
    };
} // namespace sw

namespace sw
{
    /** @brief 한 사람의 패입니다. */
    struct MatgoPlayer
    {
        CardPile _hand{};
        CardPile _captured{};
        int32    _goCount{ 0 };
        int32    _scoreAtLastGo{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 판의 시작 배치입니다(테스트와 리플레이용). 더미는 맨 위(끝)부터 뒤집습니다. */
    struct MatgoLayout
    {
        vector<CardPile> _listHand{};
        vector<CardPile> _listCaptured{}; ///< 이미 먹은 패이다(비우면 없다, 채우면 인원 수와 같아야 한다. 이어 하기와 테스트용)
        CardPile         _floor{};
        CardPile         _drawPile{};
        int32            _firstPlayer{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 이긴 쪽이 진 쪽 한 명에게 받는 점수의 내역입니다. */
    struct MatgoPayout
    {
        int32 _baseScore{ 0 };  ///< 점수에 고 가산을 더한 값이다
        int32 _multiplier{ 1 }; ///< 고 배수 x 박 배수이다
        int32 _total{ 0 };
        uint8 _bPibak{ SW_FALSE };
        uint8 _bGwangbak{ SW_FALSE };
        uint8 _bMeongbak{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /**
     * @class MatgoGame
     * @brief 맞고와 고스톱 한 판을 진행합니다. 같은 시드와 같은 행동이면 같은 판이 나오는 결정적 구현입니다.
     */
    class SW_KIT_API MatgoGame
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "CMTG" );
        static constexpr uint32 kStateVersion = 1;

        MatgoGame();

        /**
         * @brief 48장을 @p seed 로 섞어 나눕니다(사람마다 손패, 바닥, 남은 더미 순). 바닥에 같은 월 넷이 나오면 다시 섞습니다.
         * @return 설정이 범위 안이면 true 입니다. 밖이면 오류를 남기고 false 이며 상태를 바꾸지 않습니다.
         */
        [[nodiscard]] bool initialize( const MatgoSettings& settings, uint32 seed );
        /** @brief 정해진 배치로 시작합니다. 설정이 범위 밖이거나 손패 수 · 먹은 패 더미 수(비우면 없음)가 인원과 다르거나 첫 차례가 인원 밖이면 오류를 남기고 false 이며 상태를 바꾸지 않습니다. */
        [[nodiscard]] bool initializeFromLayout( const MatgoSettings& settings, const MatgoLayout& layout );

        /** @brief 차례인 @p player 가 손패 @p cardID 를 냅니다. 같은 월 바닥이 두 장이면 @p targetID 를 먹고, 지정하지 않으면 센 것을 먹습니다. */
        [[nodiscard]] bool playCard( int32 player, uint16 cardID, uint16 targetID = Card::kNoCard );
        /** @brief 고를 선언해 판을 이어 갑니다. 다음에는 점수가 더 올라야 다시 고와 스톱을 고릅니다. */
        [[nodiscard]] bool declareGo( int32 player );
        /** @brief 스톱을 선언합니다. @p player 가 이기고 판이 끝납니다. */
        [[nodiscard]] bool declareStop( int32 player );
        [[nodiscard]] bool applyAction( int32 player, const CardAction& action );

        /** @brief 사람마다 손패, 먹은 패, 고, 정산과 바닥, 더미, 차례, 이긴 사람, 단계를 씁니다. 설정은 `initialize` 에서 받으므로 저장하지 않고, 이벤트는 읽을 때 비웁니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 가 쓴 바이트를 읽어 상태를 복원합니다. 인원이 다르거나 데이터가 깨졌으면 false 를 반환하고 상태는 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

        /** @brief 먹은 패의 점수를 계산합니다. 상태가 없습니다. */
        static HwatuScore computeScore( const CardPile& captured, const MatgoSettings& settings );
        /** @brief 이긴 쪽(@p winner, 고 @p goCount 번)이 진 쪽(@p loser)에게 받을 점수를 계산합니다. 1고와 2고는 +1, +2 이고 3고부터는 2배씩 늘며, 박마다 2배입니다. */
        static MatgoPayout computePayout( const HwatuScore& winner, const HwatuScore& loser, int32 goCount, const MatgoSettings& settings );

        MatgoPhase getPhase() const { return _phase; }
        int32      getCurrentPlayer() const { return _currentPlayer; }
        /** @brief 이긴 사람을 반환합니다. 아직 정해지지 않았거나 나가리이면 -1 입니다. */
        int32              getWinner() const { return _winner; }
        int32              getPlayerCount() const { return static_cast<int32>( _listPlayer.size() ); }
        const MatgoPlayer& getPlayer( int32 player ) const { return _listPlayer[static_cast<size_t>( player )]; }
        const CardPile&    getFloor() const { return _floor; }
        const CardPile&    getDrawPile() const { return _drawPile; }
        HwatuScore         computePlayerScore( int32 player ) const { return computeScore( getPlayer( player )._captured, _settings ); }
        /** @brief 끝난 판에서 사람마다 받은 점수(+)와 낸 점수(-)를 반환합니다. */
        const vector<int32>& getSettlement() const { return _listSettlement; }
        const MatgoSettings& getSettings() const { return _settings; }
        void                 drainEvents( vector<MatgoEvent>& outListEvent );

    private:
        void resolveCard( int32 player, const Card& card, uint16 targetID );
        void captureCard( int32 player, const Card& card );
        void captureFloorCard( int32 player, uint16 cardID );
        void stealPi( int32 player );
        void finishTurn( int32 player );
        void finishGame( int32 winner );
        bool areHandsEmpty() const;

        vector<MatgoPlayer>     _listPlayer;
        EventBuffer<MatgoEvent> _eventBuffer;
        vector<int32>           _listSettlement;
        CardPile                _floor;
        CardPile                _drawPile;
        MatgoSettings           _settings;
        int32                   _currentPlayer;
        int32                   _winner;
        MatgoPhase              _phase;
    };
} // namespace sw
