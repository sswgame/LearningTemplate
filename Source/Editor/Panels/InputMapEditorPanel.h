/**
 * @file InputMapEditorPanel.h
 * @brief ImGui 기반 InputMap XML 시각적 편집기 에디터 패널
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Gui/IEditorPanel.h"

#include "Engine/Input/ActionMap.h"

namespace sw::editor
{
	/**
	 * @class InputMapEditorPanel
	 * @brief 액션, 레이어, 바인딩, 트리거, 모디파이어를 시각적으로 편집하고 XML로 저장하는 에디터 윈도우
	 */
	class InputMapEditorPanel : public IEditorPanel
	{
	public:
		InputMapEditorPanel();
		virtual ~InputMapEditorPanel() override = default;

		const utf8* getPanelTitle() const override { return "Input Map Editor"; }
		void		drawContent() override;
		bool		isToolPanel() const override { return true; }

	private:
		void drawLayerList();
		void drawActionTable();
		void drawCaptureModal();
		void reloadFromFile();
		void saveToFile();

	private:
		ActionMap			   _actionMap;
		fixed_string<128>	   _inputMapPath;
		fixed_string<64>	   _newActionName;
		fixed_string<64>	   _selectedAction;
		uint32				   _capturingBindIndex;
		uint8				   _bLoaded		  : 1;
		uint8				   _bCapturingKey : 1;
		uint8				   _bDirty		  : 1;
		[[maybe_unused]] uint8 _reserved	  : 5;
	};
} // namespace sw::editor
