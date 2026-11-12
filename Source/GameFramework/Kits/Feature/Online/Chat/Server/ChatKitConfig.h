/**
 * @file ChatKitConfig.h
 * @brief 채팅 키트의 서버 설정입니다(`_listKit[]._settings`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

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
        /** @brief 채팅 기록(귓속말 · 길드 · 파티 포함)을 남기는 기간(일)입니다. 보존 쓸기가 보낸 뒤 이 기간이 지난 기록을 지웁니다(개인정보 보존 기간). */
        PROPERTY( Min = 1, Max = 3650 )
        int32 _historyRetentionDays{ 30 };
    };
} // namespace sw
