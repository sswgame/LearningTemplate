/**
 * @file ServiceMailTypes.h
 * @brief 우편의 값 형식입니다. 첨부, 상태, 상한 — 우편함 · 관리자 프로토콜(키트)에 실려 클라이언트도 읽습니다.
 * @details 우편 넣기 · 레코드 형식(`ServiceMail`)과 보낼 우편 · 저장 레코드 · 알림 형식은 서버만 돌아 `Mail/Server/ServiceMail.h` 에 있다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Runtime/Online/Ledger/LedgerTypes.h"

namespace sw
{
    /** @brief 첨부 하나입니다. 온라인 메시지(관리자 요청, 우편함 응답)에 고정 배치형으로 실립니다. `MaxCount` 는 `LedgerConstant::kMaxAssetIDSize` 와 같은 값입니다. */
    REFLECT()
    struct ServiceMailAttachment
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
    /** @brief 우편 상태입니다. 저장 값이라 순서를 바꾸면 안 됩니다. */
    ENUM( Count = Count )
    enum class ServiceMailState : uint8
    {
        Unread = 0,
        Read,
        Claimed, ///< 첨부를 받았다(첨부가 없는 우편은 읽음과 같다)
        Count
    };
} // namespace sw

namespace sw
{
    /** @brief 우편의 상한 값들입니다. */
    struct ServiceMailConstant
    {
        static constexpr int32 kMaxAttachmentCount    = 8;
        static constexpr int32 kMaxTitleSize          = 128;
        static constexpr int32 kMaxBodySize           = 2048;
        static constexpr int32 kMaxSenderNameSize     = 64;
        static constexpr int32 kMaxIdempotencyKeySize = 64;                                                                           ///< `[0-9a-z_.-]`
        static constexpr int64 kDefaultRetentionMs    = 30 * constant::time::kSecondsPerDay * constant::time::kMillisecondsPerSecond; ///< 기본 보관 30 일
    };
} // namespace sw
