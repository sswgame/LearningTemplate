/**
 * @file UnoGame.h
 * @brief 우노를 구현합니다. 숫자, 스킵, 리버스, +2, 와일드, 와일드 +4 카드를 다루고, 낼 수 있는 카드 판정, 차례 방향, 우노 선언 누락 벌칙, +2 쌓기(설정)를 지원합니다.
 * @details 카드는 `Card::_suit` 가 색(`UnoColor`)이고 `Card::_rank` 가 값(`UnoValue`)이다. 덱은 108장이며, 색마다 0 이 한 장, 1..9 와 스킵, 리버스, +2 가 두 장씩,
 *          와일드와 와일드 +4 가 네 장씩이다. 카드를 뽑으면 차례가 넘어간다(뽑은 패를 바로 내는 규칙은 없다).
 *          한 장을 남기며 우노를 외치지 않은 사람은 다음 사람이 행동하기 전까지 다른 사람이 `callUno` 로 잡을 수 있고(벌칙 장수만큼 뽑는다),
 *          본인이 먼저 `callUno` 하면 늦은 선언으로 처리한다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/EventBuffer.h"
#include "Core/Container/vector.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Casual/CardGame/CardDeck.h"

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
        Wild ///< 와일드 카드의 색이거나 "고르지 않음" 을 뜻한다
    };

    /** @brief 값입니다(0..9 는 숫자 그대로입니다). */
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

    /**
     * @brief 규칙 설정입니다.
     * @details 범위는 필드의 `PROPERTY( Min · Max )` 이고, 벗어난 값은 `UnoGame::initialize*` 가 오류로 거절한다(`ConfigRangeUtil`). 인원 × 손패 + 1 이 덱(108장)을 넘는 짝도 거절한다.
     *          값을 조용히 맞추지 않는다(폴백 금지 원칙).
     */
    REFLECT()
    struct SW_KIT_API UnoSettings
    {
        REFLECT_BODY();
        PROPERTY( Min = 2, Max = 10 )
        int32 _playerCount{ 4 };
        PROPERTY( Min = 1 )
        int32 _handSize{ 7 };
        PROPERTY( Min = 0 )
        int32 _unoPenalty{ 2 };                ///< 우노를 외치지 않아 잡히면 뽑는 장수
        uint8 _bStackDrawTwo{ SW_FALSE };      ///< +2 위에 +2 를 쌓아 넘길 수 있다
        uint8 _bStackDrawFour{ SW_FALSE };     ///< 쌓인 벌칙 위에 +4 를 쌓아 넘길 수 있다
        uint8 _bStrictWildDrawFour{ SW_TRUE }; ///< 지금 색의 카드가 있으면 +4 를 낼 수 없다
    };
} // namespace sw

namespace sw
{
    /** @brief 행동의 종류입니다(`CardAction::_kind`). */
    enum class UnoActionKind : uint8
    {
        Play = 0, ///< `_cardID` 는 낼 카드이고, 와일드이면 `_targetID` 가 고른 색이다. `_amount` 가 0 이 아니면 우노 선언이다
        Draw,
        CallUno
    };

    /** @brief 판에서 일어난 일을 알리는 이벤트입니다. */
    struct UnoEvent
    {
        enum class Kind : uint8
        {
            Played = 0, ///< `_cardID` 가 낸 카드이다
            Drew,       ///< `_value` 는 뽑은 장수이다
            Skipped,
            Reversed,
            ColorChosen, ///< `_value` 는 UnoColor 이다
            UnoDeclared,
            UnoPenalty, ///< `_player` 는 벌칙을 받은 사람이고 `_value` 는 장수이다
            Reshuffled,
            Won
        };
        int32  _player{ -1 };
        int32  _value{ 0 };
        uint16 _cardID{ Card::kNoCard };
        Kind   _kind{ Kind::Played };
    };
} // namespace sw

namespace sw
{
    /** @brief 판의 시작 배치입니다(테스트와 리플레이용). */
    struct UnoLayout
    {
        vector<CardPile> _listHand{};
        CardPile         _drawPile{};    ///< 맨 위(끝)부터 뽑는다
        CardPile         _discardPile{}; ///< 맨 위가 지금 낼 기준이 된다
        int32            _firstPlayer{ 0 };
        UnoColor         _color{ UnoColor::Red };
    };
} // namespace sw

namespace sw
{
    /**
     * @class UnoGame
     * @brief 우노 한 판을 진행합니다. 결과가 결정적입니다. 뽑을 더미가 떨어지면 버린 더미를 같은 난수로 섞습니다.
     */
    class SW_KIT_API UnoGame
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "CUNO" );
        static constexpr uint32 kStateVersion = 1;

        UnoGame();

        /**
         * @brief 108장을 @p seed 로 섞어 한 장씩 돌려 나누고, 와일드가 아닌 첫 장을 뒤집습니다(첫 장의 효과는 적용하지 않습니다).
         * @return 설정이 범위 안이면 true 입니다. 밖이면 오류를 남기고 false 이며 상태를 바꾸지 않습니다.
         */
        [[nodiscard]] bool initialize( const UnoSettings& settings, uint32 seed );
        /** @brief 정해진 배치로 시작합니다. 설정이 범위 밖이거나 손패 수가 인원과 다르거나 첫 차례가 인원 밖이면 오류를 남기고 false 이며 상태를 바꾸지 않습니다. */
        [[nodiscard]] bool initializeFromLayout( const UnoSettings& settings, const UnoLayout& layout, uint32 seed = GameRandom::kDefaultSeed );

        /** @brief 차례인 @p player 가 낼 수 있는 패 @p cardID 를 냅니다. 와일드는 @p chosenColor 가 네 색 중 하나여야 합니다. */
        [[nodiscard]] bool playCard( int32 player, uint16 cardID, UnoColor chosenColor = UnoColor::Wild, bool bDeclareUno = false );
        /** @brief 차례인 @p player 가 카드를 뽑습니다. 쌓인 벌칙이 있으면 그만큼, 없으면 한 장을 뽑으며, 차례가 넘어갑니다. */
        [[nodiscard]] bool drawCard( int32 player );
        /** @brief 우노를 잡거나(다른 사람) 늦게 선언합니다(본인). 잡을 사람이 없으면 false 를 반환합니다. */
        [[nodiscard]] bool callUno( int32 caller );
        [[nodiscard]] bool applyAction( int32 player, const CardAction& action );

        /** @brief 손패, 뽑을 더미, 버린 더미, 난수, 차례, 방향, 쌓인 벌칙, 우노 대상, 이긴 사람, 색을 씁니다. 설정은 `initialize` 에서 받으므로 저장하지 않고, 이벤트는 읽을 때 비웁니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 가 쓴 바이트를 읽어 상태를 복원합니다. 인원이 다르거나 데이터가 깨졌으면 false 를 반환하고 상태는 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

        /** @brief 지금 낼 수 있는 카드인지 반환합니다. 색, 값, 와일드를 따지며, 벌칙이 쌓였으면 쌓을 수 있는 카드만 낼 수 있습니다. */
        bool isPlayable( const Card& card ) const;
        /** @brief 108장(번호 0..107)을 채웁니다. */
        static void makeDeck( CardPile& outPile );
        /** @brief 손패 점수를 계산합니다. 숫자는 그 수, 스킵과 리버스와 +2 는 20, 와일드는 50 입니다. 이긴 사람이 남의 손패 점수를 받습니다. */
        static int32 computeHandPoints( const CardPile& hand );

        int32           getCurrentPlayer() const { return _currentPlayer; }
        int32           getDirection() const { return _direction; }
        UnoColor        getCurrentColor() const { return _color; }
        int32           getPendingDraw() const { return _pendingDraw; }
        Card            getTopCard() const { return _discardPile.getTop(); }
        int32           getPlayerCount() const { return static_cast<int32>( _listHand.size() ); }
        const CardPile& getHand( int32 player ) const { return _listHand[static_cast<size_t>( player )]; }
        const CardPile& getDrawPile() const { return _drawPile; }
        /** @brief 이긴 사람을 반환합니다. 아직 없으면 -1 입니다. */
        int32 getWinner() const { return _winner; }
        void  drainEvents( vector<UnoEvent>& outListEvent );

    private:
        int32 findNextPlayer( int32 player, int32 steps ) const;
        int32 drawInto( int32 player, int32 count );
        bool  hasColor( int32 player, UnoColor color, uint16 exceptCardID ) const;

        vector<CardPile>      _listHand;
        EventBuffer<UnoEvent> _eventBuffer;
        CardPile              _drawPile;
        CardPile              _discardPile;
        UnoSettings           _settings;
        GameRandom            _random;
        int32                 _currentPlayer;
        int32                 _direction;
        int32                 _pendingDraw;
        int32                 _unoTarget; ///< 우노를 외치지 않고 한 장을 남긴 사람이다(-1 이면 없다)
        int32                 _winner;
        UnoColor              _color;
    };
} // namespace sw
