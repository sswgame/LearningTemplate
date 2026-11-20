/**
 * @file MemoryWatches.cpp
 * @brief Memory 창의 자동화 감시 값입니다 — `Editor.Memory*`. 창이 열려 있지 않으면 값을 내지 않습니다.
 */
#include "pch.h"

#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Panels/Memory/MemoryPanel.h"
#include "Editor/SelfTest/EditorWatchSupport.h"

#include "Engine/Automation/Watch/AutomationWatch.h"

namespace sw::editor
{
    namespace
    {
        struct MemoryWatchesInternal
        {
            /** @brief 열린 Memory 창입니다. 없거나 닫혀 있으면 nullptr 입니다. */
            static const MemoryPanel* findOpenPanel( const utf8* pWatch )
            {
                const EditorPanelManager* pPanels = getWatchPanelManager( pWatch );
                const MemoryPanel*        pPanel  = pPanels != nullptr ? pPanels->findPanel<MemoryPanel>() : nullptr;
                return pPanel != nullptr && pPanel->isOpen() ? pPanel : nullptr;
            }

            [[nodiscard]] static bool readSnapshotCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const MemoryPanel* pPanel = findOpenPanel( __func__ );
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getSnapshotCount() );
                return true;
            }

            [[nodiscard]] static bool readFinishedExportCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const MemoryPanel* pPanel = findOpenPanel( __func__ );
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getFinishedExportCount() );
                return true;
            }

            [[nodiscard]] static bool readTableRows( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const MemoryPanel* pPanel = findOpenPanel( __func__ );
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getVisibleRowCount() );
                return true;
            }

            [[nodiscard]] static bool readCompareMode( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const MemoryPanel* pPanel = findOpenPanel( __func__ );
                if ( pPanel == nullptr )
                    return false;
                outValue = pPanel->isCompareMode() ? 1.0 : 0.0;
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_WATCH( editorMemorySnapshotCount, "Editor.MemorySnapshotCount", "Snapshots the Memory panel holds", &MemoryWatchesInternal::readSnapshotCount );
    SW_AUTOMATION_WATCH( editorMemoryTableRows, "Editor.MemoryTableRows", "Rows of the Memory panel table drawn last (after filters)", &MemoryWatchesInternal::readTableRows );
    SW_AUTOMATION_WATCH( editorMemoryExportCount, "Editor.MemoryExportCount", "Finished CSV exports of the Memory panel (written or failed)", &MemoryWatchesInternal::readFinishedExportCount );
    SW_AUTOMATION_WATCH( editorMemoryCompareMode, "Editor.MemoryCompareMode", "1 while the Memory panel compares two snapshots", &MemoryWatchesInternal::readCompareMode );
} // namespace sw::editor
