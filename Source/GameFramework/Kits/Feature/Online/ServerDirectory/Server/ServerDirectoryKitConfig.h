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
    /** @brief 서버 목록 · 공지를 읽는 주기와 하트비트 한도입니다. 코드 기본값이 없다. */
    REFLECT()
    struct SW_KIT_API ServerDirectoryPolicy
    {
        REFLECT_BODY();
        PROPERTY( Required, Min = 1 )
        int64 _reloadPeriodMs{ 0 }; ///< 점검과 공지를 다시 읽는 주기이다
        PROPERTY( Required, Min = 1 )
        int64 _registryRefreshPeriodMs{ 0 }; ///< 서버 목록을 다시 읽는 주기이다
        PROPERTY( Required, Min = 1 )
        int64 _staleMs{ 0 }; ///< 하트비트가 이보다 오래된 서버는 고르지 않는다
    };
} // namespace sw

namespace sw
{
    /** @brief 서버 디렉터리 키트의 서버 설정입니다. */
    REFLECT()
    struct ServerDirectoryKitConfig
    {
        REFLECT_BODY();
        /** @brief 배정과 목록을 받는 서버 종류입니다. */
        PROPERTY( DefaultName )
        vector<string> _listServerKind{ "game" };
        /** @brief 끝난 공지 · 점검 창의 TTL 입니다. 끝난 뒤 이 기간이 지나면 TTL 쓸기가 지웁니다(끝이 없는 것은 남긴다). */
        PROPERTY( Required )
        ServiceTTLConfig _endedAnnouncementTTL{};
        /** @brief 서버 목록 · 공지를 읽는 주기와 하트비트 한도입니다. */
        PROPERTY( Required )
        ServerDirectoryPolicy _refresh{};
    };
} // namespace sw
