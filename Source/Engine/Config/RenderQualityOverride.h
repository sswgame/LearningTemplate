/**
 * @file RenderQualityOverride.h
 * @brief 렌더 품질 단계 층입니다 — `Config/Engine/RenderQuality.json` 의 고른 단계가 엔진 설정의 `_render` 위에 섭니다(바탕 → 플랫폼 → 품질 단계).
 * @details 언리얼 스케일러빌리티 그룹(`sg.*` 단계마다 `r.*` 묶음) · 유니티 SRP 품질 에셋(품질 단계마다 렌더 파이프라인 에셋) 자리다.
 *          표는 단계 이름 → `RenderConfig` 덮어쓰기 객체이고, 단계는 `_render` 안쪽만 적는다 — 층이 `{ "_render": <단계> }` 로 감싸므로 다른 칸은 형식상 건드릴 수 없다.
 *          합치기는 플랫폼 층과 같은 `PlatformConfigOverride::apply` 하나이고, 합친 글은 엄격 읽기를 지나 단계의 모르는 키 · 범위 밖 값도 로드 오류다.
 *          단계는 `gv_renderQuality`(명령줄 `-gv_renderQuality=Low`)가 기동 때 고른다. 빈 이름이면 층이 없다(바탕 그대로).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @struct RenderQualityOverride
     * @brief 품질 단계 표에서 단계 하나를 골라 엔진 설정 덮어쓰기 글로 만드는 한 곳입니다.
     */
    struct SW_API RenderQualityOverride
    {
        /**
         * @brief @p tiersJSON 에서 @p tierName 단계를 찾아 `{ "_render": <단계> }` 글을 @p outOverrideJSON 에 씁니다.
         * @return 표가 JSON 객체가 아니거나, 그런 단계가 없거나(있는 이름을 함께 남긴다), 단계가 객체가 아니면 @p pLabel 과 함께 오류를 남기고 false 입니다.
         */
        [[nodiscard]] static bool makeOverrideJSON( string_view tiersJSON, string_view tierName, const utf8* pLabel, string& outOverrideJSON );

        /** @brief @p baseJSON 위에 @p tiersJSON 의 @p tierName 단계를 덮은 글을 @p outJSON 에 씁니다(`makeOverrideJSON` + `PlatformConfigOverride::apply`). */
        [[nodiscard]] static bool apply( string_view baseJSON, string_view tiersJSON, string_view tierName, const utf8* pLabel, string& outJSON );
    };
} // namespace sw
