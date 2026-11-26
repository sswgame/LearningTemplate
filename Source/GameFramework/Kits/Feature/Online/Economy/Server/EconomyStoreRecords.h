/**
 * @file EconomyStoreRecords.h
 * @brief 경제 키트의 저장소 레코드 형식입니다(`ServiceRecordTable` — 판은 형식의 `REFLECT( Version )`, 모르는 칸 보존).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/Feature/Online/Economy/Runtime/EconomyProtocol.h"
#include "GameFramework/Runtime/Online/Store/ServiceRecordUnknownFields.h"

namespace sw
{
    /** @brief 거래 하나의 상태입니다(영수증 상태 레코드의 값 — 열거자 이름으로 싣는다). `None` 은 레코드가 없다는 뜻이라 저장하지 않는다. */
    ENUM()
    enum class EconomyReceiptState : uint8
    {
        None = 0,      ///< 레코드가 없다 — 지급도 환불도 오지 않았다
        Redeemed,      ///< 지급했다(받은 계정은 지급 분개의 다리에만 있다)
        RefundedFirst, ///< 지급 전에 환불이 왔다 — 뒤에 온 지급을 막는다
        Revoked,       ///< 지급 뒤 환불로 회수했다
        Withdrawn      ///< 청약철회로 돌려받았다
    };

    /** @brief 영수증 상태 레코드(`econ_receipt`)입니다. 지우지 않는 레코드라 사람과 이어지는 값(계정 id)은 담지 않는다(감사 P0-7). */
    REFLECT()
    struct EconomyReceiptRecord
    {
        REFLECT_BODY();
        PROPERTY()
        EconomyReceiptState _state{ EconomyReceiptState::None };

        ServiceRecordUnknownFields _unknownProperties{};
    };

    /** @brief 스토어에 아직 알리지 못한 지급(`econ_delivery`)입니다. 키는 지급 분개 키입니다. */
    REFLECT()
    struct EconomyDeliveryRecord
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = EconomyProtocol::kMaxStoreNameSize )
        string _storeName{};
        PROPERTY( MaxCount = EconomyProtocol::kMaxReceiptPayloadSize )
        string _deliveryKey{};
        PROPERTY()
        int64 _deadlineMs{ 0 };

        ServiceRecordUnknownFields _unknownProperties{};
    };

    /** @brief 계정이 상품 하나를 산 수(`econ_purchase`)입니다. */
    REFLECT()
    struct EconomyPurchaseCountRecord
    {
        REFLECT_BODY();
        PROPERTY( Min = 0 )
        int64 _count{ 0 };

        ServiceRecordUnknownFields _unknownProperties{};
    };

    /** @brief 스토어 하나의 환불 조회 기준 시각(`econ_refund_cursor`)입니다. */
    REFLECT()
    struct EconomyRefundCursorRecord
    {
        REFLECT_BODY();
        PROPERTY( Min = 0 )
        int64 _lastReadMs{ 0 };

        ServiceRecordUnknownFields _unknownProperties{};
    };
} // namespace sw
