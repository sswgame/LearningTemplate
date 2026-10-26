/**
 * @file AutomationScratch.h
 * @brief 시나리오가 쓰는 리소스 폴더를 실행마다 새로 만드는 임시 사본(`Resource/scratch/…`, 저장소에 넣지 않음)으로 보냅니다.
 * @details 시나리오가 `Resource/` 원본에 쓰면 추적 중인 파일이 바뀌거나 잔여 파일이 남고, 중단된 실행의 잔여물이 다음 실행을 지게 한다(`new_material_2`).
 *          시나리오는 쓰는 폴더를 `<Scratch from="engine/automation/editor/assetmanage" as="scratch/assetmanage"/>` 로 선언하고, 실행기는 시작할 때
 *          사본 id 에 프로세스 꼬리를 붙여(`scratch/assetmanage_p<pid>`, `makeInstanceID` — 시나리오 단계의 id 도 같이 바뀐다, `AutomationScenario::bindScratchInstances`)
 *          사본 폴더를 지운 뒤 원본을 복사한다(`prepare`). 저장소의 `Resource/` 는 같은 체크아웃의 모든 App 이 나눠 쓰므로 이름이 고정이면 동시에 도는 App 이
 *          서로의 사본을 지운다. 사본은 리소스 id 가 있는 보통 도메인 폴더라 편집기의 모든 경로(루트를 붙이는 곳 · 감시 · 색인)가 같게 본다.
 *          복사하면서 글 파일 안의 원본 id 를 사본 id 로 바꾸고(씬이 사본의 머티리얼을 가리키게), `.meta` 의 guid 는 새로 만든다(원본과 겹치지 않게).
 *          `Resource/scratch/` 는 `.gitignore` 에 있다. 설계와 남은 단위는 docs/09_Decisions.md 5-11 시나리오 사본 폴더 이다.
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
         * @brief 이 프로세스가 쓸 이름(`<이름>_p<pid>`)입니다 — `scratch/assetmanage` → `scratch/assetmanage_p1234`. 같은 시나리오를 도는 App 이 동시에 여럿이어도
         *        서로의 사본(과 울타리 표지)을 지우지 않는다. 같은 프로세스 안에서는 늘 같은 이름이라 체인의 뒤 단계가 같은 사본을 다시 만든다.
         */
        static string makeInstanceID( string_view name );
        /**
         * @brief 사본 폴더를 지우고 원본을 복사합니다. 글 파일의 원본 id 는 사본 id 로 바꾸고 `.meta` 의 guid 는 새로 만듭니다.
         * @details 사본 id 가 프로세스 꼬리(`makeInstanceID`)를 가졌으면, 같은 이름의 다른 사본 가운데 끝난 프로세스의 것을 먼저 지운다(죽은 실행의 사본이 쌓이지 않게).
         * @return 원본이 없거나 지우기 · 복사 · 쓰기가 실패하면 false 와 이유입니다(시나리오는 시작하지 않는다 — 원본에 쓰는 길로 돌아가지 않는다).
         */
        [[nodiscard]] static bool prepare( const AutomationScratchFolder& folder, string& outError );
        /**
         * @brief 사본 폴더를 끝까지 만든 횟수입니다(`prepare` 성공마다 하나 오른다). 사본을 만든 뒤 OS 파일 감시가 그 쓰기를 알려야 한다 — 에디터가 이 번호가 바뀌면
         *        감시 울타리를 다시 세운다(`AssetHotReload` 의 시작 차단 조건).
         */
        [[nodiscard]] static uint64 getPrepareCount();
        /**
         * @brief 글 @p text 안의 @p fromID 를 @p toID 로 바꿉니다. id 경계에서만 바꿉니다 — 앞은 id 글자가 아니고, 뒤는 글 끝 · `/` · id 글자가 아닌 곳이다.
         * @details 뒤 경계가 없으면 `scratch/a` 가 `scratch/ab/…` · `scratch/a_p12/…` 의 앞머리도 바꿔 다른 폴더를 가리키게 된다.
         */
        static string rebaseText( string_view text, string_view fromID, string_view toID );
    };
} // namespace sw
