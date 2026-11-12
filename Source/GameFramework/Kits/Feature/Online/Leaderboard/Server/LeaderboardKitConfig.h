/**
 * @file LeaderboardKitConfig.h
 * @brief 순위표 키트의 서버 설정입니다(`_listKit[]._settings`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 순위표 키트의 서버 설정입니다. 둘 다 없으면 보드가 없습니다(경고). */
    REFLECT()
    struct LeaderboardKitConfig
    {
        REFLECT_BODY();
        /** @brief 보드 · 업적 정의입니다(리소스 경로, `<LeaderboardCatalog>`). */
        PROPERTY()
        string _catalog{};
        /** @brief 기본 정의(내림차순 · Best · 초기화 없음 · 서버만 제출)로 올릴 보드 id 입니다. 데이터 파일 없이 도는 도구 · 테스트용입니다. */
        PROPERTY()
        vector<string> _listBoardID{};
        /** @brief 지난 기간(일간 · 주간 · 정산한 시즌)의 점수를 남기는 기간(일)입니다. 기간이 끝난 뒤(시즌은 정산 창이 닫힌 뒤) 이 기간이 지나면 지웁니다. */
        PROPERTY( Min = 1, Max = 3650 )
        int32 _pastScoreRetentionDays{ 30 };
        /** @brief 시즌 결과(순위 · 점수)를 남기는 기간(일)입니다. 정산 창이 닫힌 뒤 이 기간이 지나면 지웁니다. */
        PROPERTY( Min = 1, Max = 3650 )
        int32 _seasonResultRetentionDays{ 365 };
    };
} // namespace sw
