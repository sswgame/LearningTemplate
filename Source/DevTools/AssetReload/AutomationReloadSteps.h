/**
 * @file DevTools/AssetReload/AutomationReloadSteps.h
 * @brief 플레이 중 리로드를 일으키는 시나리오 단계입니다 — `<RebuildModule name variant>`(모듈을 다시 지어 갈아 끼움) · `<OverwriteConfig file from>`(설정 파일 덮어쓰기) ·
 *        `<RestoreOverwrites/>`(맡긴 파일을 지금 되돌림).
 * @details 바꾼 파일은 원본 글을 실행기에 맡기고(`AutomationStepContext::restoreFileAtEnd`) 시나리오가 어떻게 끝나든 되돌린다. 추적 소스는 고치지 않는다 — 모듈 판은
 *          빌드 트리의 판 머리(`<빌드>/generated/…`, CMake 가 기본 글로 만든다)를 덮는다. 설정은 쓰기 게이트가 연 폴더(`Config/Editor`)만 쓴다 — 게이트가 그 밖을 거절한다.
 *          셰이더 · 에셋은 `<Scratch>` 사본을 `asset.overwriteScratch` 로 덮는다(원본 쓰기는 게이트가 막는다).
 *          언리얼 Live Coding 자동화와 유니티 Test Framework 의 "고친 뒤 되돌림"(정리 단계) 자리다. 판 표는 데이터(`Config/Dev/ModuleRebuildVariants.json`)이고 없으면 단계가 진다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "DevTools/DevToolsAPI.h"

namespace sw
{
    /** @struct ModuleRebuildVariant @brief 모듈 다시 짓기 판 하나입니다 — 빌드 트리의 판 머리(`generated/` 아래 상대 경로)에 쓸 글. */
    struct ModuleRebuildVariant
    {
        string _header; ///< `<빌드>/generated/` 아래 상대 경로(예: `sw/dev/HotReloadProbeVariant.h`)
        string _text;   ///< 그 자리에 쓸 글(컴파일 오류 판은 `#error` 줄)
    };

    /**
     * @struct AutomationReloadSteps
     * @brief 리로드 단계의 순수 판정입니다(파일 · 실행 없이 단위 테스트가 부른다). `OverwriteConfig` · `RestoreOverwrites` 는 이 .cpp 가, `RebuildModule` 은
     *        `ModuleReload/ModuleRebuildStep.cpp` 가 등록한다 — 정적 모듈 구성(`SW_STATIC_MODULES`)은 모듈 리로드 폴더를 짓지 않아 그 단계가 없다.
     */
    struct SW_DEVTOOLS_API AutomationReloadSteps
    {
        /** @brief 판 표 파일입니다(프로젝트 루트 기준). */
        static constexpr const utf8* kVariantFile = "Config/Dev/ModuleRebuildVariants.json";

        /**
         * @brief 판 표 글에서 @p variantName 판을 읽습니다. 표가 JSON 이 아니거나, 그 판이 없거나, 칸(`_header` · `_text`)이 비었거나, 머리 경로가 `..` · 절대 경로면 오류입니다.
         * @details 표 모양: `{ "_variants": { "<판>": { "_header": "sw/dev/…h", "_text": "…" } } }`.
         */
        [[nodiscard]] static expected<ModuleRebuildVariant> parseVariant( string_view jsonText, string_view sourceName, string_view variantName );
        /** @brief 비지 않았고, 절대 경로가 아니며, `..` 마디가 없는 상대 경로면 true 입니다(글 연산 — 판 머리 · 설정 파일 경로의 공통 판정). */
        [[nodiscard]] static bool isPlainRelativePath( string_view path );
        /** @brief `OverwriteConfig file` 이 `Config/` 아래 `.json` 상대 경로인지 봅니다(빈 글 · 절대 경로 · `..` · 다른 확장자는 오류). 쓰기 허락은 쓰기 게이트가 따로 본다. */
        [[nodiscard]] static expected<void> validateConfigFile( string_view file );
    };
} // namespace sw
