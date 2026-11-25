/**
 * @file SocialStoreRecords.h
 * @brief 소셜 키트(길드 · 친구)의 저장소 레코드 형식입니다(`ServiceRecordTable` — 판은 형식의 `REFLECT( Version )`, 모르는 칸 보존).
 * @details 길드 하나 · 계정 → 길드 · 이름 → 길드 · 맡긴 사람 · 다음 길드 id 는 id 하나라 연결 표(`ServiceLinkTable`)다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/Feature/Online/Social/Runtime/SocialTypes.h"
#include "GameFramework/Runtime/Online/Store/ServiceRecordUnknownFields.h"

namespace sw
{
    /** @brief 길드 하나(`social_guild`, 키는 길드 16 진)입니다. */
    REFLECT()
    struct GuildStoreRecord
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = GuildLimit::kMaxNameSize )
        string _name{};
        PROPERTY( MaxCount = GuildLimit::kMaxAnnouncementSize )
        string _announcement{};
        PROPERTY()
        uint64 _masterID{ 0 };
        PROPERTY()
        int64 _createdMs{ 0 };
        PROPERTY( Min = 0 )
        int32 _memberCount{ 0 };
        PROPERTY( Min = 0 )
        int64 _experience{ 0 };
        PROPERTY( Min = 0, Max = 1 )
        uint8 _bOpenToApply{ SW_FALSE };

        ServiceRecordUnknownFields _unknownProperties{};
    };

    /** @brief 길드 멤버 하나(`social_guild_member`, 키 `<길드>/<계정>`)입니다. */
    REFLECT()
    struct GuildMemberStoreRecord
    {
        REFLECT_BODY();
        PROPERTY()
        GuildRole _role{ GuildRole::Member };
        PROPERTY()
        int64 _joinedMs{ 0 };

        ServiceRecordUnknownFields _unknownProperties{};
    };

    /** @brief 길드 초대(`social_guild_invite`)입니다. */
    REFLECT()
    struct GuildInviteStoreRecord
    {
        REFLECT_BODY();
        PROPERTY()
        uint64 _inviterID{ 0 };
        PROPERTY()
        int64 _expiresMs{ 0 };

        ServiceRecordUnknownFields _unknownProperties{};
    };

    /** @brief 가입 신청(`social_guild_application`, 키 `<길드>/<계정>`)입니다. */
    REFLECT()
    struct GuildApplicationStoreRecord
    {
        REFLECT_BODY();
        PROPERTY()
        int64 _appliedMs{ 0 };
        PROPERTY()
        int64 _expiresMs{ 0 };

        ServiceRecordUnknownFields _unknownProperties{};
    };

    /** @brief 창고 꺼내기 허용의 남은 한도(`social_guild_storage_grant`)입니다. */
    REFLECT()
    struct GuildGrantStoreRecord
    {
        REFLECT_BODY();
        PROPERTY( Min = 0 )
        int64 _remaining{ 0 };

        ServiceRecordUnknownFields _unknownProperties{};
    };

    /** @brief 두 계정 사이의 친구 관계(`social_link`, 키 `<나>/<상대>`)입니다. */
    REFLECT()
    struct SocialLinkStoreRecord
    {
        REFLECT_BODY();
        PROPERTY()
        SocialLinkState _state{ SocialLinkState::None };
        PROPERTY()
        int64 _sinceMs{ 0 };

        ServiceRecordUnknownFields _unknownProperties{};
    };

    /** @brief 계정 하나의 관계 수(`social_count`)입니다. */
    REFLECT()
    struct SocialCountStoreRecord
    {
        REFLECT_BODY();
        PROPERTY( Min = 0 )
        int32 _friendCount{ 0 };
        PROPERTY( Min = 0 )
        int32 _outgoingCount{ 0 };
        PROPERTY( Min = 0 )
        int32 _incomingCount{ 0 };
        PROPERTY( Min = 0 )
        int32 _blockedCount{ 0 };

        ServiceRecordUnknownFields _unknownProperties{};
    };
} // namespace sw
