/**
 * @file EditorReflectionProbeBakeCommand.cpp
 * @brief 에디터 커맨드 "Bake Reflection Probes"(`lighting.bakeReflectionProbes`) — Build 메뉴와 커맨드 팔레트에서 활성 씬의 반사 프로브를 GPU 로 베이크합니다(IndirectLighting.md P4).
 * @details 선택한 오브젝트 중 반사 프로브가 있으면 그것만, 없으면 씬의 모든 반사 프로브를 베이크한다(`lighting.bakeProbes` 와 같은 나눔).
 *          커맨드는 베이크 세션(`EditorReflectionProbeBakeSession`)을 시작만 한다 — 베이크는 에디터 프레임에 프로브 단위로 나눠 돌고(그 규칙은 세션 헤더),
 *          진행은 상태줄에 보인다. 결과는 씬 파일 옆 `<씬 이름>.<오브젝트 이름(소문자)>.reflprobe` 에 쓰고 프로브의 `_probeDataPath` 에 적는다.
 */
#include "pch.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Commands/Scene/EditorReflectionProbeBakeSession.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/Workspace/EditorService.h"

namespace sw::editor
{
    namespace
    {
        struct EditorReflectionProbeBakeCommandInternal
        {
            static void runBake()
            {
                EditorReflectionProbeBakeSession* pSession = editor::findSubsystem<EditorReflectionProbeBakeSession>();
                if ( pSession != nullptr )
                    pSession->start();
            }

            /** @brief 편집 상태에서만 켠다 — 플레이 중에 구우면 플레이가 움직인 오브젝트를 찍는다. 편집 허용 답(플레이 중 · 베이크 중)은 레지스트리가 함께 묻는다(`EditorCommandEdit::Scene`). */
            static bool isEnabled() { return editor::getActiveObjectManager() != nullptr; }
        };
    } // namespace

    SW_EDITOR_COMMAND( ReflectionProbeBake, "lighting.bakeReflectionProbes", 3320, "Bake Reflection Probes", editoricon::kLightGlobal, "Lighting",
                       "선택한(없으면 모든) 반사 프로브를 GPU 로 찍어 굽습니다(씬 옆 .reflprobe)", "Capture the selected (or every) reflection probe of the active scene on the GPU", {},
                       &EditorReflectionProbeBakeCommandInternal::runBake, &EditorReflectionProbeBakeCommandInternal::isEnabled, Scene, "MainMenu/Build" );
} // namespace sw::editor
