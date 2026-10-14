/**
 * @file CardDeck.h
 * @brief 카드 게임 공통 요소를 담습니다. 카드 한 장(`Card`), 카드 더미(`CardPile`, 덱, 버린 더미, 손패), 시드를 고정한 셔플(Fisher-Yates + `GameRandom`), 뽑기, 나누기,
 *        턴 중계(`GF_NetTurnRelay`)에 실을 행동 바이트(`CardAction`, `CardActionUtil`)를 제공합니다.
 * @details 카드는 게임마다 뜻이 다른 두 필드(`_suit`, `_rank`)와 덱 안의 고유 번호(`_id`)만 가진다. 트럼프는 무늬와 숫자(에이스 = 1),
 *          화투는 종류와 월, 우노는 색과 값이다. 띠 색이나 고도리 같은 속성은 게임이 `_id` 로 별도 테이블에서 찾는다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Archive;

    /** @brief 카드 한 장입니다. 같은 덱 안에서 `_id` 는 하나뿐입니다. */
    struct Card
    {
        static constexpr uint16 kNoCard = 0xFFFFu; ///< "카드 없음" 을 나타내는 번호이다

        uint16 _id{ kNoCard };
        uint8  _suit{ 0 }; ///< 트럼프는 무늬, 화투는 종류, 우노는 색이다
        uint8  _rank{ 0 }; ///< 트럼프는 숫자(에이스 = 1, 킹 = 13), 화투는 월(1..12), 우노는 값이다

        bool isValid() const { return _id != kNoCard; }
        bool operator==( const Card& other ) const { return _id == other._id && _suit == other._suit && _rank == other._rank; }
        bool operator!=( const Card& other ) const { return ( *this == other ) == false; }
    };
} // namespace sw

namespace sw
{
    /** @brief 트럼프 무늬입니다. 다이아와 하트가 빨강입니다. */
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
     * @brief 카드 더미입니다. 덱, 버린 더미, 손패, 바닥이 모두 이 타입입니다. 맨 위(뽑는 쪽)는 목록의 끝입니다.
     */
    class SW_KIT_API CardPile
    {
    public:
        CardPile();

        void clear() { _listCard.clear(); }
        /** @brief 맨 위에 올립니다. */
        void push( const Card& card ) { _listCard.push_back( card ); }
        /** @brief 맨 아래에 끼웁니다. */
        void pushBottom( const Card& card );
        /** @brief 맨 위 한 장을 뽑습니다. 비었으면 false 를 반환합니다. */
        [[nodiscard]] bool draw( Card& outCard );
        /** @brief 맨 위에서 @p count 장을 @p target 위로 옮깁니다. 한 장씩 옮기므로 순서가 뒤집힙니다. 옮긴 수를 반환합니다. */
        int32 drawInto( CardPile& target, int32 count );
        /** @brief 번호가 @p cardID 인 카드를 빼냅니다. 없으면 false 를 반환합니다. */
        [[nodiscard]] bool takeByID( uint16 cardID, Card& outCard );
        /** @brief index @p index 의 카드를 뺍니다. 나머지의 위아래 순서는 그대로입니다. */
        Card removeAt( int32 index );
        /** @brief 번호가 @p cardID 인 카드의 index 를 반환합니다. 없으면 -1 입니다. */
        int32 findIndexByID( uint16 cardID ) const;
        bool  containsID( uint16 cardID ) const { return findIndexByID( cardID ) >= 0; }

        /** @brief Fisher-Yates 셔플을 합니다. 같은 시드의 @p random 이면 플랫폼과 무관하게 같은 순서가 나옵니다. */
        void shuffle( GameRandom& random );
        /**
         * @brief 맨 위부터 한 장씩 돌아가며 @p inoutListHand 의 손패마다 @p cardsEach 장을 나눕니다(0번 손패가 먼저).
         * @return 덱이 모자라 덜 나눴으면 false 를 반환한다.
         */
        [[nodiscard]] bool deal( vector<CardPile>& inoutListHand, int32 cardsEach );

        int32       getCount() const { return static_cast<int32>( _listCard.size() ); }
        bool        isEmpty() const { return _listCard.empty(); }
        const Card& getAt( int32 index ) const { return _listCard[static_cast<size_t>( index )]; }
        /** @brief 맨 위 카드를 반환합니다. 비었으면 빈 카드(`isValid() == false`)를 반환합니다. */
        Card                getTop() const { return _listCard.empty() ? Card{} : _listCard.back(); }
        const vector<Card>& getCards() const { return _listCard; }

        /** @brief 장수와, 카드마다 번호, 무늬, 숫자를 맨 아래부터 씁니다. 카드 게임 키트 상태에서 더미 하나에 해당합니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 가 쓴 바이트를 읽어 상태를 복원합니다. 데이터가 깨졌으면 false 를 반환하고 상태는 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        vector<Card> _listCard; ///< [0] 이 맨 아래이고 끝이 맨 위이다
    };
} // namespace sw

namespace sw
{
    /** @brief 트럼프 덱 도우미입니다. */
    struct SW_KIT_API CardDeckUtil
    {
        /** @brief 52장(무늬 순, 숫자 순, 번호 0..51)과 조커 @p jokerCount 장(번호 52부터)을 @p outPile 에 채웁니다. 섞지는 않습니다. */
        static void makeStandardDeck( CardPile& outPile, int32 jokerCount = 0 );
        static bool isRedSuit( uint8 suit ) { return suit == StandardSuit::kDiamonds || suit == StandardSuit::kHearts; }
        /** @brief @p listCardInDrawOrder 의 첫 카드가 가장 먼저 뽑히는 더미를 만듭니다(테스트와 리플레이용 덱). */
        static void makePileInDrawOrder( const vector<Card>& listCardInDrawOrder, CardPile& outPile );
    };
} // namespace sw

namespace sw
{
    /**
     * @struct CardAction
     * @brief 한 게임의 행동 하나입니다. 뜻은 게임이 정합니다(`_kind` 는 게임의 행동 enum 이고, 카드, 대상, 금액을 함께 가진다).
     * @details 포커는 종류와 금액을, 맞고는 낸 카드와 고를 바닥 카드를, 우노는 낸 카드와 고른 색(`_targetID`)과 우노 선언(`_amount`)을 쓴다.
     */
    struct CardAction
    {
        int32  _amount{ 0 };
        uint16 _cardID{ Card::kNoCard };
        uint16 _targetID{ Card::kNoCard };
        uint8  _kind{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct CardActionUtil
     * @brief `CardAction` 과 바이트 사이를 변환합니다. `TurnRelayClient::submitAction` 에 그대로 실어 보내고, 받은 `TurnAction::_buffer` 를 다시 `CardAction` 으로 되돌립니다.
     * @details 레이아웃은 [태그 0xCA][종류][카드 번호 LE16][대상 LE16][금액 LE32] 로 10바이트이다. 바이트 단위로 적으므로 플랫폼과 무관하다.
     */
    struct SW_KIT_API CardActionUtil
    {
        static constexpr uint8 kTag         = 0xCAu;
        static constexpr int32 kEncodedSize = 10;

        static void encodeAction( const CardAction& action, vector<uint8>& outBuffer );
        /** @brief 크기나 태그가 맞지 않으면 false 를 반환합니다. */
        [[nodiscard]] static bool decodeAction( const vector<uint8>& buffer, CardAction& outAction );
    };
} // namespace sw
