/**
 * @file UnoGame.h
 * @brief 우노 — 숫자 · 스킵 · 리버스 · +2 · 와일드 · 와일드 +4, 낼 수 있는 카드 판정, 차례 방향, 우노 선언 누락 벌칙, +2 쌓기(설정)입니다.
 * @details 카드는 `Card::_suit` = 색(`UnoColor`), `Card::_rank` = 값(`UnoValue`)입니다. 덱은 108 장(색마다 0 한 장 · 1..9 와 스킵 · 리버스 · +2 두 장씩,
 *          와일드 · 와일드 +4 네 장씩)입니다. 뽑으면 차례가 넘어갑니다(뽑은 패를 바로 내는 규칙은 없다).
 *          한 장 남기며 우노를 외치지 않은 사람은 다음 사람이 행동하기 전까지 남이 `callUno` 로 잡을 수 있고(벌칙 장수만큼 뽑는다),
 *          본인이 먼저 `callUno` 하면 늦은 선언으로 칩니다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Utility/EventBuffer.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Casual/CardGame/CardDeck.h"

namespace sw
{
    class Archive;

    /** @brief 색입니다. */
    enum class UnoColor : uint8
    {
        Red = 0,
        Yellow,
        Green,
        Blue,
        Wild ///< 와일드 카드의 색 · "고르지 않음"
    };

    /** @brief 값입니다(0..9 는 숫자 그대로). */
    enum class UnoValue : uint8
    {
        Zero = 0,
        Nine = 9,
        Skip = 10,
        Reverse,
        DrawTwo,
        Wild,
        WildDrawFour
    };

    /** @brief 규칙 설정입니다. */
    struct UnoSettings
    {
        int32 _playerCount{ 4 };
        int32 _handSize{ 7 };
        int32 _unoPenalty{ 2 };                ///< 우노를 외치지 않다 잡히면 뽑는 장수
        uint8 _bStackDrawTwo{ SW_FALSE };      ///< +2 위에 +2 를 쌓아 넘긴다
        uint8 _bStackDrawFour{ SW_FALSE };     ///< 쌓인 벌칙 위에 +4 를 쌓아 넘긴다
        uint8 _bStrictWildDrawFour{ SW_TRUE }; ///< 지금 색의 카드가 있으면 +4 를 낼 수 없다
    };
} // namespace sw

namespace sw
{
    /** @brief 행동 종류입니다(`CardAction::_kind`). */
    enum class UnoActionKind : uint8
    {
        Play = 0, ///< `_cardId`, `_targetId` = 고른 색(와일드), `_amount` ≠ 0 = 우노 선언
        Draw,
        CallUno
    };

    /** @brief 판에서 생긴 일입니다. */
    struct UnoEvent
    {
        enum class Kind : uint8
        {
            Played = 0, ///< _cardId
            Drew,       ///< _value = 장수
            Skipped,
            Reversed,
            ColorChosen, ///< _value = UnoColor
            UnoDeclared,
            UnoPenalty, ///< _player = 벌칙 받은 사람, _value = 장수
            Reshuffled,
            Won
        };
        int32  _player{ -1 };
        int32  _value{ 0 };
        uint16 _cardId{ Card::kNoCard };
        Kind   _kind{ Kind::Played };
    };
} // namespace sw

namespace sw
{
    /** @brief 판의 시작 모양입니다(시험 · 리플레이). */
    struct UnoLayout
    {
        vector<CardPile> _listHand{};
        CardPile         _drawPile{};    ///< 맨 위(끝)부터 뽑는다
        CardPile         _discardPile{}; ///< 맨 위가 지금 낼 기준
        int32            _firstPlayer{ 0 };
        UnoColor         _color{ UnoColor::Red };
    };
} // namespace sw

namespace sw
{
    /**
     * @class UnoGame
     * @brief 우노 한 판입니다. 결정적입니다(뽑을 더미가 떨어지면 버린 더미를 같은 난수로 섞는다).
     */
    class SW_GF_API UnoGame
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "CUNO" );
        static constexpr uint32 kStateVersion = 1;

        UnoGame();

        /** @brief 108 장을 @p seed 로 섞어 한 장씩 돌려 나누고, 와일드가 아닌 첫 장을 뒤집습니다(첫 장의 효과는 없다). */
        void initialize( const UnoSettings& settings, uint32 seed );
        void initializeFromLayout( const UnoSettings& settings, const UnoLayout& layout, uint32 seed = GameRandom::kDefaultSeed );

        /** @brief 차례인 @p player 가 낼 수 있는 패 @p cardId 를 냅니다. 와일드는 @p chosenColor 가 네 색 중 하나여야 합니다. */
        [[nodiscard]] bool playCard( int32 player, uint16 cardId, UnoColor chosenColor = UnoColor::Wild, bool bDeclareUno = false );
        /** @brief 차례인 @p player 가 뽑습니다 — 쌓인 벌칙이 있으면 그만큼, 없으면 한 장. 차례가 넘어갑니다. */
        [[nodiscard]] bool drawCard( int32 player );
        /** @brief 우노 잡기(남) · 늦은 선언(본인)입니다. 잡을 사람이 없으면 false 입니다. */
        [[nodiscard]] bool callUno( int32 caller );
        [[nodiscard]] bool applyAction( int32 player, const CardAction& action );

        /** @brief 손패 · 뽑을 더미 · 버린 더미 · 난수 · 차례 · 방향 · 쌓인 벌칙 · 우노 대상 · 이긴 사람 · 색을 씁니다. 설정은 `initialize` 의 것이라 싣지 않고, 알림은 읽을 때 비웁니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 인원이 다르거나 깨졌으면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

        /** @brief 지금 낼 수 있는 카드인가입니다(색 · 값 · 와일드, 벌칙이 쌓였으면 쌓을 수 있는 카드만). */
        bool isPlayable( const Card& card ) const;
        /** @brief 108 장(번호 0..107)입니다. */
        static void makeDeck( CardPile& outPile );
        /** @brief 손패 점수(숫자 = 그 수, 스킵 · 리버스 · +2 = 20, 와일드 = 50)입니다 — 이긴 사람이 남의 손패를 받는다. */
        static int32 computeHandPoints( const CardPile& hand );

        int32           getCurrentPlayer() const { return _currentPlayer; }
        int32           getDirection() const { return _direction; }
        UnoColor        getCurrentColor() const { return _color; }
        int32           getPendingDraw() const { return _pendingDraw; }
        Card            getTopCard() const { return _discardPile.getTop(); }
        int32           getPlayerCount() const { return static_cast<int32>( _listHand.size() ); }
        const CardPile& getHand( int32 player ) const { return _listHand[static_cast<size_t>( player )]; }
        const CardPile& getDrawPile() const { return _drawPile; }
        /** @brief 이긴 사람입니다. 아직이면 −1 입니다. */
        int32 getWinner() const { return _winner; }
        void  drainEvents( vector<UnoEvent>& outListEvent );

    private:
        int32 findNextPlayer( int32 player, int32 steps ) const;
        int32 drawInto( int32 player, int32 count );
        bool  hasColor( int32 player, UnoColor color, uint16 exceptCardId ) const;

        vector<CardPile>      _listHand;
        EventBuffer<UnoEvent> _eventBuffer;
        CardPile              _drawPile;
        CardPile              _discardPile;
        UnoSettings           _settings;
        GameRandom            _random;
        int32                 _currentPlayer;
        int32                 _direction;
        int32                 _pendingDraw;
        int32                 _unoTarget; ///< 우노를 외치지 않고 한 장 남긴 사람(−1 = 없음)
        int32                 _winner;
        UnoColor              _color;
    };
} // namespace sw
