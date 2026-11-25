/**
 * @file LeaderboardStoreRecords.h
 * @brief 순위표 키트의 저장소 레코드 형식입니다(`ServiceRecordTable` — 판은 형식의 `REFLECT( Version )`, 모르는 칸 보존).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Runtime/Online/Service/OnlineLimit.h"
#include "GameFramework/Runtime/Online/Store/ServiceRecordUnknownFields.h"

namespace sw
{
    /** @brief 값 하나와 그 시각(점수 `lb_score` · 통계 `lb_stat` · 업적 `lb_achievement` — 업적의 값은 달성 시각)입니다. */
    REFLECT()
    struct LeaderboardValueRecord
    {
        REFLECT_BODY();
        PROPERTY()
        int64 _value{ 0 };
        PROPERTY()
        int64 _updatedMs{ 0 };
        PROPERTY( MaxCount = OnlineLimit::kMaxDisplayNameSize )
        string _displayName{}; ///< 점수만 싣는다(계정 목록 조회)

        ServiceRecordUnknownFields _unknownProperties{};
    };

    /** @brief 시즌 하나의 정산 결과(`lb_season_result`, 키 `<보드>/<시즌>/<계정>`)입니다. */
    REFLECT()
    struct LeaderboardSeasonResultRecord
    {
        REFLECT_BODY();
        PROPERTY()
        int64 _score{ 0 };
        PROPERTY( Min = 1 )
        int32 _rank{ 1 };

        ServiceRecordUnknownFields _unknownProperties{};
    };

    /** @brief 순위표가 캐시에 둔 표시 이름(키 `lb.name:<계정>`)입니다. 다시 만들 수 있는 캐시라 판이 다르면 없는 것으로 본다. */
    REFLECT()
    struct LeaderboardNameCacheRecord
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = OnlineLimit::kMaxDisplayNameSize )
        string _displayName{};
    };
} // namespace sw
