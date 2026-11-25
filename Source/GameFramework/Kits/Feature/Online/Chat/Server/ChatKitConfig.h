/**
 * @file ChatKitConfig.h
 * @brief 채팅 키트의 서버 설정입니다(`_listKit[]._settings`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/Feature/Online/Chat/Server/ChatSpamGuard.h"
#include "GameFramework/Runtime/Online/Report/ServiceReport.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerConfig.h"

namespace sw
{
    /** @brief 채팅 키트의 서버 설정입니다. 경로는 프로젝트 루트 기준입니다. 데이터 파일 경로는 코드 기본값 없이 설정에 적습니다. */
    REFLECT()
    struct ChatKitConfig
    {
        REFLECT_BODY();
        /** @brief 금칙어 파일입니다. 빈 글을 적으면 거르지 않습니다. */
        PROPERTY( Required )
        string _bannedWordsFile{};
        /** @brief 정규화가 건너뛸 글자 파일입니다(`ChatTextNormalizer`). */
        PROPERTY( Required )
        string _ignoredCharactersFile{};
        /** @brief 채팅 기록(귓속말 · 길드 · 파티 포함)의 TTL 입니다. TTL 쓸기가 보낸 뒤 이 기간이 지난 기록을 지웁니다(개인정보 보존 기간). */
        PROPERTY( Required )
        ServiceTTLConfig _historyTTL{};
        /** @brief 도배 방지(토큰 버킷 · 같은 글 반복)의 한도입니다. 코드 기본값이 없다. */
        PROPERTY( Required )
        ChatSpamConfig _spam{};
        /** @brief 한 계정이 들어가 있을 수 있는 채널 수입니다. */
        PROPERTY( Required, Min = 1, Max = 1024 )
        int32 _maxChannelPerMember{ 0 };
        /** @brief 신고 스냅숏에 담는 메시지 수(신고한 메시지와 그 앞)입니다. 신고 레코드의 줄 상한(`ServiceReportConstant::kMaxLineCount`) 이하입니다. */
        PROPERTY( Required, Min = 1, Max = ServiceReportConstant::kMaxLineCount )
        int32 _reportContextCount{ 0 };
    };
} // namespace sw
