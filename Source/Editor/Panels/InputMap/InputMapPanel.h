/**
 * @file InputMapPanel.h
 * @brief InputMap XML 을 시각적으로 편집하는 ImGui 패널입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Defines.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/GUI/Panel/IEditorPanel.h"
#include "Editor/Panels/InputMap/Model/InputMapDocument.h"

namespace sw
{
    class InputManager;
} // namespace sw

namespace sw::editor
{
    /**
     * @class InputMapPanel
     * @brief 액션, 레이어, 바인딩, 트리거, 모디파이어를 시각적으로 편집하고, 입력 장치 상태를 실시간으로 보여 주는 에디터 패널입니다.
     * @details 상태와 명령은 `InputMapDocument`(Model/, ImGui 없음)가 들고, 이 패널은 그리기 · 입력칸 · 문서 dirty 만 든다.
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
        [[nodiscard]] bool readSelectedBindingCount( uint32& outCount ) const { return _document.readSelectedBindingCount( outCount ); }
        /** @brief 프로브용입니다. 선택한 액션의 0번 바인딩의 발화 규칙(`ActionTrigger` 번호)을 반환합니다. 선택한 액션이 없으면 false 입니다. */
        [[nodiscard]] bool readSelectedTrigger( uint32& outTrigger ) const { return _document.readSelectedTrigger( outTrigger ); }
        /** @brief 프로브용입니다. 지난 프레임에 찾은 바인딩 충돌 수를 반환합니다. */
        uint32 getConflictCount() const { return static_cast<uint32>( _document.getConflicts().size() ); }

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

        /** @brief 잡은 키로 바인딩을 바꾸고, 바꿨으면 dirty 를 올립니다. */
        void finishKeyCapture( sw::Key newKey );
        /** @brief 맵 파일을 다시 읽고, 읽었으면 dirty 를 내립니다. */
        void reloadFromFile();
        /** @brief 맵 파일에 쓰고, 썼으면 dirty 를 내립니다. */
        [[nodiscard]] bool saveToFile();

    private:
        InputMapDocument       _document; ///< 편집하는 InputMap · 리플레이와 명령
        string                 _newActionName;
        utf8                   _arrActionFilter[constant::kMaxBuffer64]; ///< 액션 목록의 검색어
        string                 _testComboPattern;
        float32                _testVibLeft;
        float32                _testVibRight;
        float2                 _simStick;
        int32                  _newActionValueType;
        int32                  _simKeyToInject;
        uint8                  _bSelectFirstTab : 1; ///< 새 인스턴스의 첫 그리기 — ImGui 가 탭 막대 id 로 기억한 앞 인스턴스의 탭이 아니라 Mappings 로 연다
        [[maybe_unused]] uint8 _reserved        : 7;
    };
} // namespace sw::editor
