/**
 * @file PokerTable.h
 * @brief 텍사스 홀덤 한 판 — 블라인드 · 체크 · 콜 · 레이즈(최소 증액) · 폴드 · 올인, 스트리트 진행, 쇼다운과 사이드 팟 분배입니다.
 * @details 규칙만 있는 상태 클래스입니다(엔진 · 씬 없음). 네트워크 게임은 행동을 `CardActionUtil` 로 바이트로 만들어 턴 중계에 싣고,
 *          받은 쪽에서 `applyAction` 으로 같은 순서대로 넣으면 모두 같은 판이 됩니다(덱은 같은 씨앗으로 섞는다).
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Foundation/Utility/EventBuffer.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Casual/CardGame/CardDeck.h"
#include "GameFramework/Kits/Casual/CardGame/PokerHand.h"

namespace sw
{
    class Archive;

    /** @brief 스트리트입니다. */
    enum class PokerStreet : uint8
    {
        Preflop = 0,
        Flop,
        Turn,
        River,
        HandOver ///< 판이 끝났다(쇼다운 또는 한 명만 남음) — 다음 `startHand` 를 기다린다
    };

    /** @brief 행동 종류입니다(`CardAction::_kind`). */
    enum class PokerActionKind : uint8
    {
        Fold = 0,
        Check,
        Call,
        Raise, ///< `_amount` = 이번 스트리트에 낼 총액(레이즈 투)
        AllIn
    };

    /** @brief 판 설정입니다. */
    struct PokerSettings
    {
        int32 _smallBlind{ 1 };
        int32 _bigBlind{ 2 };
    };
} // namespace sw

namespace sw
{
    /** @brief 자리 하나입니다. */
    struct PokerSeat
    {
        Card  _arrHole[2]{};
        int32 _stack{ 0 };       ///< 앞에 남은 칩
        int32 _committed{ 0 };   ///< 이번 스트리트에 낸 것
        int32 _contributed{ 0 }; ///< 이번 판에 낸 것 모두(팟 계산)
        int32 _won{ 0 };         ///< 지난 판에 받은 것
        uint8 _bInHand{ SW_FALSE };
        uint8 _bFolded{ SW_FALSE };
        uint8 _bAllIn{ SW_FALSE };
        uint8 _bActed{ SW_FALSE }; ///< 마지막 레이즈 뒤에 행동했는가
    };
} // namespace sw

namespace sw
{
    /** @brief 팟 하나(메인 또는 사이드)입니다. */
    struct PokerPot
    {
        vector<int32> _listEligibleSeat{}; ///< 이 팟을 가져갈 수 있는 자리(폴드 안 했고 이만큼 냈다)
        int32         _amount{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 팟 계산 · 분배입니다. 테이블 없이도 씁니다. */
    struct SW_GF_API PokerPotUtil
    {
        /**
         * @brief 자리별 낸 금액에서 메인 · 사이드 팟을 만듭니다. 폴드한 자리의 칩은 들어가지만 가져갈 수 없습니다.
         * @details 가져갈 사람이 같은 이웃 팟은 하나로 합칩니다. 아무도 가져갈 수 없는 꼭대기(폴드한 사람만 낸 몫)는 아래 팟에 붙습니다.
         */
        static void makePots( const vector<int32>& listContribution, const vector<uint8>& listFolded, vector<PokerPot>& outListPot );
        /**
         * @brief 팟마다 가장 높은 점수(`PokerHandValue::_score`)의 자리들이 나눠 갖습니다. 나눠지지 않는 칩은 @p firstSeat 부터
         *        자리 순서로 한 개씩입니다(버튼 왼쪽부터). @p outListWon 은 자리별로 받은 칩입니다.
         */
        static void distributePots( const vector<PokerPot>& listPot, const vector<uint32>& listScore, int32 firstSeat, vector<int32>& outListWon );
    };
} // namespace sw

namespace sw
{
    /** @brief 테이블에서 생긴 일입니다. */
    struct PokerEvent
    {
        enum class Kind : uint8
        {
            Blind = 0, ///< _seat · _amount
            Fold,
            Check,
            Call,   ///< _amount = 이번에 더 낸 것
            Raise,  ///< _amount = 레이즈 투
            AllIn,  ///< _amount = 레이즈 투(콜에 못 미칠 수도)
            Street, ///< _amount = 새 스트리트(PokerStreet)
            Win     ///< _seat · _amount = 받은 칩
        };
        int32 _seat{ -1 };
        int32 _amount{ 0 };
        Kind  _kind{ Kind::Blind };
    };
} // namespace sw

namespace sw
{
    /**
     * @class PokerTable
     * @brief 한 테이블입니다. 자리마다 칩을 들고 시작하고, `startHand` 로 판을 열어 차례인 자리가 `act` 합니다.
     * @details 버튼은 판마다 칩이 있는 다음 자리로 갑니다. 둘이면 버튼이 스몰 블라인드이고 프리플롭에 먼저 행동합니다.
     *          최소 레이즈는 "직전 레이즈 크기만큼 더"(처음엔 빅 블라인드)입니다. 그보다 작은 올인은 받아 주되 최소 증액을 바꾸지 않습니다
     *          (단순화: 이미 행동한 사람도 다시 레이즈할 수 있다). 카드를 태우지(burn) 않습니다.
     */
    class SW_GF_API PokerTable
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "CPKR" );
        static constexpr uint32 kStateVersion = 1;

        PokerTable();

        /** @brief 자리 수 = @p listStack 의 길이입니다. 버튼은 마지막 자리에서 시작해 첫 판에 0 번이 됩니다. */
        void initialize( const PokerSettings& settings, const vector<int32>& listStack );
        /** @brief 덱을 섞어 판을 엽니다. 칩이 있는 자리가 둘 미만이면 false 입니다. */
        [[nodiscard]] bool startHand( GameRandom& random );
        /** @brief 주어진 덱(맨 위부터 나눈다)으로 판을 엽니다 — 시험 · 리플레이용입니다. */
        [[nodiscard]] bool startHandWithDeck( const CardPile& deck );

        /** @brief 차례인 @p seat 의 행동입니다. 규칙에 어긋나면(차례 아님 · 체크 불가 · 최소 레이즈 미달 · 칩 부족) false 이고 아무것도 바뀌지 않습니다. */
        [[nodiscard]] bool act( int32 seat, PokerActionKind kind, int32 amount = 0 );
        /** @brief 중계로 받은 행동입니다(`_kind` = PokerActionKind, `_amount`). */
        [[nodiscard]] bool applyAction( int32 seat, const CardAction& action );

        /** @brief 자리(홀 카드 · 칩 · 낸 것 · 표시) · 지난 팟 · 덱 · 보드 · 버튼 · 차례 · 베팅 · 스트리트를 씁니다. 설정은 `initialize` 의 것이라 싣지 않고, 알림은 읽을 때 비웁니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 자리 수가 다르거나 깨졌으면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

        int32            getSeatCount() const { return static_cast<int32>( _listSeat.size() ); }
        const PokerSeat& getSeat( int32 seat ) const { return _listSeat[static_cast<size_t>( seat )]; }
        PokerStreet      getStreet() const { return _street; }
        int32            getCurrentSeat() const { return _currentSeat; }
        int32            getButton() const { return _button; }
        int32            getCurrentBet() const { return _currentBet; }
        /** @brief 지금 레이즈하려면 최소 이만큼(레이즈 투)입니다. */
        int32           getMinRaiseTo() const { return _currentBet + _lastRaiseSize; }
        int32           computePotTotal() const;
        const CardPile& getBoard() const { return _board; }
        /** @brief 지난 쇼다운(또는 혼자 남음)의 팟입니다. */
        const vector<PokerPot>& getLastPots() const { return _listLastPot; }
        void                    drainEvents( vector<PokerEvent>& outListEvent );

    private:
        [[nodiscard]] bool openHand();
        int32              findNextSeat( int32 fromSeat, bool bCanActOnly ) const;
        bool               canAct( int32 seat ) const;
        bool               needsAction( int32 seat ) const;
        void               commitChips( int32 seat, int32 amount );
        void               afterAction( int32 seat );
        void               beginStreet( PokerStreet street );
        void               dealBoard( int32 count );
        void               finishShowdown();
        void               finishUncontested( int32 winnerSeat );

        vector<PokerSeat>       _listSeat;
        vector<PokerPot>        _listLastPot;
        EventBuffer<PokerEvent> _eventBuffer;
        CardPile                _deck;
        CardPile                _board;
        PokerSettings           _settings;
        int32                   _button;
        int32                   _currentSeat;
        int32                   _currentBet;
        int32                   _lastRaiseSize;
        PokerStreet             _street;
    };
} // namespace sw
