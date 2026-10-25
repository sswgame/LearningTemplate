/**
 * @file EditorAssetImportStatus.cpp
 * @brief 상태줄 항목 — 바뀐 원본(모델 · 텍스처 · 높이장)을 워커에서 임포트하는 동안 "Importing assets: 1 running, 2 queued" 를 보입니다(`AssetHotReload`).
 * @details 임포트하지 않을 때는 아무것도 그리지 않는다. 임포트는 몇 초에서 몇 분 걸리는데 에디터 스레드에서 돌지 않으므로(프레임이 멈추지 않는다) 돈다는 것만 알린다.
 */
#include "pch.h"

#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/EditorStatusBar.h"
#include "Editor/Common/Workspace/AssetHotReload.h"
#include "Editor/Common/Workspace/EditorService.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        void drawAssetImportStatus()
        {
            // 상태줄은 에디터 컨텍스트가 활성일 때만 그려지고 AssetHotReload 는 그 범위의 서브시스템이다 — 없으면 버그.
            const AssetHotReload* pHotReload = &editor::getSubsystem<AssetHotReload>();
            if ( pHotReload->isImportBusy() == false )
                return;
            fixed_string<constant::kMaxBuffer128> label;
            formatstring( label.data(), label.capacity(), "Importing assets: %# running, %# queued", static_cast<uint32>( pHotReload->getRunningImportCount() ),
                          static_cast<uint32>( pHotReload->getQueuedImportCount() ) );
            ImGui::TextUnformatted( label.c_str() );
        }
    } // namespace

    SW_EDITOR_STATUS_BAR_ITEM( AssetImport, "asset_import", 95, &drawAssetImportStatus );
} // namespace sw::editor
