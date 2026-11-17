/**
 * @file AutomationScratch.h
 * @brief 시나리오가 쓰는 리소스 폴더를 실행마다 새로 만드는 임시 사본(작업 도메인 `scratch/…`)으로 보냅니다.
 * @details 시나리오가 `Content/` 원본에 쓰면 추적 중인 파일이 바뀌거나 잔여 파일이 남고, 중단된 실행의 잔여물이 다음 실행을 지게 한다(`new_material_2`).
 *          시나리오는 쓰는 폴더를 `<Scratch from="engine/automation/editor/assetmanage" as="scratch/assetmanage"/>` 로 선언하고, 실행기는 시작할 때
 *          사본 폴더를 지운 뒤 원본을 복사한다(`AutomationScratchWorkspace::prepare` — 개발 도구). 사본 id 는 적은 그대로다 — 작업 도메인의 디스크 자리가
 *          이 프로세스의 실행 뿌리 아래라(`AssetPaths::getScratchFolderPath`) 같은 체크아웃에서 나란히 도는 App 의 사본이 이 프로세스의 리소스 트리에 없다.
 *          사본은 리소스 id 가 있는 도메인 폴더라 편집기의 모든 경로(id ↔ 디스크 · 감시 · 색인)가 같게 본다.
 *          복사하면서 글 파일 안의 원본 id 를 사본 id 로 바꾼다(씬이 사본의 머티리얼을 가리키게 — 에셋은 경로 하나로 가리킨다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Content/AssetPaths.h"

namespace sw
{
    /** @brief 시나리오 하나가 쓰는 폴더의 원본과 사본입니다(`<Scratch from as>`). 둘 다 리소스 id 입니다. */
    struct SW_API AutomationScratchFolder
    {
        string _sourceID{};  ///< 원본 폴더(추적 중인 픽스처)
        string _scratchID{}; ///< 사본 폴더(시나리오가 적은 `as=` 그대로). `scratch/` 로 시작해야 한다

        /** @brief 두 id 가 맞는 모양인지 봅니다(빈 값 · `..` · 사본이 `scratch/` 밖 · 원본이 `scratch/` 안이면 그 이유). */
        [[nodiscard]] expected<void> validate() const;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 시나리오 사본 폴더의 id 규칙(도메인 이름 · 글 안의 id 바꾸기)입니다. 상태가 없습니다. 선언 검사는 `AutomationScratchFolder::validate` 다.
     * @details 시나리오 형식(`AutomationScenario` — 엔진)이 읽을 때 쓰는 규칙만 둔다. 사본을 실제로 만들고 쓰기 문을 세우는 일은 개발 도구의 실행기 쪽
     *          `AutomationScratchWorkspace`(`DevTools/Client/Automation`) — 배포 구성에는 실행기가 없다.
     */
    struct SW_API AutomationScratch
    {
        static constexpr const utf8* kScratchDomain = AssetPaths::kScratchDomain; ///< 사본이 사는 도메인(실행 뿌리 아래 — `AssetPaths::getScratchFolderPath`)
        static constexpr const utf8* kImportDomain  = "scratch/import";           ///< 에디터 가져오기 사본의 그릇(`scratch/import/<이름>`) — `asset.import` 가 받는 폴더는 이 아래뿐이다

        /**
         * @brief 글 @p text 안의 @p fromID 를 @p toID 로 바꿉니다. id 경계에서만 바꿉니다 — 앞은 id 글자가 아니고, 뒤는 글 끝 · `/` · id 글자가 아닌 곳이다.
         * @details 뒤 경계가 없으면 `scratch/a` 가 `scratch/ab/…` · `scratch/a_b/…` 의 앞머리도 바꿔 다른 폴더를 가리키게 된다.
         */
        static string rebaseText( string_view text, string_view fromID, string_view toID );
    };
} // namespace sw
