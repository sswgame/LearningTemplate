#pragma once
#include "Editor/Windows/IEditorWindow.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
	/**
	 * @brief 프리팹 인스턴스 컴포넌트 프로퍼티 오버라이드 항목
	 */
	struct PrefabOverrideItem
	{
		string _componentName;
		string _propertyName;
		string _defaultValue;
		string _overriddenValue;
		bool   _bModified{ false };
	};

	/**
	 * @brief 프리팹 인스턴스 오버라이드 검사, 복원/적용 및 중첩 프리팹 비주얼 관리 도구
	 */
	class PrefabEditorTool : public IEditorWindow
	{
	public:
		PrefabEditorTool();
		~PrefabEditorTool() override = default;

		bool		isToolWindow() const override { return true; }
		const utf8* getWindowTitle() const override { return "Prefab Inspector & Overrides"; }
		void		draw( const EditorUIContext& ctx ) override;

	private:
		void scanPrefabOverrides( const utf8* pPrefabPath );

		string					   _selectedPrefabPath;
		string					   _selectedInstanceName;
		vector<PrefabOverrideItem> _listOverrides;
		vector<string>			   _listNestedPrefabs;
		bool					   _bShowOnlyModified;
	};
} // namespace sw
