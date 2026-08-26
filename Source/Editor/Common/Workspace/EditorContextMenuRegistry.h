#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"

namespace sw
{
	/** @brief 컨텍스트 메뉴 표시 위치 */
	enum class ContextMenuLocation : uint8
	{
		Hierarchy = 0,
		ContentBrowser,
		Viewport,
		Inspector
	};

	/** @brief 개별 컨텍스트 메뉴 항목 */
	struct ContextMenuItem
	{
		string			 _path;
		string			 _shortcut;
		Delegate<void()> _action;
		Delegate<bool()> _enabledPredicate;
	};

	/**
	 * @class EditorContextMenuRegistry
	 * @brief 에디터 주요 패널(Hierarchy, Content Browser 등)의 우클릭 컨텍스트 메뉴를 동적으로 확장하는 정적 레지스트리
	 */
	class EditorContextMenuRegistry
	{
	public:
		EditorContextMenuRegistry()	 = default;
		~EditorContextMenuRegistry() = default;

		// Static Public API
		static void registerItem( ContextMenuLocation location, string_view path, Delegate<void()> action,
								  string_view shortcut = "", Delegate<bool()> enabledPredicate = {} );
		static void drawContextMenu( ContextMenuLocation location );

		// Instance Implementations (owned by EditorContext)
		void registerItemImpl( ContextMenuLocation location, string_view path, Delegate<void()> action,
							   string_view shortcut, Delegate<bool()> enabledPredicate );
		void drawContextMenuImpl( ContextMenuLocation location );

	private:
		vector<ContextMenuItem> _mapItems[4];
	};
} // namespace sw
