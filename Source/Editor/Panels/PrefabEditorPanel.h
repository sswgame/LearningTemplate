#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Common/Commands/EditorToolAssetCommands.h"
#include "Editor/Common/Gui/IEditorPanel.h"

namespace sw::editor
{
	/**
	 * @brief 프리팹 인스턴스 오버라이드 검사, 복원/적용 및 중첩 프리팹 비주얼 관리 도구
	 */
	class PrefabEditorPanel : public IEditorPanel
	{
	public:
		PrefabEditorPanel();
		~PrefabEditorPanel() override = default;

		bool		isToolPanel() const override { return true; }
		const utf8* getPanelTitle() const override { return "Prefab Editor"; }
		void		drawContent() override;
		float2		getInitialPanelSize() const override { return float2{ 650.0f, 480.0f }; }

	private:
		void scanPrefabOverrides( const utf8* pPrefabPath );

		string					   _selectedPrefabPath;
		string					   _selectedInstanceName;
		string					   _lastScanKey;
		vector<PrefabOverrideItem> _listOverride;
		vector<string>			   _listNestedPrefab;
		bool					   _bShowOnlyModified;
	};
} // namespace sw::editor
