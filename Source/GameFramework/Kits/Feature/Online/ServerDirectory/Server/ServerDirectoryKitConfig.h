/**
 * @file ServerDirectoryKitConfig.h
 * @brief 서버 디렉터리 키트의 서버 설정입니다(`_listKit[]._settings`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Runtime/Online/Server/OnlineServerConfig.h"

namespace sw
{
    /** @brief 서버 디렉터리 키트의 서버 설정입니다. */
    REFLECT()
    struct ServerDirectoryKitConfig
    {
        REFLECT_BODY();
        /** @brief 배정과 목록을 받는 서버 종류입니다. */
        PROPERTY()
        vector<string> _listServerKind{ "game" };
        /** @brief 끝난 공지 · 점검 창의 TTL 입니다. 끝난 뒤 이 기간이 지나면 TTL 쓸기가 지웁니다(끝이 없는 것은 남긴다). */
        PROPERTY( Required )
        ServiceTTLConfig _endedAnnouncementTTL{};
    };
} // namespace sw
