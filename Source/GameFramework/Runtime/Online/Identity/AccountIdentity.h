/**
 * @file AccountIdentity.h
 * @brief 신원 값 형식입니다. 계정 id 와 공개 신원(온라인 메시지에 실리는 것)을 둡니다.
 * @details 키트 프로토콜과 클라이언트가 읽는 형식이라 공유에 둔다. 서버만 쓰는 계정 포트(`IAccountDirectory` · 차단 목록 · 세션 끊기 · 이름 색인 · 외부 연동 · 오프라인 푸시)는
 *          `Identity/Server/AccountDirectory.h` 에 있다 — 클라이언트 구성은 그 폴더를 짓지도 읽지도 않는다. 언리얼 `FUniqueNetId`(값)와 `IOnlineIdentity`(창구)의 갈림과 같다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Runtime/Online/Service/OnlineLimit.h"

namespace sw
{
    /** @brief 계정 id 입니다. 0 = 없음. 저장소 키로는 `ServiceKeyUtil::makeHex64`. */
    using AccountID = uint64;

    inline constexpr AccountID kInvalidAccountID = 0;
} // namespace sw

namespace sw
{
    /** @brief 계정 하나의 공개 정보입니다. 온라인 메시지(계정 발급 응답, 관리자 조회 응답)에 고정 배치형으로 실립니다. 이름 상한은 `OnlineLimit::kMaxDisplayNameSize` 와 같은 값입니다. */
    REFLECT()
    struct AccountIdentity
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = OnlineLimit::kMaxDisplayNameSize )
        string _displayName{};
        PROPERTY()
        AccountID _accountID{ kInvalidAccountID };
        PROPERTY( Min = 0, Max = 1 )
        uint8 _bGuest{ SW_FALSE }; ///< 게스트 계정(연동 전)
    };
} // namespace sw
