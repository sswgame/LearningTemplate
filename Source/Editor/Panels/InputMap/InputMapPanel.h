/**
 * @file InputMapPanel.h
 * @brief InputMap XML 을 시각적으로 편집하는 ImGui 패널입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Defines.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/GUI/Panel/IEditorPanel.h"
#include "Editor/Common/Widgets/EditableAssetPath.h"
#include "Editor/Panels/InputMap/Model/InputMapConflicts.h"

#include "Engine/Input/Map/InputMap.h"
#include "Engine/Input/Virtual/InputReplay.h"

namespace sw
{
    class InputManager;
} // namespace sw

namespace sw::editor
{
    /**
     * @class InputMapPanel
     * @brief 액션, 레이어, 바인딩, 트리거, 모디파이어를 시각적으로 편집하고, 입력 장치 상태를 실시간으로 보여 주는 에디터 패널입니다.
     */
    class InputMapPanel : public IEditorPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>` · `setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "input_map";

        InputMapPanel();
        virtual ~InputMapPanel() override = default;

        const utf8*      getPanelTitle() const override { return "Input Map Editor"; }
        void             drawContent() override;
        bool             isToolPanel() const override { return true; }
        EditorTextExtent getInitialPanelSize() const override { return EditorTextExtent::ofText( 128.0f, 34.0f ); }

        /**
         * @brief InputMap XML 을 씁니다.
         * @details dirty 비트는 기반 클래스가 보관한다. 그래서 Ctrl+S(`saveFocusedOrScene`)와 종료 확인이 이 패널의 편집을 본다.
         */
        [[nodiscard]] bool saveDocument() override;
        void               revertDocument() override;

        /** @brief 프로브용입니다. 선택한 액션의 바인딩 수를 반환합니다. 선택한 액션이 없으면 false 입니다. */
        [[nodiscard]] bool readSelectedBindingCount( uint32& outCount ) const;
        /** @brief 프로브용입니다. 선택한 액션의 0번 바인딩의 발화 규칙(`ActionTrigger` 번호)을 반환합니다. 선택한 액션이 없으면 false 입니다. */
        [[nodiscard]] bool readSelectedTrigger( uint32& outTrigger ) const;
        /** @brief 프로브용입니다. 지난 프레임에 찾은 바인딩 충돌 수를 반환합니다. */
        uint32 getConflictCount() const { return static_cast<uint32>( _listConflict.size() ); }

    private:
        /** @brief Mappings 탭을 그립니다. 왼쪽은 레이어와 액션 목록이고 오른쪽은 선택한 액션의 바인딩, 발화 규칙, 충돌, 글리프입니다. 언리얼 Enhanced Input 의 한 화면에 해당합니다. */
        void drawMappingsTab();
        /** @brief 액션 목록을 그립니다. 선택, 충돌 표시, 상태를 보여 줍니다. */
        void drawActionList();
        /** @brief 선택한 액션의 바인딩, 발화 규칙, 충돌, 플랫폼 글리프를 그립니다. */
        void drawActionDetails();
        /** @brief 바인딩 한 줄(키 글리프 · 레이어 · 트리거 콤보 · 다시 잡기 · 시간 · 횟수 · Negate)을 그립니다. */
        void drawBindingRow( const hashed_string& action, uint32 bindIndex, const ActionBinding& binding );
        /** @brief 바인딩의 발화 규칙(트리거) 콤보입니다. */
        void drawTriggerCombo( const hashed_string& action, uint32 bindIndex );
        /** @brief 트리거 콤보 뒤의 발화 시간 · 누름 횟수 · Negate 필드입니다. 오른쪽 끝 @p rightEdgeX 에 안 들어가면 다음 줄로 내립니다. */
        void drawTriggerOptions( const hashed_string& action, uint32 bindIndex, const ActionBinding& binding, float32 rightEdgeX );
        /** @brief 선택한 액션이 낀 키 충돌 목록과 다시 잡기 · 비우기 버튼입니다. */
        void drawActionConflicts( const hashed_string& action );
        /** @brief 플랫폼별 버튼 글리프 목록입니다. */
        void drawPromptGlyphs( const hashed_string& action );
        /** @brief Debug 탭을 그립니다. 장치 상태, 그래프, 뷰포트 HUD, 명령 패턴 테스트를 접을 수 있는 섹션으로 둡니다. */
        void drawDebugTab();
        /** @brief 명령 패턴(숫자 패드 표기) 테스트 섹션을 그립니다. */
        void drawCommandPatternTester();
        void drawDeviceMonitorTab();

        /** @brief 키보드 실시간 상태를 그립니다. */
        void drawKeyboardMonitor();
        /** @brief 마우스 실시간 상태를 그립니다. */
        void drawMouseMonitor();
        /** @brief 게임패드 실시간 상태를 그립니다. */
        void drawGamepadMonitor();
        /**
         * @brief 선택한 액션의 바인딩을 새 키로 바꿉니다. 바꾸기 전에 충돌을 확인합니다.
         * @details 키 감지와 버튼 격자가 모두 이 함수를 거친다. 이미 다른 액션이 쓰는 키이면(`InputMap::hasBindingConflict`) 되돌리지는 않고
         *          무엇과 부딪히는지 알린다. 덮어쓰기를 원할 수 있기 때문이다.
         */
        void rebindSelectedAction( sw::Key newKey );
        void drawInputGraphTab();
        void drawInputSimulatorTab();
        void drawInputReplayTab();
        /** @brief 입력 녹화 시작 · 중지 줄입니다. */
        void drawRecordControls( InputManager& input, bool bPlaying );
        /** @brief 녹화한 입력의 재생 · 일시정지 · 한 프레임 이동 · 타임라인 슬라이더입니다. */
        void drawPlaybackControls( InputManager& input, bool bPlaying, uint32 currentFrame );
        void drawViewportOverlayTab();

        void drawLayerList();
        void drawAddActionSection();
        void drawCaptureModal();
        void drawGamepadStickVisualizer( const utf8* pLabel, float32 stickX, float32 stickY, float32 deadzone );

        void               reloadFromFile();
        [[nodiscard]] bool saveToFile();

    private:
        static constexpr size_t kPlotSampleCount = 120;

        InputMap                 _inputMap;
        InputReplay              _replay;
        vector<InputMapConflict> _listConflict;   ///< 이번 프레임의 바인딩 충돌이다. Mappings 탭이 다시 모은다
        EditableAssetPath        _inputMapPath;   ///< 편집하는 InputMap 리소스. 처음 값은 enginedefaultassets 의 디버그 키 맵, 칸에서 고친 글은 확정해야 읽기 · 쓰기에 쓰인다
        EditableAssetPath        _replayFilePath; ///< Save · Load 가 쓰는 .swreplay 리소스. 처음 값은 editortooldefaults 의 `_inputReplayFile`
        string                   _newActionName;
        string                   _selectedAction;
        utf8                     _arrActionFilter[constant::kMaxBuffer64]; ///< 액션 목록의 검색어
        string                   _testComboPattern;
        float32                  _arrPlotLeftStickX[kPlotSampleCount];
        float32                  _arrPlotLeftStickY[kPlotSampleCount];
        float32                  _arrPlotMouseDeltaX[kPlotSampleCount];
        float32                  _arrPlotMouseDeltaY[kPlotSampleCount];
        float32                  _arrPlotTriggerL[kPlotSampleCount];
        float32                  _arrPlotTriggerR[kPlotSampleCount];
        float32                  _testVibLeft;
        float32                  _testVibRight;
        float2                   _simStick;
        uint32                   _plotOffset;
        uint32                   _recordedBeginFrameCount; ///< 마지막으로 녹화한 입력 프레임(`InputManager::getBeginFrameCount`)이다. 같은 프레임을 두 번 기록하지 않는다
        uint32                   _capturingBindIndex;
        int32                    _newActionValueType;
        int32                    _simKeyToInject;
        uint8                    _bLoaded         : 1;
        uint8                    _bCapturingKey   : 1;
        uint8                    _bPlotPaused     : 1;
        uint8                    _bSelectFirstTab : 1; ///< 새 인스턴스의 첫 그리기 — ImGui 가 탭 막대 id 로 기억한 앞 인스턴스의 탭이 아니라 Mappings 로 연다
        [[maybe_unused]] uint8   _reserved        : 4;
    };
} // namespace sw::editor
