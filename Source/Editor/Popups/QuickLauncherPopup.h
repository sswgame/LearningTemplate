/**
 * @file QuickLauncherPopup.h
 * @brief 글로벌 퀵 애셋/오브젝트 검색 및 런처 팝업 (Ctrl+P)
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"

#include "Editor/Common/Gui/IEditorPopup.h"

namespace sw::editor
{
	/** @brief 퀵 런처 검색 항목 */
	struct QuickLauncherItem
	{
		string			 _category; /**< "Scene", "Prefab", "Texture", "Shader", "Data", "GameObject" */
		string			 _title;
		string			 _detail;
		string			 _path;
		uint64			 _targetObjectId{ 0 };
		Delegate<void()> _action;
	};

	/**
	 * @class QuickLauncherPopup
	 * @brief Ctrl+P 단축키로 열리는 글로벌 스마트 애셋 & 게임 오브젝트 퍼지 런처
	 */
	class QuickLauncherPopup : public IEditorPopup
	{
	public:
		QuickLauncherPopup();
		virtual ~QuickLauncherPopup() override = default;

		virtual const utf8* getPopupId() const override { return "QuickLauncher"; }
		virtual const utf8* getPopupTitle() const override { return "Quick Open"; }

		static void open();
		static void close();
		static void toggle();
		static bool isOpen();

	protected:
		virtual void drawContent() override;
		virtual void onOpen() override;

	private:
		void rebuildIndex();

	private:
		vector<QuickLauncherItem> _listAllItem;
		utf8					  _arrSearchBuffer[constant::kMaxBuffer128];
		int32					  _selectedIndex;
		bool					  _bJustOpened;
	};
} // namespace sw::editor
