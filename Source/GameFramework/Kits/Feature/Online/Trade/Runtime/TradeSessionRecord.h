/**
 * @file TradeSessionRecord.h
 * @brief 거래 키트의 와이어 형식입니다. 메서드와 알림 번호(영역 `OnlineMethodRange::kTrade`), 요청과 응답 본문, 저장소 레코드 코덱을 정의합니다.
 * @details 요청과 응답은 `REFLECT` 구조체 하나씩이고 본문은 고정 배치형(B)이다. 바이트 배치는 `PROPERTY` 선언 순서와 키가 정한다.
 *          응답 본문(`TradeReply`)은 결과와 스냅샷이고, 정산이면 요청한 계정의 이동 뒤 잔액(자산 id와 양)이 함께 온다. 게임이 이 잔액을 지갑과 인벤토리 미러에 넣는다.
 *          알림 본문도 `TradeReply` 이며 정산이면 그 계정의 잔액이 함께 온다. 기능 플래그 `feature.trade_enabled` 가 꺼져 있으면 모든 요청이 `kFeatureDisabled` 로 끝난다.
 *          저장소의 거래 레코드는 다른 빌드가 읽으므로 자기 기술형(A)이다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Trade/Runtime/TradeTypes.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocol.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocolRegistry.h"
#include "GameFramework/Runtime/Online/Store/ServiceRecordUnknownFields.h"

namespace sw
{
    /** @brief 거래 메서드 이름입니다. 열거자 순서가 와이어 번호(`OnlineMethodTable`)입니다. 새 메서드는 `Count` 앞에만 더합니다. */
    ENUM( Count = Count )
    enum class TradeMethodName : uint8
    {
        Invite,
        Respond,
        SetOffer,
        Lock,
        Confirm,
        Cancel,
        Count,
    };

    /** @brief 거래 알림 이름입니다. 열거자 순서가 와이어 번호(`OnlineMethodTable`)입니다. 새 알림은 `Count` 앞에만 더합니다. */
    ENUM( Count = Count )
    enum class TradePushName : uint8
    {
        Invited,
        Update,
        Closed,
        Count,
    };
} // namespace sw

namespace sw
{
    /** @brief 신청(`Invite`)의 요청입니다. */
    REFLECT()
    struct TradeInviteRequest
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = TradeConstant::kMaxPeerNameSize )
        string _peerDisplayName{};
    };
} // namespace sw

namespace sw
{
    /** @brief 거래 id 하나를 받는 요청(`Lock`, `Cancel`)입니다. */
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
    /** @brief 수락과 거절(`Respond`)의 요청입니다. */
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
    /** @brief 제시(`SetOffer`)의 요청입니다. 한쪽 상한(`TradeConstant::kMaxLegsPerSide`)을 넘는 항목은 서비스가 TooManyLegs 로 끝내므로 와이어 상한은 그 두 배입니다. */
    REFLECT()
    struct TradeOfferRequest
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = TradeConstant::kMaxLegsOnWire )
        vector<TradeLeg> _listLeg{};
        PROPERTY()
        uint64 _tradeID{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 확정(`Confirm`)의 요청입니다. 내가 본 두 제시 버전을 싣습니다. */
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
        PROPERTY( MaxCount = LedgerConstant::kMaxAssetIDSize )
        string _assetID{};
        PROPERTY()
        int64 _amount{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 거래 명령의 응답입니다. 결과, 스냅샷, 정산이면 이 계정의 이동 뒤 잔액입니다.
     * @details 여섯 명령이 모두 거래 하나의 상태를 바꾸고 그 상태(스냅샷)를 돌려주므로 응답 형식이 하나다 — 메서드마다 나누면 같은 칸의 사본이 여섯 벌이다.
     */
    REFLECT()
    struct TradeReply
    {
        REFLECT_BODY();
        PROPERTY()
        TradeSnapshot _snapshot{};
        PROPERTY( MaxCount = TradeConstant::kMaxLegsOnWire )
        vector<TradeBalance> _listBalance{}; ///< 상한은 양쪽 항목 수(`TradeConstant::kMaxLegsPerSide` 의 두 배)이다
        PROPERTY()
        TradeResult _result{ TradeResult::Ok };

        /** @brief 다시 시도할 업무 결과가 없다 — 충돌 · 일시 장애는 공통 범주(`kBusy` · `kUnavailable`)로 오고 보존은 그 범주가 정한다. */
        [[nodiscard]] bool isRetryable() const { return false; }
    };

    /** @brief 거래 알림(초대 · 바뀜 · 닫힘)의 본문입니다. 스냅샷과, 정산이면 이 계정의 이동 뒤 잔액입니다. 알림에는 결과가 없다. */
    REFLECT()
    struct SW_KIT_API TradeNotice
    {
        REFLECT_BODY();
        static constexpr uint32 kBusVersion = 1; ///< push 중계(`OnlinePushRelay`)의 의미 버전
        PROPERTY()
        TradeSnapshot _snapshot{};
        PROPERTY( MaxCount = TradeConstant::kMaxLegsOnWire )
        vector<TradeBalance> _listBalance{}; ///< 상한은 양쪽 항목 수(`TradeConstant::kMaxLegsPerSide` 의 두 배)이다
    };
} // namespace sw

namespace sw
{
    /** @brief Trade 메서드 표입니다. 번호는 열거자 순서에서 나오고, 요청 · 응답의 짝과 호출 규칙은 서술자가 정합니다. */
    struct TradeMethod : OnlineMethodTable<TradeMethodName, OnlineMethodRange::kTrade, TradePushName>
    {
        using Invite   = Method<MethodName::Invite, TradeInviteRequest, TradeReply, OnlineMethodFlag::RequiresIdempotencyKey>; ///< 상대의 표시 이름을 받는다
        using Respond  = Method<MethodName::Respond, TradeRespondRequest, TradeReply, OnlineMethodFlag::RequiresIdempotencyKey>;
        using SetOffer = Method<MethodName::SetOffer, TradeOfferRequest, TradeReply, OnlineMethodFlag::RequiresIdempotencyKey>;
        using Lock     = Method<MethodName::Lock, TradeIDRequest, TradeReply, OnlineMethodFlag::RequiresIdempotencyKey>;
        using Confirm  = Method<MethodName::Confirm, TradeConfirmRequest, TradeReply, OnlineMethodFlag::RequiresIdempotencyKey>; ///< 내가 본 내 제시 버전, 내가 본 상대 제시 버전을 받는다
        using Cancel   = Method<MethodName::Cancel, TradeIDRequest, TradeReply, OnlineMethodFlag::RequiresIdempotencyKey>;
        using Invited  = Push<PushName::Invited, TradeNotice>; ///< 초대받았다는 알림이며 스냅샷을 싣는다
        using Update   = Push<PushName::Update, TradeNotice>;  ///< 바뀌었다는 알림이며 스냅샷을 싣는다
        using Closed   = Push<PushName::Closed, TradeNotice>;  ///< 닫혔다는 알림이며 스냅샷을 싣고 정산이면 잔액도 싣는다
    };
} // namespace sw

namespace sw
{
    /** @brief 저장소의 거래 레코드입니다. 거래를 맡은 서버와 스냅샷입니다. 자기 기술형(A)으로 씁니다. */
    REFLECT()
    struct SW_KIT_API TradeSessionRecord
    {
        REFLECT_BODY();
        PROPERTY()
        TradeSnapshot _snapshot{};
        PROPERTY()
        uint64 _ownerServerID{ 0 };
        /// 이 빌드가 모르는 칸(배포 중 새 빌드가 더한 칸)이다. PROPERTY 가 아니다 — 영속 읽기가 담고 쓰기가 그대로 싣는다(`ServiceRecordUtil`).
        ServiceRecordUnknownFields _unknownProperties{};

        // 거래 키트의 메시지 타입 목록, 등록 항목, 저장소 레코드 코덱입니다. 본문은 고정 배치형(B)이며 `MessageTypes::write` · `read` 로만 쓰고 읽습니다.
        using Table        = OnlineProtocolTable<TradeMethod, OnlineMethodList<TradeMethod::Invite, TradeMethod::Respond, TradeMethod::SetOffer, TradeMethod::Lock, TradeMethod::Confirm, TradeMethod::Cancel>, OnlinePushList<TradeMethod::Invited, TradeMethod::Update, TradeMethod::Closed>>;
        using MessageTypes = Table::MessageTypes;
        using ResultName   = TradeResult;

        static const OnlineProtocolEntry kOnlineEntry; ///< 온라인 스키마 해시에 섞이는 이 키트의 등록 항목. 정의가 `.cpp` 에 있어 모듈이 오를 때 등록된다

        /**
         * @brief 거래 레코드를 읽고 씁니다. 영속 레코드이므로 자기 기술형(A)이며 레코드 버전을 머리에 둡니다.
         * @details 이 빌드가 모르는 필드는 레코드의 `_unknownProperties` 에 담고(다시 쓸 때 그대로 싣는다), 다른 버전이거나 아는 필드를 읽지 못했거나 선언된 범위를 벗어나면 손상으로 보고 오류 로그를 남기며 false 입니다(`ServiceRecordUtil::readDurable`).
         *          필드를 더하면 옛 레코드의 그 필드는 기본값으로 채워집니다. 필드를 빼거나 뜻을 바꾸면 버전을 올리고 데이터를 다시 씁니다.
         */
        [[nodiscard]] static bool encodeSession( const TradeSessionRecord& record, vector<uint8>& outBytes );
        [[nodiscard]] static bool decodeSession( string_view key, const vector<uint8>& bytes, TradeSessionRecord& outRecord );
    };
} // namespace sw
