/**
 * @file AutomationScratch.h
 * @brief 시나리오가 쓰는 리소스 폴더를 실행마다 새로 만드는 임시 사본(`Resource/scratch/…`, 저장소에 넣지 않음)으로 보냅니다.
 * @details 시나리오가 `Resource/` 원본에 쓰면 추적 중인 파일이 바뀌거나 잔여 파일이 남고, 중단된 실행의 잔여물이 다음 실행을 지게 한다(`new_material_2`).
 *          시나리오는 쓰는 폴더를 `<Scratch from="engine/automation/editor/assetmanage" as="scratch/assetmanage"/>` 로 선언하고, 실행기는 시작할 때
 *          사본 폴더를 지운 뒤 원본을 복사한다(`prepare`). 사본은 리소스 id 가 있는 보통 도메인 폴더라 편집기의 모든 경로(루트를 붙이는 곳 · 감시 · 색인)가 같게 본다.
 *          복사하면서 글 파일 안의 원본 id 를 사본 id 로 바꾸고(씬이 사본의 머티리얼을 가리키게), `.meta` 의 guid 는 새로 만든다(원본과 겹치지 않게).
 *          `Resource/scratch/` 는 `.gitignore` 에 있다. 설계와 남은 단위는 docs/plans/ScenarioWritesSourceTree.md 이다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
    /** @brief 시나리오 하나가 쓰는 폴더의 원본과 사본입니다(`<Scratch from as>`). 둘 다 리소스 id 입니다. */
    struct AutomationScratchFolder
    {
        string _sourceID{};  ///< 원본 폴더(추적 중인 픽스처)
        string _scratchID{}; ///< 사본 폴더. `scratch/` 로 시작해야 한다
    };
} // namespace sw

namespace sw
{
    /** @brief 시나리오 사본 폴더를 만듭니다. 상태가 없습니다. */
    struct SW_API AutomationScratch
    {
        static constexpr const utf8* kScratchDomain = "scratch"; ///< 사본이 사는 도메인(`Resource/scratch/`)

        /** @brief `<Scratch>` 의 두 id 가 맞는 모양인지 봅니다(빈 값 · `..` · 사본이 `scratch/` 밖 · 원본이 `scratch/` 안이면 false 와 이유). */
        [[nodiscard]] static bool isValidFolder( const AutomationScratchFolder& folder, string& outReason );
        /**
         * @brief 사본 폴더를 지우고 원본을 복사합니다. 글 파일의 원본 id 는 사본 id 로 바꾸고 `.meta` 의 guid 는 새로 만듭니다.
         * @return 원본이 없거나 지우기 · 복사 · 쓰기가 실패하면 false 와 이유입니다(시나리오는 시작하지 않는다 — 원본에 쓰는 길로 돌아가지 않는다).
         */
        [[nodiscard]] static bool prepare( const AutomationScratchFolder& folder, string& outError );
        /**
         * @brief 사본 폴더를 끝까지 만든 횟수입니다(`prepare` 성공마다 하나 오른다). 사본을 만든 뒤 OS 파일 감시가 그 쓰기를 알려야 한다 — 에디터가 이 번호가 바뀌면
         *        감시 울타리를 다시 세운다(`AssetHotReload` 의 시작 차단 조건).
         */
        [[nodiscard]] static uint64 getPrepareCount();
        /** @brief 글 @p text 안의 @p fromID 를 @p toID 로 바꿉니다. id 경계(앞이 id 글자가 아닌 곳)에서만 바꿉니다. */
        static string rebaseText( string_view text, string_view fromID, string_view toID );
    };
} // namespace sw
