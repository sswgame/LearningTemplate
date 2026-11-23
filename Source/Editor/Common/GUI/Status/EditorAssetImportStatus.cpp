/**
 * @file EditorAssetImportStatus.cpp
 * @brief 상태줄 항목 — 바뀐 원본(모델 · 텍스처 · 높이장)을 워커에서 임포트하는 동안 "Importing assets: 1 running, 2 queued" 를 보입니다(`AssetHotReload`).
 * @details 임포트하지 않을 때는 아무것도 그리지 않는다. 임포트는 몇 초에서 몇 분 걸리는데 에디터 스레드에서 돌지 않으므로(프레임이 멈추지 않는다) 돈다는 것만 알린다.
 */
#include "pch.h"

#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/Status/EditorStatusBar.h"
#include "Editor/Common/GUI/Status/Model/EditorStatusViewModel.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        void drawAssetImportStatus()
        {
            EditorAssetImportStatus status;
            if ( EditorStatusViewModel::gatherAssetImport( status ) == false )
                return;
            fixed_string<constant::buffer::kMax128> label;
            formatstring( label.data(), label.capacity(), "Importing assets: %# running, %# queued", status._runningCount, status._queuedCount );
            ImGui::TextUnformatted( label.c_str() );
        }
    } // namespace

    SW_EDITOR_STATUS_BAR_ITEM( AssetImport, "asset_import", 95, &drawAssetImportStatus );
} // namespace sw::editor
