/**
 * @file EditorStatusBarItems.cpp
 * @brief 에디터 본체의 상태줄 항목(MVVM View)입니다 — 라이트 프로브 · 반사 프로브 베이크 진행과 Cancel 단추, 원본 임포트 진행. 값은 `EditorStatusViewModel` 이 모은다.
 * @details 일이 돌지 않을 때 항목은 아무것도 그리지 않는다. 세션은 명령 층(3)이고 그리기는 GUI 층(4)이라 항목을 여기 둔다.
 *          임포트는 몇 초에서 몇 분 걸리는데 에디터 스레드에서 돌지 않으므로(프레임이 멈추지 않는다) 돈다는 것만 알린다(`AssetHotReload`).
 *          기능 창이 가진 항목(맵 검사 수)은 그 창 파일이 같은 줄로 더한다.
 */
#include "pch.h"

#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/Status/EditorStatusBar.h"
#include "Editor/Common/GUI/Status/Model/EditorStatusViewModel.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct EditorStatusBarItemsInternal
        {
            /** @brief 라이트 프로브를 베이크하는 동안 "Baking probes 2/3 (41 / 100)" 와 Cancel 단추입니다(`EditorLightProbeBakeSession`). */
            static void drawLightProbeBake()
            {
                EditorProbeBakeStatus status;
                if ( EditorStatusViewModel::gatherLightProbeBake( status ) == false )
                    return;
                fixed_string<constant::buffer::kMax128> label;
                formatstring( label.data(), label.capacity(), "Baking probes %#/%# (%# / 100)", status._done, status._total, static_cast<uint32>( status._itemProgress * 100.0f ) );
                ImGui::TextUnformatted( label.c_str() );
                ImGui::SameLine();
                if ( ImGui::SmallButton( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdVerb", "Cancel" ), "Cancel##lightProbeBake" ) ) )
                    EditorStatusViewModel::cancelLightProbeBake();
                EditorSelfTestMarks::record( "statusBar.lightProbeBakeCancel" ); // 시나리오 lightprobebakecancel 이 누른다
            }

            /** @brief 반사 프로브를 베이크하는 동안 "Baking reflection probes 1/3" 과 Cancel 단추입니다(`EditorReflectionProbeBakeSession`). */
            static void drawReflectionProbeBake()
            {
                EditorProbeBakeStatus status;
                if ( EditorStatusViewModel::gatherReflectionProbeBake( status ) == false )
                    return;
                fixed_string<constant::buffer::kMax128> label;
                formatstring( label.data(), label.capacity(), "Baking reflection probes %#/%#", status._done, status._total );
                ImGui::TextUnformatted( label.c_str() );
                ImGui::SameLine();
                if ( ImGui::SmallButton( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdVerb", "Cancel" ), "Cancel##reflectionProbeBake" ) ) )
                    EditorStatusViewModel::cancelReflectionProbeBake();
            }

            /** @brief 바뀐 원본(모델 · 텍스처 · 높이장)을 워커에서 임포트하는 동안 "Importing assets: 1 running, 2 queued" 입니다. */
            static void drawAssetImport()
            {
                EditorAssetImportStatus status;
                if ( EditorStatusViewModel::gatherAssetImport( status ) == false )
                    return;
                fixed_string<constant::buffer::kMax128> label;
                formatstring( label.data(), label.capacity(), "Importing assets: %# running, %# queued", status._runningCount, status._queuedCount );
                ImGui::TextUnformatted( label.c_str() );
            }
        };
    } // namespace

    SW_EDITOR_STATUS_BAR_ITEM( LightProbeBake, "light_probe_bake", 90, &EditorStatusBarItemsInternal::drawLightProbeBake );
    SW_EDITOR_STATUS_BAR_ITEM( ReflectionProbeBake, "reflection_probe_bake", 91, &EditorStatusBarItemsInternal::drawReflectionProbeBake );
    SW_EDITOR_STATUS_BAR_ITEM( AssetImport, "asset_import", 95, &EditorStatusBarItemsInternal::drawAssetImport );
} // namespace sw::editor
