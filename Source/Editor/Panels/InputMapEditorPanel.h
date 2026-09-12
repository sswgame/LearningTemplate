/**
 * @file InputMapEditorPanel.h
 * @brief ImGui 기반 InputMap XML 시각적 편집기 에디터 패널
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

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
        void        drawContent() override;
        bool        isToolPanel() const override { return true; }

        /**
         * @brief InputMap XML 을 씁니다.
         * @details 예전에는 이 패널이 자기 `_bDirty` 만 들고 문서 계약을 구현하지 않아서,
         *          화면에는 "* Unsaved changes" 를 띄우면서 Ctrl+S 는 이 파일이 아니라 **씬을**
         *          저장했고(`saveFocusedOrScene` 이 이 패널을 dirty 로 보지 못했다) 종료 확인도
         *          이 편집을 세지 않아 조용히 사라졌다. 이제 기반이 dirty 비트를 든다.
         */
        bool saveDocument() override;
        void revertDocument() override;

    private:
        void drawActionMapTab();
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
         * @details 예전에는 두 자리(키 감지·버튼 격자)에서 곧장 rebindKey 를 불러, 이미 다른 액션이 쓰는
         *          키로 바꿔도 아무 말이 없었다 — 같은 패널의 "Key Conflict Matrix" 탭이 그제서야 알려 준다.
         *          엔진에는 그 질문에 답하는 `ActionMap::hasBindingConflict` 가 이미 있었는데 아무도 부르지
         *          않았다. 되돌리지는 않는다(덮어쓰기를 원할 수 있다) — 대신 무엇과 부딪히는지 남긴다.
         */
        void rebindSelectedAction( sw::Key newKey );
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
        bool saveToFile();

    private:
        static constexpr size_t kPlotSampleCount = 120;

        ActionMap              _actionMap;
        InputReplay            _replay;
        string                 _inputMapPath;
        string                 _replayFilePath;
        string                 _newActionName;
        string                 _newLayerName;
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
