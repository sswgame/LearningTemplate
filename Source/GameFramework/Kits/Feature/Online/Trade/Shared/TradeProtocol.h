/**
 * @file TradeProtocol.h
 * @brief 거래 키트의 와이어 형식입니다. 메서드와 알림 번호(영역 `OnlineMethodRange::kTrade`), 요청과 응답 본문, 저장소 레코드 코덱을 정의합니다.
 * @details 요청과 응답은 `REFLECT` 구조체 하나씩이고 본문은 고정 배치형(B)이다. 바이트 배치는 `PROPERTY` 선언 순서와 키가 정한다.
 *          응답 본문(`TradeReply`)은 결과와 스냅샷이고, 정산이면 요청한 계정의 이동 뒤 잔액(자산 id와 양)이 함께 온다. 게임이 이 잔액을 지갑과 인벤토리 미러에 넣는다.
 *          알림 본문도 `TradeReply` 이며 정산이면 그 계정의 잔액이 함께 온다. 기능 플래그 `feature.trade_enabled` 가 꺼져 있으면 모든 요청이 `kFeatureDisabled` 로 끝난다.
 *          저장소의 거래 레코드는 다른 빌드가 읽으므로 자기 기술형(A)이다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineProtocolRegistry.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Trade/Shared/TradeTypes.h"

namespace sw
{
    /** @brief 거래 키트의 메서드와 알림 번호입니다. 와이어 값이며 온라인 스키마 해시에 들어가지 않습니다(`OnlineProtocolRegistry.h`). */
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
    /** @brief 신청(`kInvite`)의 요청입니다. */
    REFLECT()
    struct TradeInviteRequest
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = 64 )
        string _peerDisplayName{};
    };
} // namespace sw

namespace sw
{
    /** @brief 거래 id 하나를 받는 요청(`kLock`, `kCancel`)입니다. */
    REFLECT()
    struct TradeIDRequest
    {
        REFLECT_BODY();
        PROPERTY()
        uint64 _tradeID{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 수락과 거절(`kRespond`)의 요청입니다. */
    REFLECT()
    struct TradeRespondRequest
    {
        REFLECT_BODY();
        PROPERTY()
        uint64 _tradeID{ 0 };
        PROPERTY( Min = 0, Max = 1 )
        uint8 _bAccept{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 제시(`kSetOffer`)의 요청입니다. 한쪽 상한(`TradeConstant::kMaxLegsPerSide`)을 넘는 항목은 서비스가 TooManyLegs 로 끝내므로 와이어 상한은 그 두 배입니다. */
    REFLECT()
    struct TradeOfferRequest
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = 16 )
        vector<TradeLeg> _listLeg{};
        PROPERTY()
        uint64 _tradeID{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 확정(`kConfirm`)의 요청입니다. 내가 본 두 제시 버전을 싣습니다. */
    REFLECT()
    struct TradeConfirmRequest
    {
        REFLECT_BODY();
        PROPERTY()
        uint64 _tradeID{ 0 };
        PROPERTY()
        uint32 _seenOwnRevision{ 0 };
        PROPERTY()
        uint32 _seenPeerRevision{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 이동 뒤 잔액 하나입니다. 원장의 계정 잔액이며 게임 미러에 씁니다. 자산 상한은 `LedgerConstant::kMaxAssetIDSize` 와 같은 값입니다. */
    REFLECT()
    struct TradeBalance
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = 48 )
        string _assetID{};
        PROPERTY()
        int64 _amount{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 응답과 알림의 본문입니다. 결과, 스냅샷, 정산이면 이 계정의 이동 뒤 잔액입니다. */
    REFLECT()
    struct TradeReply
    {
        REFLECT_BODY();
        PROPERTY()
        TradeSnapshot _snapshot{};
        PROPERTY( MaxCount = 16 )
        vector<TradeBalance> _listBalance{}; ///< 상한은 양쪽 항목 수(`TradeConstant::kMaxLegsPerSide` 의 두 배)이다
        PROPERTY()
        TradeResult _result{ TradeResult::Ok };
    };
} // namespace sw

namespace sw
{
    /** @brief 저장소의 거래 레코드입니다. 거래를 맡은 서버와 스냅샷입니다. 자기 기술형(A)으로 씁니다. */
    REFLECT()
    struct TradeSessionRecord
    {
        REFLECT_BODY();
        PROPERTY()
        TradeSnapshot _snapshot{};
        PROPERTY()
        uint64 _ownerServerID{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct TradeProtocol
     * @brief 거래 키트의 메시지 타입 목록, 등록 항목, 저장소 레코드 코덱입니다. 본문은 고정 배치형(B)이며 `MessageTypes::write` · `read` 로만 쓰고 읽습니다.
     */
    struct SW_KIT_API TradeProtocol
    {
        using MessageTypes = OnlineMessageTypes<TradeInviteRequest, TradeIDRequest, TradeRespondRequest, TradeOfferRequest, TradeConfirmRequest, TradeReply>;

        static const OnlineProtocolEntry kOnlineEntry; ///< 온라인 스키마 해시에 섞이는 이 키트의 등록 항목. 정의가 `.cpp` 에 있어 모듈이 오를 때 등록된다

        /**
         * @brief 거래 레코드를 읽고 씁니다. 자기 기술형(A)이며 레코드 판을 머리에 둡니다.
         * @details 다른 판이거나 모르는 필드가 있거나 선언된 범위를 벗어나면 읽지 않습니다. 필드가 바뀌어 옛 레코드를 읽어야 하면 판을 올리고 변환(`SchemaMigrateFn`)을 등록합니다.
         */
        static vector<uint8>      encodeSession( const TradeSessionRecord& record );
        [[nodiscard]] static bool decodeSession( const vector<uint8>& bytes, TradeSessionRecord& outRecord );

        /** @brief 공통 오류 코드를 거래 결과로 바꿉니다. `kVersionMismatch` 는 그 앞에서 `OnlineReplyUtil::markError` 가 `UpdateRequired` 로 바꿉니다. */
        static constexpr TradeResult fromErrorCode( uint16 errorCode ) { return errorCode == OnlineError::kInvalidRequest ? TradeResult::Invalid : TradeResult::Unavailable; }
    };
} // namespace sw
