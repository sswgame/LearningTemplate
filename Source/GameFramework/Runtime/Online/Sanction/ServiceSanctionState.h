/**
 * @file ServiceSanctionState.h
 * @brief 계정 제재의 값 형식입니다. 종류와 계정 하나의 제재 상태 — 관리자 조회 응답(키트 프로토콜)에 실려 클라이언트 · 도구도 읽습니다.
 * @details 레코드 읽기 · 쓰기(`ServiceSanction`)와 제재 버스 주제는 서버만 돌아 `Sanction/Server/ServiceSanction.h` 에 있다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 제재 종류입니다. 저장 값이라 순서를 바꾸지 않는다. */
    ENUM( Count = Count )
    enum class ServiceSanctionKind : uint8
    {
        ChatMute = 0, ///< 채팅 금지
        Suspend,      ///< 기간 정지. 로그인을 거절하고 세션을 끊는다
        Ban,          ///< 영구 정지
        Count
    };
} // namespace sw

namespace sw
{
    /** @brief 계정 하나의 제재 상태입니다. 관리자 조회 응답에 고정 배치형으로 실립니다. `_version` 은 레코드 버전이라 `PROPERTY` 가 아니며 와이어에 싣지 않습니다. */
    REFLECT()
    struct ServiceSanctionState
    {
        REFLECT_BODY();
        static constexpr int64 kPermanentMs = 0x7FFFFFFFFFFFFFFFll;

        static constexpr int32 kMaxReasonCodeSize = 256; ///< 관리자 프로토콜의 문구 상한(`AdminProtocol::kMaxTextSize`)과 같다

        PROPERTY( MaxCount = kMaxReasonCodeSize )
        string _reasonCode{}; ///< 표시용 사유 코드(로컬라이제이션 키). 가장 최근 것이다
        PROPERTY()
        int64  _arrUntilMs[static_cast<int32>( ServiceSanctionKind::Count )]{}; ///< 0 = 없음
        uint64 _version{ 0 };                                                   ///< 레코드 판(0 = 레코드 없음)
    };
} // namespace sw
