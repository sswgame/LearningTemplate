/**
 * @file AutomationScratch.h
 * @brief 시나리오가 쓰는 리소스 폴더를 실행마다 새로 만드는 임시 사본(`Content/scratch/…`, 저장소에 넣지 않음)으로 보냅니다.
 * @details 시나리오가 `Content/` 원본에 쓰면 추적 중인 파일이 바뀌거나 잔여 파일이 남고, 중단된 실행의 잔여물이 다음 실행을 지게 한다(`new_material_2`).
 *          시나리오는 쓰는 폴더를 `<Scratch from="engine/automation/editor/assetmanage" as="scratch/assetmanage"/>` 로 선언하고, 실행기는 시작할 때
 *          사본 id 에 프로세스 꼬리를 붙여(`scratch/assetmanage_p<pid>`, `makeInstanceID` — 시나리오 단계의 id 도 같이 바뀐다, `AutomationScenario::bindScratchInstances`)
 *          사본 폴더를 지운 뒤 원본을 복사한다(`AutomationScratchWorkspace::prepare` — 개발 도구). 저장소의 `Content/` 는 같은 체크아웃의 모든 App 이 나눠 쓰므로 이름이 고정이면 동시에 도는 App 이
 *          서로의 사본을 지운다. 사본은 리소스 id 가 있는 보통 도메인 폴더라 편집기의 모든 경로(루트를 붙이는 곳 · 감시 · 색인)가 같게 본다.
 *          복사하면서 글 파일 안의 원본 id 를 사본 id 로 바꾼다(씬이 사본의 머티리얼을 가리키게 — 에셋은 경로 하나로 가리킨다).
 *          `Content/scratch/` 는 `.gitignore` 에 있다.
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
    struct AutomationScratchFolder
    {
        string _sourceID{};   ///< 원본 폴더(추적 중인 픽스처)
        string _scratchID{};  ///< 사본 폴더. `scratch/` 로 시작해야 한다
        string _declaredID{}; ///< 시나리오가 적은 사본 id(`as=`). 실행마다 프로세스 꼬리가 붙어 `_scratchID` 와 달라진다 — 복사한 글 파일 안의 이 id 도 사본 id 로 바꾼다
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 시나리오 사본 폴더의 id 규칙(선언 검사 · 프로세스 꼬리 · 글 안의 id 바꾸기)입니다. 상태가 없습니다.
     * @details 시나리오 형식(`AutomationScenario` — 엔진)이 읽을 때 쓰는 규칙만 둔다. 사본을 실제로 만들고 쓰기 문을 세우는 일은 개발 도구의 실행기 쪽
     *          `AutomationScratchWorkspace`(`DevTools/Client/Automation`) — 배포 구성에는 실행기가 없다.
     */
    struct SW_API AutomationScratch
    {
        static constexpr const utf8* kScratchDomain = AssetPaths::kScratchDomain; ///< 사본이 사는 도메인(`Content/scratch/`)

        /** @brief `<Scratch>` 의 두 id 가 맞는 모양인지 봅니다(빈 값 · `..` · 사본이 `scratch/` 밖 · 원본이 `scratch/` 안이면 그 이유). */
        [[nodiscard]] static expected<void> isValidFolder( const AutomationScratchFolder& folder );
        /**
         * @brief 이 프로세스가 쓸 이름(`<이름>_p<pid>`)입니다 — `scratch/assetmanage` → `scratch/assetmanage_p1234`. 같은 시나리오를 도는 App 이 동시에 여럿이어도
         *        서로의 사본(과 울타리 표지)을 지우지 않는다. 같은 프로세스 안에서는 늘 같은 이름이라 체인의 뒤 단계가 같은 사본을 다시 만든다.
         */
        static string makeInstanceID( string_view name );
        /**
         * @brief 리소스 id @p assetID 가 다른 프로세스의 사본(`scratch/<이름>_p<다른 pid>/…` — `makeInstanceID`)이면 true 입니다. 꼬리가 없거나 이 프로세스의 사본이면 false.
         * @details 나란히 도는 시나리오 실행(백엔드마다 하나)은 같은 `Content/scratch/` 에 저마다 사본을 만든다 — 에디터의 파일 감시는 남의 사본 쓰기를 무시해야 한다.
         */
        [[nodiscard]] static bool isOtherProcessInstance( string_view assetID );
        /**
         * @brief 리소스 루트 @p contentRoot 아래를 훑은 디스크 경로(파일 · 폴더) 목록에서 다른 프로세스의 사본(`isOtherProcessInstance`)을 뺍니다.
         * @details 리소스 트리를 디스크에서 훑는 곳(에셋 색인 · 콘텐츠 브라우저 · 참조 색인 · 참조 검사 · 시나리오 목록 · 애니메이션 세트 찾기)은 모두 이것을 거친다 —
         *          남의 사본(같은 체크아웃의 다른 App · 끝난 실행의 잔여)이 원본과 같은 파일 이름으로 보이면 Quick Open 이 사본을 집는 식으로 엉뚱한 파일을 고른다.
         */
        static void removeOtherProcessInstances( string_view contentRoot, vector<string>& inoutListPath );
        /** @brief 프로세스 꼬리의 앞머리(`<이름>_p<pid>` 의 `_p`)입니다. */
        static constexpr string_view kInstanceMark = "_p";
        /** @brief 폴더 이름 @p folderName 이 프로세스 꼬리(`<이름>_p<pid>` — `makeInstanceID`)로 끝나면 그 pid 입니다. 꼬리가 없거나 숫자가 아니면 비었습니다. */
        [[nodiscard]] static optional<int32> findInstanceProcessID( string_view folderName );
        /**
         * @brief 글 @p text 안의 @p fromID 를 @p toID 로 바꿉니다. id 경계에서만 바꿉니다 — 앞은 id 글자가 아니고, 뒤는 글 끝 · `/` · id 글자가 아닌 곳이다.
         * @details 뒤 경계가 없으면 `scratch/a` 가 `scratch/ab/…` · `scratch/a_p12/…` 의 앞머리도 바꿔 다른 폴더를 가리키게 된다.
         */
        static string rebaseText( string_view text, string_view fromID, string_view toID );
    };
} // namespace sw
