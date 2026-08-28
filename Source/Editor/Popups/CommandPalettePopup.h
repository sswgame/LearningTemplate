/**
 * @file CommandPalettePopup.h
 * @brief 글로벌 커맨드 팔레트 팝업 (IEditorPopup 구현체)
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"

#include "Editor/Common/Gui/IEditorPopup.h"

namespace sw::editor
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
	 * @brief Ctrl+Shift+P / Ctrl+Space 로 열리는 글로벌 액션 & 오브젝트 & 윈도우 퍼지 검색기
	 */
	class CommandPalettePopup : public IEditorPopup
	{
	public:
		CommandPalettePopup();
		virtual ~CommandPalettePopup() override = default;

		// ------------------------------------------------------------------------------
		// IEditorPopup 구현
		// ------------------------------------------------------------------------------
		virtual const utf8* getPopupId() const override { return "CommandPalette"; }
		virtual const utf8* getPopupTitle() const override { return "Command Palette"; }

		// ------------------------------------------------------------------------------
		// 정적(Static) 편의 API
		// ------------------------------------------------------------------------------
		static void open();
		static void close();
		static void toggle();
		static bool isOpen();
		static void registerCommand( string_view category, string_view label, string_view detail,
									 Delegate<void()> action );

		// ------------------------------------------------------------------------------
		// 인스턴스 메서드
		// ------------------------------------------------------------------------------
		void registerCommandInstance( string_view category, string_view label, string_view detail,
									  Delegate<void()> action );

	protected:
		virtual void drawContent() override;
		virtual void onOpen() override;

	private:
		void rebuildDynamicEntries();

	private:
		vector<CommandPaletteEntry> _listStaticCommand;
		vector<CommandPaletteEntry> _listAllCommand;
		utf8						_arrSearchBuffer[128]{ 0 };
		int32						_selectedIndex{ 0 };
		bool						_bJustOpened{ false };
	};
} // namespace sw::editor
