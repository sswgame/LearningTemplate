/**
 * @file EditorLightProbeBakeCommand.cpp
 * @brief 에디터 커맨드 "Bake Light Probes"(`lighting.bakeProbes`) — Build 메뉴와 커맨드 팔레트에서 활성 씬의 라이트 프로브 볼륨을 베이크합니다.
 * @details 선택한 오브젝트 중 볼륨이 있으면 그것만, 없으면 씬의 모든 볼륨을 베이크한다(언리얼 Build Lighting 의 선택 베이크와 같은 구분).
 *          커맨드는 베이크 세션(`EditorLightProbeBakeSession`)을 시작만 한다 — 베이크는 에디터 프레임에 나눠 돌고(나눠 베이크하는 규칙은 그 헤더), 진행은 상태줄에 보인다.
 */
#include "pch.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Commands/Scene/EditorLightProbeBakeSession.h"
#include "Editor/Common/GUI/Theme/EditorIconGlyphs.h"
#include "Editor/Common/Workspace/EditorService.h"

namespace sw::editor
{
    namespace
    {
        struct EditorLightProbeBakeCommandInternal
        {
            static void runBake()
            {
                editor::getSubsystem<EditorLightProbeBakeSession>().start(); // 필수 - 에디터 서브시스템이다
            }

            /** @brief 씬이 있을 때 켠다. 편집 허용 답(플레이 중 · 베이크 중이면 꺼진다)은 레지스트리가 함께 묻는다(`EditorCommandEdit::Scene`). */
            static bool isEnabled() { return editor::getActiveObjectManager() != nullptr; }
        };
    } // namespace

    SW_EDITOR_COMMAND( LightProbeBake, "lighting.bakeProbes", 3310, "Bake Light Probes", editoricon::kLightGlobal, "Lighting",
                       "선택한(없으면 모든) 라이트 프로브 볼륨의 간접광을 베이크합니다(씬 옆 .probes)", "Bake the selected (or every) light probe volume of the active scene", nullptr,
                       &EditorLightProbeBakeCommandInternal::runBake, &EditorLightProbeBakeCommandInternal::isEnabled, Scene, "MainMenu/Build", None, 0 );
} // namespace sw::editor
