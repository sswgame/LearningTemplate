/**
 * @file Wallet.h
 * @brief 통화를 여러 개 가진 지갑입니다. 가게(`Shop.h`)와 따로 둔다 — 지갑만 쓰는 게임 모드 · 키트가 가게 카탈로그 · 인벤토리 · 조건식 헤더를 끌어오지 않게.
 * @details 구현은 가게와 같은 `Shop.cpp` 에 있다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/Event/EventBuffer.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 지갑의 통화 하나입니다. */
    REFLECT()
    struct WalletBalance
    {
        REFLECT_BODY();

        PROPERTY()
        hashed_string _currency{};
        PROPERTY()
        int64 _amount{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 지갑의 변화 하나입니다. 화면의 "+20 Gold" 같은 표시에 씁니다. */
    struct WalletEvent
    {
        hashed_string _currency{};
        int64         _delta{ 0 };   ///< 더한 값(쓰면 음수)
        int64         _balance{ 0 }; ///< 바뀐 뒤 잔액
    };
} // namespace sw

namespace sw
{
    /**
     * @class Wallet
     * @brief 통화 여러 개(이름 → int64)를 담습니다. 통화 이름은 데이터이므로 장르마다 정합니다("Gold", "Credits", "Souls", "Tickets").
     * @details 잔액이 음수(빚)가 되는 것은 `charge` 로만 가능하다. 쓰기는 `trySpend` 로 하고, 잔액이 모자라거나 빚이 있으면 아무것도 바꾸지 않는다. 통화 순서는 처음 만난 순서이다. 화면과 세이브가 같은 순서를 쓰도록 결정적으로 정한 것이다.
     */
    REFLECT()
    class SW_GF_API Wallet
    {
    public:
        REFLECT_BODY();

        static constexpr uint32 kStateTag     = FourCCUtil::make( "WALT" );
        static constexpr uint32 kStateVersion = 2; ///< 2: 리플렉션 상태 칸(판 1 손 본문은 머리를 붙여 올린다 — `ReflectedStateUpgrade::prependHead`)

        /** @brief 통화를 적지 않으면 쓰는 기본 통화 "Gold" 입니다. */
        static hashed_string getDefaultCurrency();

        /** @brief @p amount 를 더합니다. 0 보다 커야 합니다. */
        void add( const hashed_string& currency, int64 amount );
        /** @brief 잔액이 @p amount 이상이면 빼고 true 를 반환합니다. 모자라면 그대로 두고 false 를 반환합니다. */
        [[nodiscard]] bool trySpend( const hashed_string& currency, int64 amount );
        /** @brief @p amount 를 거절 없이 뺍니다. 0 보다 커야 합니다. 운영비나 급여처럼 미룰 수 없는 지출에 쓰며, 잔액이 음수(빚)가 될 수 있습니다. 빚이 있으면 `trySpend` 는 거절됩니다. */
        void charge( const hashed_string& currency, int64 amount );
        /** @brief 잔액을 @p amount 로 설정합니다. 세이브 불러오기와 치트에 씁니다. 이벤트는 내지 않습니다. */
        void setBalance( const hashed_string& currency, int64 amount );
        void clear();
        /** @brief 쌓인 변화를 넘겨 주고 비웁니다. */
        void pollEvents( vector<WalletEvent>& outListEvent );
        /**
         * @brief 통화마다 이름과 잔액을 처음 만난 순서로 씁니다(리플렉션 상태 칸).
         *        `writeState` 가 쓴 바이트로 모두 복원합니다. 알림은 없습니다. 통화 이름이 비어 있으면 false 를 반환하고 상태는 바뀌지 않습니다. 음수 잔액(빚)도 그대로 복원합니다.
         */
        SW_DECLARE_STATE_IO();

        int64                        getBalance( const hashed_string& currency ) const;
        bool                         canAfford( const hashed_string& currency, int64 amount ) const { return amount <= getBalance( currency ); }
        const vector<WalletBalance>& getBalances() const { return _listBalance; }
        uint32                       getRevision() const { return _revision; }

    private:
        WalletBalance& getOrCreateBalance( const hashed_string& currency );

    private:
        PROPERTY( RuntimeState )
        vector<WalletBalance>           _listBalance{};
        NotificationBuffer<WalletEvent> _eventBuffer{};
        uint32                          _revision{ 0 };
    };
} // namespace sw
