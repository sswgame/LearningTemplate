/**
 * @file ModResolver.h
 * @brief 켤 모드들의 매니페스트를 검증하고 마운트 순서를 정합니다 — 콘텐츠 판 · 덮어쓰기 허락 · 의존 · 충돌 · 순환 · 덮어쓰기 겹침.
 * @details 순서는 의존이 먼저(아래), 그다음 매니페스트 우선순위 · id 순이다 — 요청 순서는 보지 않는다. 그래서 같은 모드 집합이면 서버와 클라이언트가 같은 순서 ·
 *          같은 구성 해시를 만든다(언리얼 pak 순서가 파일 이름이 아니라 밝힌 순서로 정해지는 것과 같은 결정성). 판단은 순수 함수이고 파일을 읽지 않는다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Content/ModSupport/ModManifest.h"
#include "GameFramework/Kits/Feature/Content/ModSupport/ModSupportSettings.h"

namespace sw
{
    /**
     * @struct ModResolver
     * @brief 모드 집합의 검증과 순서입니다.
     */
    struct SW_KIT_API ModResolver
    {
        /**
         * @brief @p listManifest(켤 모드 전부, id 가 겹치지 않는다)를 검증하고 마운트 순서(낮은 우선순위 → 높은)의 색인을 @p outListOrder 에 담습니다.
         * @return 하나라도 어기면 첫 이유를 오류로 남기고 false 입니다 — 일부만 켜지 않는다(서버와 다른 구성으로 조용히 돌지 않는다).
         *         어기는 것: 모드 수 상한, 같은 id, 콘텐츠 판 범위 밖, 허락하지 않은 덮어쓰기, 켜지 않은 필수 의존, 낮은 의존 판, 함께 켠 충돌 모드, 의존 순환,
         *         의존 관계가 없는 두 모드가 같은 리소스를 덮어씀(뒤 모드가 앞 모드에 직접 의존하면 받는다 — 알고 위에 쌓은 것이다).
         */
        [[nodiscard]] static bool resolveOrder( const vector<ModManifest>& listManifest, const ModSupportSettings& settings, vector<size_t>& outListOrder );
    };
} // namespace sw
