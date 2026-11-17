/**
 * @file AdminKitConfig.h
 * @brief GM 키트의 서버 설정입니다(`_listKit[]._settings`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/Feature/Online/Admin/Runtime/AdminProtocol.h"

namespace sw
{
    /** @brief 레코드가 없을 때만 넣는 첫 관리자 하나입니다. */
    REFLECT()
    struct AdminSeedEntry
    {
        REFLECT_BODY();
        PROPERTY( Required, Min = 1 )
        int64 _accountID{ 0 };
        PROPERTY( Required )
        AdminRole _role{ AdminRole::None };
    };

    /** @brief GM 키트의 서버 설정입니다. */
    REFLECT()
    struct AdminKitConfig
    {
        REFLECT_BODY();
        PROPERTY()
        vector<AdminSeedEntry> _listSeedAdmin{};
        /** @brief Support 가 걸 수 있는 채팅 금지의 최대 일수입니다(감사 O-16). */
        PROPERTY( Required, Min = 1, Max = 3650 )
        int32 _supportMaxSanctionDays{ 0 };
        /** @brief Operator 가 걸 수 있는 채팅 금지 · 정지의 최대 일수입니다. 영구 정지와 그보다 긴 제재는 Super 의 일이다. */
        PROPERTY( Required, Min = 1, Max = 3650 )
        int32 _operatorMaxSanctionDays{ 0 };
    };
} // namespace sw
