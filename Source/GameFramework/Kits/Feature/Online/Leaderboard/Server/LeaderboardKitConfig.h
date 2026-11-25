/**
 * @file LeaderboardKitConfig.h
 * @brief 순위표 키트의 서버 설정입니다(`_listKit[]._settings`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/Feature/Online/Leaderboard/Server/LeaderboardCatalog.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerConfig.h"

namespace sw
{
    /** @brief 순위표 키트의 서버 설정입니다. 둘 다 없으면 보드가 없습니다(경고). */
    REFLECT()
    struct LeaderboardKitConfig
    {
        REFLECT_BODY();
        /** @brief 보드 · 업적 정의(`<LeaderboardCatalog>`)입니다 — 글 칸이었을 때는 같은 이름의 참조 칸과 GUID 이전 도구가 가르지 못했다. */
        PROPERTY()
        AssetRef<LeaderboardCatalog> _catalog{};
        /** @brief 기본 정의(내림차순 · Best · 초기화 없음 · 서버만 제출)로 올릴 보드 id 입니다. 데이터 파일 없이 도는 도구 · 테스트용입니다. */
        PROPERTY()
        vector<string> _listBoardID{};
        /** @brief 지난 기간(일간 · 주간 · 정산한 시즌) 점수의 TTL 입니다. 기간이 끝난 뒤(시즌은 정산 창이 닫힌 뒤) 이 기간이 지나면 지웁니다. */
        PROPERTY( Required )
        ServiceTTLConfig _pastScoreTTL{};
        /** @brief 시즌 결과(순위 · 점수)의 TTL 입니다. 정산 창이 닫힌 뒤 이 기간이 지나면 지웁니다. */
        PROPERTY( Required )
        ServiceTTLConfig _seasonResultTTL{};
        /** @brief 둘레 읽기(내 순위 위아래)의 반지름 상한입니다. 요청의 와이어 상한을 넘지 못합니다. */
        PROPERTY( Required, Min = 0, Max = LeaderboardLimit::kMaxPage )
        int32 _maxAroundRadius{ 0 };
        /** @brief 표시 이름 캐시의 시한(밀리초)입니다. */
        PROPERTY( Required, Min = 1000 )
        int64 _nameCacheTTLMs{ 0 };
    };
} // namespace sw
