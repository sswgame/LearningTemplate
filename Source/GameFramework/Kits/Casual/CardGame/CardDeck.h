/**
 * @file CardDeck.h
 * @brief 카드 게임 공통 — 카드 한 장(`Card`), 더미(`CardPile` — 덱 · 버린 더미 · 손패), 씨앗 고정 셔플(Fisher-Yates + `GameRandom`) · 뽑기 · 나누기,
 *        그리고 턴 중계(`GF_NetTurnRelay`)에 실을 행동 바이트(`CardAction` · `CardActionUtil`)입니다.
 * @details 카드는 게임마다 뜻이 다른 두 칸(`_suit` · `_rank`)과 덱 안의 고유 번호(`_id`)뿐입니다. 트럼프는 무늬 · 숫자(에이스 = 1),
 *          화투는 종류 · 월, 우노는 색 · 값입니다. 더 붙는 속성(띠 색 · 고도리 등)은 게임이 `_id` 로 표를 찾습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Utility/GameRandom.h"

namespace sw
{
    /** @brief 카드 한 장입니다. 같은 덱 안에서 `_id` 는 하나뿐입니다. */
    struct Card
    {
        static constexpr uint16 kNoCard = 0xFFFFu; ///< "카드 없음" 번호

        uint16 _id{ kNoCard };
        uint8  _suit{ 0 }; ///< 트럼프 무늬 · 화투 종류 · 우노 색
        uint8  _rank{ 0 }; ///< 트럼프 숫자(에이스 = 1, 킹 = 13) · 화투 월(1..12) · 우노 값

        bool isValid() const { return _id != kNoCard; }
        bool operator==( const Card& other ) const { return _id == other._id && _suit == other._suit && _rank == other._rank; }
        bool operator!=( const Card& other ) const { return ( *this == other ) == false; }
    };
} // namespace sw

namespace sw
{
    /** @brief 트럼프 무늬입니다(다이아 · 하트가 빨강). */
    struct StandardSuit
    {
        static constexpr uint8 kClubs    = 0;
        static constexpr uint8 kDiamonds = 1;
        static constexpr uint8 kHearts   = 2;
        static constexpr uint8 kSpades   = 3;
        static constexpr uint8 kJoker    = 4;
        static constexpr int32 kCount    = 4;
    };
} // namespace sw

namespace sw
{
    /** @brief 트럼프 숫자입니다. */
    struct StandardRank
    {
        static constexpr uint8 kAce   = 1;
        static constexpr uint8 kJack  = 11;
        static constexpr uint8 kQueen = 12;
        static constexpr uint8 kKing  = 13;
    };
} // namespace sw

namespace sw
{
    /**
     * @class CardPile
     * @brief 카드 더미입니다 — 덱 · 버린 더미 · 손패 · 바닥 모두 이것입니다. 맨 위(뽑는 쪽)는 목록의 끝입니다.
     */
    class SW_GF_API CardPile
    {
    public:
        CardPile();

        void clear() { _listCard.clear(); }
        /** @brief 맨 위에 얹습니다. */
        void push( const Card& card ) { _listCard.push_back( card ); }
        /** @brief 맨 아래에 끼웁니다. */
        void pushBottom( const Card& card );
        /** @brief 맨 위 한 장을 뽑습니다. 비었으면 false 입니다. */
        [[nodiscard]] bool draw( Card& outCard );
        /** @brief 맨 위에서 @p count 장을 @p target 위로 옮깁니다(한 장씩 — 순서가 뒤집힌다). 옮긴 수입니다. */
        int32 drawInto( CardPile& target, int32 count );
        /** @brief 번호가 @p cardId 인 카드를 빼냅니다. 없으면 false 입니다. */
        [[nodiscard]] bool takeById( uint16 cardId, Card& outCard );
        /** @brief 자리 @p index 의 카드를 뺍니다(위아래 순서는 그대로). */
        Card removeAt( int32 index );
        /** @brief 번호가 @p cardId 인 카드의 자리입니다. 없으면 −1 입니다. */
        int32 findIndexById( uint16 cardId ) const;
        bool  containsId( uint16 cardId ) const { return findIndexById( cardId ) >= 0; }

        /** @brief Fisher-Yates 셔플입니다. 같은 씨앗의 @p random 이면 같은 순서가 나옵니다(플랫폼 무관). */
        void shuffle( GameRandom& random );
        /**
         * @brief 맨 위부터 한 장씩 돌아가며 @p inoutListHand 의 손패마다 @p cardsEach 장을 나눕니다(0 번 손패가 먼저).
         * @return 덱이 모자라 덜 나눴으면 false 입니다.
         */
        [[nodiscard]] bool deal( vector<CardPile>& inoutListHand, int32 cardsEach );

        int32       getCount() const { return static_cast<int32>( _listCard.size() ); }
        bool        isEmpty() const { return _listCard.empty(); }
        const Card& getAt( int32 index ) const { return _listCard[static_cast<size_t>( index )]; }
        /** @brief 맨 위 카드입니다. 비었으면 빈 카드(`isValid() == false`)입니다. */
        Card                getTop() const { return _listCard.empty() ? Card{} : _listCard.back(); }
        const vector<Card>& getCards() const { return _listCard; }

    private:
        vector<Card> _listCard; ///< [0] = 맨 아래, 끝 = 맨 위
    };
} // namespace sw

namespace sw
{
    /** @brief 트럼프 덱 도우미입니다. */
    struct SW_GF_API CardDeckUtil
    {
        /** @brief 52 장(무늬 순 · 숫자 순, 번호 0..51)과 조커 @p jokerCount 장(번호 52..)을 @p outPile 에 채웁니다. 섞지 않습니다. */
        static void makeStandardDeck( CardPile& outPile, int32 jokerCount = 0 );
        static bool isRedSuit( uint8 suit ) { return suit == StandardSuit::kDiamonds || suit == StandardSuit::kHearts; }
        /** @brief @p listCardInDrawOrder 의 첫 카드가 가장 먼저 뽑히는 더미를 만듭니다(시험 · 리플레이용 덱). */
        static void makePileInDrawOrder( const vector<Card>& listCardInDrawOrder, CardPile& outPile );
    };
} // namespace sw

namespace sw
{
    /**
     * @struct CardAction
     * @brief 한 게임의 행동 하나입니다. 뜻은 게임이 정합니다(`_kind` = 게임의 행동 enum, 카드 · 대상 · 금액).
     * @details 포커: 종류 + 금액, 맞고: 낸 카드 + 고를 바닥 카드, 우노: 낸 카드 + 고른 색(`_targetId`) + 우노 선언(`_amount`).
     */
    struct CardAction
    {
        int32  _amount{ 0 };
        uint16 _cardId{ Card::kNoCard };
        uint16 _targetId{ Card::kNoCard };
        uint8  _kind{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct CardActionUtil
     * @brief `CardAction` ↔ 바이트입니다. `TurnRelayClient::submitAction` 에 그대로 싣고, 받은 `TurnAction::_buffer` 를 되돌립니다.
     * @details 꼴: [표지 0xCA][종류][카드 번호 LE16][대상 LE16][금액 LE32] — 10 바이트, 플랫폼 무관(바이트 단위로 적는다).
     */
    struct SW_GF_API CardActionUtil
    {
        static constexpr uint8 kTag         = 0xCAu;
        static constexpr int32 kEncodedSize = 10;

        static void encodeAction( const CardAction& action, vector<uint8>& outBuffer );
        /** @brief 크기 · 표지가 맞지 않으면 false 입니다. */
        [[nodiscard]] static bool decodeAction( const vector<uint8>& buffer, CardAction& outAction );
    };
} // namespace sw
