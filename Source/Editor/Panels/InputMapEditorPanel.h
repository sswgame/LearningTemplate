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
#include "Engine/Input/InputReplay.h"

namespace sw::editor
{
	/**
	 * @class InputMapEditorPanel
	 * @brief 액션, 레이어, 바인딩, 트리거, 모디파이어를 시각적으로 편집하고 실시간 장치 상태를 모니터링하는 통합 에디터 패널
	 */
	class InputMapEditorPanel : public IEditorPanel
	{
	public:
		InputMapEditorPanel();
		virtual ~InputMapEditorPanel() override = default;

		const utf8* getPanelTitle() const override { return "Input & ActionMap Editor"; }
		void		drawContent() override;
		bool		isToolPanel() const override { return true; }

	private:
		void drawActionMapTab();
		void drawDeviceMonitorTab();
		void drawConflictMatrixTab();
		void drawOscilloscopeTab();
		void drawInputSimulatorTab();
		void drawInputReplayTab();
		void drawGlyphPreviewerTab();
		void drawViewportOverlayTab();
		void drawCombosAndBufferTab();

		void drawLayerList();
		void drawActionTable();
		void drawAddActionSection();
		void drawCaptureModal();
		void drawGamepadStickVisualizer( const utf8* pLabel, float32 stickX, float32 stickY, float32 deadzone );

		void reloadFromFile();
		void saveToFile();

	private:
		static constexpr size_t kPlotSampleCount = 120;

		ActionMap			   _actionMap;
		InputReplay			   _replay;
		fixed_string<128>	   _inputMapPath;
		fixed_string<128>	   _replayFilePath;
		fixed_string<64>	   _newActionName;
		fixed_string<64>	   _newLayerName;
		fixed_string<64>	   _selectedAction;
		fixed_string<64>	   _testComboPattern;
		float32				   _arrPlotLeftStickX[kPlotSampleCount];
		float32				   _arrPlotLeftStickY[kPlotSampleCount];
		float32				   _arrPlotMouseDeltaX[kPlotSampleCount];
		float32				   _arrPlotMouseDeltaY[kPlotSampleCount];
		float32				   _arrPlotTriggerL[kPlotSampleCount];
		float32				   _arrPlotTriggerR[kPlotSampleCount];
		float32				   _testVibLeft;
		float32				   _testVibRight;
		float32				   _simStickX;
		float32				   _simStickY;
		uint32				   _plotOffset;
		uint32				   _capturingBindIndex;
		int32				   _newActionValueType;
		int32				   _simKeyToInject;
		int32				   _selectedGlyphPlatform;
		uint8				   _bLoaded		  : 1;
		uint8				   _bCapturingKey : 1;
		uint8				   _bDirty		  : 1;
		uint8				   _bPlotPaused	  : 1;
		[[maybe_unused]] uint8 _reserved	  : 4;
	};
} // namespace sw::editor
