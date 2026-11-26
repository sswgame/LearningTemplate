/**
 * @file ConfigOverride.h
 * @brief 엔진 설정의 덮어쓰기 층 둘입니다 — 플랫폼 층(`PlatformConfigOverride`)과 렌더 품질 단계 층(`RenderQualityOverride`).
 * @details **플랫폼 층**
 *          호스트 엔진 설정의 플랫폼 덮어쓰기 층입니다 — `Config/Platform/<플랫폼>/EngineConfig.json` 이 `Config/Engine/EngineConfig.json` 위에 섭니다.
 *          언리얼의 플랫폼 ini 층(`Config/<Platform>/<Platform>Engine.ini` 가 `DefaultEngine.ini` 를 덮는다)과 같은 자리다. 덮어쓰기 파일은 바뀌는 키만 적는다
 *          (빈 객체도 된다). 없으면 덮어쓰기가 없다 — 선택 층이다. 합친 결과는 바탕 층과 같은 엄격 읽기(`ConfigManager::readConfigJSON`)를 지나므로
 *          덮어쓰기의 모르는 키 · 범위 밖 값도 오류다. 디스크 구성은 실행할 때 파일을, 정적 구성(배포 · 웹 · Android)은 configure 가 구운 글
 *          (`shipping_host::kEngineConfigPlatformOverrideJSON`)을 같은 합치기(`apply`) 하나로 덮는다.
 *          게임 프리셋은 팩 루트 한 칸뿐이라 이 층이 없다 — 게임의 쿠킹 규칙 층(`GameConfig::_cookRules`)과 같은 "바탕 뒤에 합친다" 모양이다.
 *
 *          **렌더 품질 단계 층**
 *          렌더 품질 단계 층입니다 — `Config/Engine/RenderQuality.json` 의 고른 단계가 엔진 설정의 `_render` 위에 섭니다(바탕 → 플랫폼 → 품질 단계).
 *          언리얼 스케일러빌리티 그룹(`sg.*` 단계마다 `r.*` 묶음) · 유니티 SRP 품질 에셋(품질 단계마다 렌더 파이프라인 에셋) 자리다.
 *          표는 단계 이름 → `RenderConfig` 덮어쓰기 객체이고, 단계는 `_render` 안쪽만 적는다 — 층이 `{ "_render": <단계> }` 로 감싸므로 다른 칸은 형식상 건드릴 수 없다.
 *          합치기는 플랫폼 층과 같은 `PlatformConfigOverride::apply` 하나이고, 합친 글은 엄격 읽기를 지나 단계의 모르는 키 · 범위 밖 값도 로드 오류다.
 *          단계 이름(low · medium · high · ultra)은 사용자 옵션 `graphics.renderQuality` · 품질 프리셋 이름과 같다 — 옵션이 `gv_renderQuality` 를 넣고
 *          (명령줄 `-gv_renderQuality=low` 가 이긴다) 바뀌면 `ConfigManager::applyRenderQuality` 가 제자리에서 다시 읽는다. 빈 이름이면 층이 없다(바탕 그대로).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @struct PlatformConfigOverride
     * @brief 플랫폼 덮어쓰기의 경로와 합치기 규칙 한 곳입니다.
     */
    struct SW_API PlatformConfigOverride
    {
        /** @brief 플랫폼 @p platformName 의 엔진 설정 덮어쓰기 파일(프로젝트 루트 기준 — `Config/Platform/<플랫폼>/EngineConfig.json`)입니다. */
        [[nodiscard]] static string makeEngineConfigPath( string_view platformName );
        /** @brief 이 빌드 플랫폼(`build::kPlatformName`)의 덮어쓰기 파일입니다. */
        [[nodiscard]] static string makeEngineConfigPath();

        /**
         * @brief @p baseJSON 위에 @p overrideJSON 을 덮은 글을 @p outJSON 에 씁니다.
         * @details 둘 다 객체인 칸은 키마다 안으로 들어가고, 그 밖(값 · 배열 · 객체를 값으로 바꾸기)은 덮어쓰기 쪽이 통째로 이긴다 — 배열의 원소 단위 더하기 · 빼기는 없다
         *          (언리얼 ini 의 `+` · `-` 자리. 쓰는 곳이 생기면 그때 들인다). 바탕에 없는 키도 그대로 옮긴다 — 모르는 키는 뒤의 엄격 읽기가 이름과 함께 알린다.
         * @return 둘 중 하나가 JSON 객체가 아니면 @p pOverrideLabel 과 함께 오류를 남기고 false 입니다.
         */
        [[nodiscard]] static bool apply( string_view baseJSON, string_view overrideJSON, const utf8* pOverrideLabel, string& outJSON );

        /**
         * @brief 구운 엔진 설정(`shipping_host::kEngineConfigJSON`)에 구운 이 플랫폼의 덮어쓰기를 덮은 글입니다. 덮어쓰기가 없으면 바탕 그대로입니다.
         * @details 구운 사본을 읽는 곳(정적 구성의 설정 표 · 리소스 검색 순서)이 모두 이것을 써서 같은 값을 본다. 구운 글은 configure 가 JSON 객체인지 이미 봤다.
         * @return 합치지 못하면(오류를 남겼다) false 입니다.
         */
        [[nodiscard]] static bool composeBakedEngineConfig( string& outJSON );
    };

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
