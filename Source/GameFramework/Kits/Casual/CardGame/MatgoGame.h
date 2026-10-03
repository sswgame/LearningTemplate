/**
 * @file MatgoGame.h
 * @brief 맞고(2 인) · 고스톱(3 인) — 손패 · 바닥 · 같은 월 먹기, 뻑 · 따닥 · 쪽 · 싹쓸이(피 뺏기), 점수, 고/스톱, 고 배수, 피박 · 광박 · 멍박입니다.
 * @details 한 엔진이 인원 · 손패 · 바닥 · 기준 점수만 바꿔 맞고와 고스톱을 합니다(`MatgoSettings::makeMatgo` · `makeGoStop`). 규칙 변형은 설정 구조체입니다.
 *          한 차례: 손패 한 장을 내고(같은 월 바닥 패를 먹거나 바닥에 놓는다) 더미 맨 위를 뒤집어 같은 식으로 맞춥니다.
 *          - 쪽: 낸 패가 바닥에 놓였는데 뒤집은 패가 같은 월 → 둘 다 먹고 피 한 장씩 뺏는다
 *          - 뻑: 낸 패가 바닥 한 장과 맞았는데 뒤집은 패도 같은 월 → 셋 다 바닥에 남는다(그 월이 넷째 장에 다 먹힌다)
 *          - 따닥: 낸 패가 바닥 두 장 중 하나와 맞았는데 뒤집은 패가 넷째 → 넷 다 먹고 피를 뺏는다
 *          - 뻑 먹기: 바닥에 같은 월 셋(뻑)이 있을 때 넷째로 다 먹고 피를 뺏는다
 *          - 싹쓸이: 차례 뒤 바닥이 비면 피를 뺏는다
 *          흔들기 · 폭탄 · 총통 · 보너스 패 · 고박 · 첫 뻑 보너스는 아직 없습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Casual/CardGame/CardDeck.h"
#include "GameFramework/Kits/Casual/CardGame/HwatuDeck.h"

namespace sw
{
    /** @brief 규칙 설정입니다. */
    struct MatgoSettings
    {
        int32 _playerCount{ 2 };
        int32 _handSize{ 10 };
        int32 _floorSize{ 8 };
        int32 _goThreshold{ 7 };     ///< 이 점수가 나면 고/스톱(맞고 7 · 고스톱 3)
        int32 _gwangThreeScore{ 3 }; ///< 비광 없는 3 광
        int32 _gwangRainThreeScore{ 2 };
        int32 _gwangFourScore{ 4 };
        int32 _gwangFiveScore{ 15 };
        int32 _godoriScore{ 5 };
        int32 _danScore{ 3 };          ///< 홍단 · 청단 · 초단 각각
        int32 _yeolThreshold{ 5 };     ///< 열끗 이만큼부터 1 점, 한 장마다 +1
        int32 _ttiThreshold{ 5 };      ///< 띠 이만큼부터 1 점
        int32 _piThreshold{ 10 };      ///< 피 이만큼부터 1 점
        int32 _pibakBelow{ 6 };        ///< 진 쪽 피가 이보다 적으면 피박(이긴 쪽이 피로 점수를 냈을 때)
        int32 _meongbakYeolCount{ 7 }; ///< 이긴 쪽 열끗이 이만큼이면 멍박(멍따)
        uint8 _bPibak{ SW_TRUE };
        uint8 _bGwangbak{ SW_TRUE };
        uint8 _bMeongbak{ SW_FALSE };
        uint8 _bSeptemberYeolAsDoublePi{ SW_FALSE }; ///< 국진 열끗을 쌍피로 센다

        /** @brief 맞고(2 인 · 손패 10 · 바닥 8 · 7 점)입니다. */
        static MatgoSettings makeMatgo() { return MatgoSettings{}; }
        /** @brief 고스톱(3 인 · 손패 7 · 바닥 6 · 3 점)입니다. */
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

    /** @brief 먹은 패의 점수 내역입니다. */
    struct HwatuScore
    {
        int32 _gwang{ 0 };
        int32 _godori{ 0 };
        int32 _dan{ 0 }; ///< 홍단 + 청단 + 초단
        int32 _yeol{ 0 };
        int32 _tti{ 0 };
        int32 _pi{ 0 };
        int32 _total{ 0 };
        int32 _gwangCount{ 0 };
        int32 _yeolCount{ 0 };
        int32 _ttiCount{ 0 };
        int32 _piCount{ 0 }; ///< 쌍피는 2
    };

    /** @brief 판의 단계입니다. */
    enum class MatgoPhase : uint8
    {
        Play = 0,     ///< 차례인 사람이 패를 낸다
        GoStopChoice, ///< 차례인 사람이 고/스톱을 고른다
        Finished      ///< 누가 스톱했거나 나가리
    };

    /** @brief 행동 종류입니다(`CardAction::_kind`). */
    enum class MatgoActionKind : uint8
    {
        Play = 0, ///< `_cardId` = 낼 패, `_targetId` = 같은 월 바닥 두 장 중 먹을 것(없으면 센 것)
        Go,
        Stop
    };

    /** @brief 판에서 생긴 일입니다. */
    struct MatgoEvent
    {
        enum class Kind : uint8
        {
            Played = 0, ///< _cardId
            Flipped,    ///< _cardId
            Captured,   ///< _cardId — 먹은 패 한 장마다
            Ppeok,
            Jjok,
            Ttadak,
            PpeokEaten,
            Sweep,
            PiStolen,     ///< _player = 뺏은 쪽, _value = 뺏긴 쪽, _cardId
            GoStopChoice, ///< _value = 점수
            Go,           ///< _value = 고 횟수
            Stop,         ///< _value = 받을 점수(진 사람 하나당 · 고스톱이면 첫 패자 기준)
            Nagari
        };
        int32  _player{ -1 };
        int32  _value{ 0 };
        uint16 _cardId{ Card::kNoCard };
        Kind   _kind{ Kind::Played };
    };

    /** @brief 한 사람의 패입니다. */
    struct MatgoPlayer
    {
        CardPile _hand{};
        CardPile _captured{};
        int32    _goCount{ 0 };
        int32    _scoreAtLastGo{ 0 };
    };

    /** @brief 판의 시작 모양입니다(시험 · 리플레이). 더미는 맨 위(끝)부터 뒤집습니다. */
    struct MatgoLayout
    {
        vector<CardPile> _listHand{};
        vector<CardPile> _listCaptured{}; ///< 이미 먹은 패(비우면 없음 — 이어 하기 · 시험)
        CardPile         _floor{};
        CardPile         _drawPile{};
        int32            _firstPlayer{ 0 };
    };

    /** @brief 이긴 쪽이 진 쪽 하나에게 받는 점수의 내역입니다. */
    struct MatgoPayout
    {
        int32 _baseScore{ 0 };  ///< 점수 + 고 가산
        int32 _multiplier{ 1 }; ///< 고 배수 × 박
        int32 _total{ 0 };
        uint8 _bPibak{ SW_FALSE };
        uint8 _bGwangbak{ SW_FALSE };
        uint8 _bMeongbak{ SW_FALSE };
    };

    /**
     * @class MatgoGame
     * @brief 맞고 · 고스톱 한 판입니다. 결정적입니다(같은 씨앗 · 같은 행동 → 같은 판).
     */
    class SW_GF_API MatgoGame
    {
    public:
        MatgoGame();

        /** @brief 48 장을 @p seed 로 섞어 나눕니다(사람마다 손패 → 바닥 → 남은 더미). 바닥에 같은 월 넷이면 다시 섞습니다. */
        void initialize( const MatgoSettings& settings, uint32 seed );
        void initializeFromLayout( const MatgoSettings& settings, const MatgoLayout& layout );

        /** @brief 차례인 @p player 가 손패 @p cardId 를 냅니다. 같은 월 바닥 두 장이면 @p targetId 를 먹습니다(아니면 센 것). */
        [[nodiscard]] bool playCard( int32 player, uint16 cardId, uint16 targetId = Card::kNoCard );
        /** @brief 고 — 판을 이어 갑니다. 다음에는 점수가 더 올라야 다시 고/스톱입니다. */
        [[nodiscard]] bool declareGo( int32 player );
        /** @brief 스톱 — @p player 가 이기고 판이 끝납니다. */
        [[nodiscard]] bool declareStop( int32 player );
        [[nodiscard]] bool applyAction( int32 player, const CardAction& action );

        /** @brief 먹은 패의 점수입니다. 상태가 없습니다. */
        static HwatuScore computeScore( const CardPile& captured, const MatgoSettings& settings );
        /** @brief 이긴 쪽(@p winner, 고 @p goCount 번)이 진 쪽(@p loser)에게 받을 점수입니다 — 1 · 2 고는 +1 · +2, 3 고부터 ×2 씩, 박마다 ×2. */
        static MatgoPayout computePayout( const HwatuScore& winner, const HwatuScore& loser, int32 goCount, const MatgoSettings& settings );

        MatgoPhase getPhase() const { return _phase; }
        int32      getCurrentPlayer() const { return _currentPlayer; }
        /** @brief 이긴 사람입니다. 아직이거나 나가리면 −1 입니다. */
        int32              getWinner() const { return _winner; }
        int32              getPlayerCount() const { return static_cast<int32>( _listPlayer.size() ); }
        const MatgoPlayer& getPlayer( int32 player ) const { return _listPlayer[static_cast<size_t>( player )]; }
        const CardPile&    getFloor() const { return _floor; }
        const CardPile&    getDrawPile() const { return _drawPile; }
        HwatuScore         computePlayerScore( int32 player ) const { return computeScore( getPlayer( player )._captured, _settings ); }
        /** @brief 끝난 판에서 사람마다 받은(+) · 낸(−) 점수입니다. */
        const vector<int32>& getSettlement() const { return _listSettlement; }
        const MatgoSettings& getSettings() const { return _settings; }
        void                 drainEvents( vector<MatgoEvent>& outListEvent );

    private:
        void resolveCard( int32 player, const Card& card, uint16 targetId );
        void captureCard( int32 player, const Card& card );
        void captureFloorCard( int32 player, uint16 cardId );
        void stealPi( int32 player );
        void finishTurn( int32 player );
        void finishGame( int32 winner );
        bool areHandsEmpty() const;

        vector<MatgoPlayer> _listPlayer;
        vector<MatgoEvent>  _listEvent;
        vector<int32>       _listSettlement;
        CardPile            _floor;
        CardPile            _drawPile;
        MatgoSettings       _settings;
        int32               _currentPlayer;
        int32               _winner;
        MatgoPhase          _phase;
    };
} // namespace sw
