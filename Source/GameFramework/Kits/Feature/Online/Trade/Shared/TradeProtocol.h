/**
 * @file TradeProtocol.h
 * @brief 거래 키트의 와이어 — 메서드 · 알림 번호(영역 `OnlineMethodRange::kTrade`)와 응답 몸 형식입니다.
 * @details 응답 몸 = `TradeResult` + 스냅숏 + (정산이면) 요청한 계정의 이동 뒤 잔액(자산 id · 양 — 게임이 지갑 · 인벤토리 거울에 넣는다).
 *          알림 몸 = 스냅숏(+ 정산이면 그 계정의 잔액). 기능 플래그 `feature.trade_enabled` 가 꺼져 있으면 모든 요청이 `kFeatureDisabled`.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Trade/Shared/TradeTypes.h"

namespace sw
{
    class BitReader;
    class BitWriter;

    /** @brief 거래 키트 메서드 · 알림 번호입니다. */
    struct TradeMethod
    {
        static constexpr uint16 kInvite      = OnlineMethodRange::kTrade + 0x01; ///< 상대 표시 이름
        static constexpr uint16 kRespond     = OnlineMethodRange::kTrade + 0x02; ///< 거래 id · 수락
        static constexpr uint16 kSetOffer    = OnlineMethodRange::kTrade + 0x03; ///< 거래 id · 다리
        static constexpr uint16 kLock        = OnlineMethodRange::kTrade + 0x04; ///< 거래 id
        static constexpr uint16 kConfirm     = OnlineMethodRange::kTrade + 0x05; ///< 거래 id · 본 내 판 · 본 상대 판
        static constexpr uint16 kCancel      = OnlineMethodRange::kTrade + 0x06; ///< 거래 id
        static constexpr uint16 kPushInvited = OnlineMethodRange::kTrade + 0x80; ///< 초대받았다 — 스냅숏
        static constexpr uint16 kPushUpdate  = OnlineMethodRange::kTrade + 0x81; ///< 바뀌었다 — 스냅숏
        static constexpr uint16 kPushClosed  = OnlineMethodRange::kTrade + 0x82; ///< 닫혔다 — 스냅숏(+ 정산 잔액)

        static_assert( OnlineMethodRange::isInRange( kCancel, OnlineMethodRange::kTrade ) && OnlineMethodRange::isMethod( kCancel ) );
        static_assert( OnlineMethodRange::isMethod( kPushClosed ) == false );
    };
} // namespace sw

namespace sw
{
    struct TradeProtocol
    {
        static constexpr uint32 kVersion = 1;
    };
} // namespace sw

namespace sw
{
    /** @brief 이동 뒤 잔액 하나입니다(원장의 계정 잔액 — 게임 거울용). */
    struct TradeBalance
    {
        string _assetID{};
        int64  _amount{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 거래 응답 · 알림 몸 코덱입니다. */
    struct SW_GF_API TradeReplyWire
    {
        static void               writeReply( BitWriter& outWriter, TradeResult result, const TradeSnapshot& snapshot, const vector<TradeBalance>& listBalance );
        [[nodiscard]] static bool readReply( BitReader& reader, TradeResult& outResult, TradeSnapshot& outSnapshot, vector<TradeBalance>& outListBalance );
    };
} // namespace sw
