/**
 * @file ModulesWatches.cpp
 * @brief 모듈 창의 자동화 감시 값입니다 — `Editor.ModulePreviewNewlyInactive`.
 * @details 감시 값 레지스트리(`Engine/Automation/Watch`)가 기능 층보다 아래라 기능이 자기 감시 값을 등록합니다.
 */
#include "pch.h"

#include "Editor/Panels/Modules/ModulesPanel.h"

#include "Engine/Automation/Watch/AutomationWatch.h"

namespace sw::editor
{
    namespace
    {
        /** @brief 모듈 창의 미리보기에서 새로 꺼지는 모듈 수입니다. */
        [[nodiscard]] bool readModulePreviewNewlyInactive( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            outValue = static_cast<float64>( ModulesPanel::getPreviewNewlyInactiveCount() );
            return true;
        }
    } // namespace

    SW_AUTOMATION_WATCH( editorModulePreviewNewlyInactive, "Editor.ModulePreviewNewlyInactive", "Modules the Modules window's preview would turn off (0 when not asking)",
                         &readModulePreviewNewlyInactive );
} // namespace sw::editor
