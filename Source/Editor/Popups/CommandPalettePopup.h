#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"

namespace sw
{
	/** @brief 커맨드 팔레트 항목 */
	struct CommandPaletteEntry
	{
		string			 _category;
		string			 _label;
		string			 _detail;
		Delegate<void()> _action;
	};

	/**
	 * @class CommandPalettePopup
	 * @brief Ctrl+Shift+P / Ctrl+Space 로 열리는 글로벌 액션 & 오브젝트 & 윈도우 퍼지 검색기 (정적 클래스)
	 */
	class CommandPalettePopup
	{
	public:
		CommandPalettePopup()	= default;
		~CommandPalettePopup() = default;

		// Static Public API
		static void open();
		static void close();
		static void toggle();
		static bool isOpen();
		static void registerCommand( string_view category, string_view label, string_view detail,
									 Delegate<void()> action );
		static void draw();

		// Instance Implementations (owned by EditorContext)
		void openImpl();
		void closeImpl();
		void toggleImpl();
		bool isOpenImpl() const { return _bOpen; }
		void registerCommandImpl( string_view category, string_view label, string_view detail,
								  Delegate<void()> action );
		void drawImpl();

	private:
		void rebuildDynamicEntries();

	private:
		vector<CommandPaletteEntry> _listStaticCommands;
		vector<CommandPaletteEntry> _listAllCommands;
		utf8						_arrSearchBuffer[128]{ 0 };
		int32						_selectedIndex{ 0 };
		bool						_bOpen{ false };
		bool						_bJustOpened{ false };
	};
} // namespace sw
