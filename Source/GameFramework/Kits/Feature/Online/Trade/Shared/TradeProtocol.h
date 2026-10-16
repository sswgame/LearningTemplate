/**
 * @file TradeProtocol.h
 * @brief 거래 키트의 와이어 형식입니다. 메서드와 알림 번호(영역 `OnlineMethodRange::kTrade`), 응답 본문 형식을 정의합니다.
 * @details 응답 본문은 `TradeResult` 뒤에 스냅샷이 오고, 정산이면 요청한 계정의 이동 뒤 잔액(자산 id와 양)이 이어진다. 게임이 이 잔액을 지갑과 인벤토리 미러에 넣는다.
 *          알림 본문은 스냅샷이며 정산이면 그 계정의 잔액이 이어진다. 기능 플래그 `feature.trade_enabled` 가 꺼져 있으면 모든 요청이 `kFeatureDisabled` 로 끝난다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineProtocolRegistry.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Trade/Shared/TradeTypes.h"

namespace sw
{
    class BitReader;
    class BitWriter;

    /** @brief 거래 키트의 메서드와 알림 번호입니다. */
    struct TradeMethod
    {
        static constexpr uint16 kInvite      = OnlineMethodRange::kTrade + 0x01; ///< 상대의 표시 이름을 받는다
        static constexpr uint16 kRespond     = OnlineMethodRange::kTrade + 0x02; ///< 거래 id 와 수락 여부를 받는다
        static constexpr uint16 kSetOffer    = OnlineMethodRange::kTrade + 0x03; ///< 거래 id 와 항목을 받는다
        static constexpr uint16 kLock        = OnlineMethodRange::kTrade + 0x04; ///< 거래 id
        static constexpr uint16 kConfirm     = OnlineMethodRange::kTrade + 0x05; ///< 거래 id, 내가 본 내 제시 버전, 내가 본 상대 제시 버전을 받는다
        static constexpr uint16 kCancel      = OnlineMethodRange::kTrade + 0x06; ///< 거래 id
        static constexpr uint16 kPushInvited = OnlineMethodRange::kTrade + 0x80; ///< 초대받았다는 알림이며 스냅샷을 싣는다
        static constexpr uint16 kPushUpdate  = OnlineMethodRange::kTrade + 0x81; ///< 바뀌었다는 알림이며 스냅샷을 싣는다
        static constexpr uint16 kPushClosed  = OnlineMethodRange::kTrade + 0x82; ///< 닫혔다는 알림이며 스냅샷을 싣고 정산이면 잔액도 싣는다

        static_assert( OnlineMethodRange::isInRange( kCancel, OnlineMethodRange::kTrade ) && OnlineMethodRange::isMethod( kCancel ) );
        static_assert( OnlineMethodRange::isMethod( kPushClosed ) == false );
    };
} // namespace sw

namespace sw
{
    struct SW_KIT_API TradeProtocol
    {
        static const OnlineProtocolEntry kOnlineEntry; ///< 온라인 스키마 해시에 섞이는 이 키트의 등록 항목. 정의가 `.cpp` 에 있어 모듈이 오를 때 등록된다
        static constexpr uint32          kVersion = 1;

        /** @brief 공통 오류 코드를 거래 결과로 바꿉니다. `kVersionMismatch` 는 그 앞에서 `OnlineReplyUtil::markError` 가 `UpdateRequired` 로 바꿉니다. */
        static constexpr TradeResult fromErrorCode( uint16 errorCode ) { return errorCode == OnlineError::kInvalidRequest ? TradeResult::Invalid : TradeResult::Unavailable; }
    };
} // namespace sw

namespace sw
{
    /** @brief 이동 뒤 잔액 하나입니다. 원장의 계정 잔액이며 게임 미러에 씁니다. */
    struct TradeBalance
    {
        string _assetID{};
        int64  _amount{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 거래 응답과 알림 본문의 코덱입니다. */
    struct SW_KIT_API TradeReplyWire
    {
        static void               writeReply( BitWriter& outWriter, TradeResult result, const TradeSnapshot& snapshot, const vector<TradeBalance>& listBalance );
        [[nodiscard]] static bool readReply( BitReader& reader, TradeResult& outResult, TradeSnapshot& outSnapshot, vector<TradeBalance>& outListBalance );
    };
} // namespace sw
