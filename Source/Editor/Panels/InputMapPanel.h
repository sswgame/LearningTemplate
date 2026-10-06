/**
 * @file InputMapPanel.h
 * @brief InputMap XML 을 시각적으로 편집하는 ImGui 패널입니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "Editor/Common/Gui/IEditorPanel.h"

#include "Engine/Input/InputMap.h"
#include "Engine/Input/InputReplay.h"

namespace sw::editor
{
    /**
     * @class InputMapPanel
     * @brief 액션, 레이어, 바인딩, 트리거, 모디파이어를 시각적으로 편집하고, 입력 장치 상태를 실시간으로 보여 주는 에디터 패널입니다.
     */
    class InputMapPanel : public IEditorPanel
    {
    public:
        InputMapPanel();
        virtual ~InputMapPanel() override = default;

        const utf8* getPanelTitle() const override { return "Input Map Editor"; }
        void        drawContent() override;
        bool        isToolPanel() const override { return true; }

        /**
         * @brief InputMap XML 을 씁니다.
         * @details dirty 비트는 기반 클래스가 듭니다 — 그래서 Ctrl+S(`saveFocusedOrScene`)와 종료 확인이 이 패널의 편집을 봅니다.
         */
        [[nodiscard]] bool saveDocument() override;
        void               revertDocument() override;

    private:
        void drawInputMapTab();
        void drawDeviceMonitorTab();

        /** @brief 키보드 실시간 상태를 그립니다. */
        void drawKeyboardMonitor();
        /** @brief 마우스 실시간 상태를 그립니다. */
        void drawMouseMonitor();
        /** @brief 게임패드 실시간 상태를 그립니다. */
        void drawGamepadMonitor();
        void drawConflictMatrixTab();
        /**
         * @brief 선택한 액션의 바인딩을 새 키로 바꿉니다. **바꾸기 전에 충돌을 확인합니다.**
         * @details 키 감지 · 버튼 격자 모두 이것을 거칩니다. 이미 다른 액션이 쓰는 키면(`InputMap::hasBindingConflict`) 되돌리지는 않고
         *          (덮어쓰기를 원할 수 있습니다) 무엇과 부딪히는지 알립니다.
         */
        void rebindSelectedAction( sw::Key newKey );
        void drawInputGraphTab();
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

        void               reloadFromFile();
        [[nodiscard]] bool saveToFile();

    private:
        static constexpr size_t kPlotSampleCount = 120;

        InputMap               _inputMap;
        InputReplay            _replay;
        string                 _inputMapPath;
        string                 _replayFilePath;
        string                 _newActionName;
        string                 _selectedAction;
        string                 _testComboPattern;
        float32                _arrPlotLeftStickX[kPlotSampleCount];
        float32                _arrPlotLeftStickY[kPlotSampleCount];
        float32                _arrPlotMouseDeltaX[kPlotSampleCount];
        float32                _arrPlotMouseDeltaY[kPlotSampleCount];
        float32                _arrPlotTriggerL[kPlotSampleCount];
        float32                _arrPlotTriggerR[kPlotSampleCount];
        float32                _testVibLeft;
        float32                _testVibRight;
        float2                 _simStick;
        uint32                 _plotOffset;
        uint32                 _recordedBeginFrameCount; ///< 마지막으로 녹화한 입력 프레임(`InputManager::getBeginFrameCount`) — 같은 프레임을 두 번 적지 않는다
        uint32                 _capturingBindIndex;
        int32                  _newActionValueType;
        int32                  _simKeyToInject;
        int32                  _selectedGlyphPlatform;
        uint8                  _bLoaded       : 1;
        uint8                  _bCapturingKey : 1;
        uint8                  _bPlotPaused   : 1;
        [[maybe_unused]] uint8 _reserved      : 5;
    };
} // namespace sw::editor
