#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/Commands/Asset/EditorToolAssetCommands.h"
#include "Editor/Common/GUI/IEditorPanel.h"
#include "Editor/Common/Workspace/EditorAssetType.h"

namespace sw::editor
{
    /**
     * @brief 프리팹 인스턴스의 오버라이드를 살펴보고 되돌리거나 적용하며, 중첩 프리팹을 보여 주는 도구입니다.
     */
    class PrefabPanel : public IEditorPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>` · `setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "prefab_editor";

        PrefabPanel();
        ~PrefabPanel() override = default;

        bool isToolPanel() const override { return true; }
        /** @brief 종류 테이블의 프리팹 패널 제목은 프리팹 창(`PrefabEditorPanel`)이 쓴다. 이 도구는 다른 제목을 둔다. */
        const utf8*    getPanelTitle() const override { return "Prefab Overrides"; }
        EditorDockSlot getDefaultDockSlot() const override { return EditorDockSlot::Center; }
        void           drawContent() override;

        /** @brief 중첩 프리팹·서브 애셋 섹션을 그립니다. */
        void drawNestedPrefabSection();
        /** @brief 프리팹 오버라이드 테이블을 그립니다. */
        void   drawOverrideTable();
        float2 getInitialPanelSize() const override { return float2{ 650.0f, 480.0f }; }

    private:
        void scanPrefabOverrides( const utf8* pPrefabPath );

        string                     _selectedPrefabPath;
        string                     _selectedInstanceName;
        string                     _lastScanKey;
        vector<PrefabOverrideItem> _listOverride;
        vector<string>             _listNestedPrefab;
        bool                       _bShowOnlyModified;
    };
} // namespace sw::editor
