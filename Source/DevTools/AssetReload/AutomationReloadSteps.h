/**
 * @file DevTools/AssetReload/AutomationReloadSteps.h
 * @brief 플레이 중 리로드를 일으키는 시나리오 단계입니다 — `<RebuildModule name variant>`(모듈을 다시 지어 갈아 끼움) · `<OverwriteConfig configFile from>`(설정 파일 덮어쓰기) ·
 *        `<RestoreOverwrites/>`(맡긴 파일을 지금 되돌림).
 * @details 바꾼 파일은 원본 글을 실행기에 맡기고(`IAutomationStepContext::restoreFileAtEnd`) 시나리오가 어떻게 끝나든 되돌린다. 추적 소스는 고치지 않는다 — 모듈 판은
 *          빌드 트리의 판 머리(`<빌드>/generated/…`, CMake 가 기본 글로 만든다)를 덮는다. 설정은 쓰기 게이트가 연 폴더(`Config/Editor`)만 쓴다 — 게이트가 그 밖을 거절한다.
 *          셰이더 · 에셋은 `<Scratch>` 사본을 `asset.overwriteScratch` 로 덮는다(원본 쓰기는 게이트가 막는다).
 *          언리얼 Live Coding 자동화와 유니티 Test Framework 의 "고친 뒤 되돌림"(정리 단계) 자리다. 판 표는 데이터(`Config/Dev/ModuleRebuildVariants.json`)이고 없으면 단계가 진다.
 */
#pragma once
#include "Core/Container/map.h"
#include "Core/Container/string.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "DevTools/DevToolsAPI.h"

#include "Engine/Config/ConfigFileID.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @struct ModuleRebuildVariant @brief 모듈 다시 짓기 판 하나입니다 — 빌드 트리의 판 머리(`generated/` 아래 상대 경로)에 쓸 글. */
    REFLECT()
    struct SW_DEVTOOLS_API ModuleRebuildVariant
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _header; ///< `<빌드>/generated/` 아래 상대 경로(예: `sw/dev/HotReloadProbeVariant.h`)
        PROPERTY( Required )
        string _text; ///< 그 자리에 쓸 글(컴파일 오류 판은 `#error` 줄)
    };

    /** @struct ModuleRebuildVariantTable @brief 판 표입니다 — `{ "_mapVariant": { "<판>": { "_header": "sw/dev/…h", "_text": "…" } } }`. */
    REFLECT()
    struct SW_DEVTOOLS_API ModuleRebuildVariantTable
    {
        REFLECT_BODY();

        static constexpr ConfigFileID<ModuleRebuildVariantTable> kConfigFile{ "Config/Dev/ModuleRebuildVariants.json" };

        PROPERTY( Required )
        map<string, ModuleRebuildVariant> _mapVariant;

        /** @brief 판마다 글이 비지 않았고 머리가 빌드 트리 `generated/` 아래 상대 경로인지 봅니다(`ConfigManager::readConfig` 가 부른다). */
        [[nodiscard]] expected<void> validate() const;
        /** @brief 이름이 @p variantName 인 판입니다. 없으면 까닭입니다. */
        [[nodiscard]] expected<ModuleRebuildVariant> find( string_view variantName ) const;
    };

    /**
     * @struct AutomationReloadSteps
     * @brief 리로드 단계의 순수 판정입니다(파일 · 실행 없이 단위 테스트가 부른다). `OverwriteConfig` · `RestoreOverwrites` 는 이 .cpp 가, `RebuildModule` 은
     *        `ModuleReload/ModuleRebuildStep.cpp` 가 등록한다 — 정적 모듈 구성(`SW_STATIC_MODULES`)은 모듈 리로드 폴더를 짓지 않아 그 단계가 없다.
     */
    struct SW_DEVTOOLS_API AutomationReloadSteps
    {
        /** @brief 비지 않았고, 절대 경로가 아니며, `..` 마디가 없는 상대 경로면 true 입니다(글 연산 — 판 머리 · 설정 파일 경로의 공통 판정). */
        [[nodiscard]] static bool isPlainRelativePath( string_view path );
        /** @brief `OverwriteConfig configFile` 이 `Config/` 아래 `.json` 상대 경로인지 봅니다(빈 글 · 절대 경로 · `..` · 다른 확장자는 오류). 쓰기 허락은 쓰기 게이트가 따로 본다. */
        [[nodiscard]] static expected<void> validateConfigFile( string_view file );
    };
} // namespace sw
