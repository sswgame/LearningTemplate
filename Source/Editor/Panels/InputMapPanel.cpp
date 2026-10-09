#include "pch.h"

#include "Editor/Panels/InputMapPanel.h"

#include "Core/Container/StringUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Gui/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Widgets/ViewportInputOverlay.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Panels/EditorPanelManager.h"

#include "Engine/Input/Devices/GamepadDevice.h"
#include "Engine/Input/Devices/KeyboardDevice.h"
#include "Engine/Input/Devices/MouseDevice.h"
#include "Engine/Input/GamepadButtonUtil.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputMap.h"
#include "Engine/Input/InputReplay.h"
#include "Engine/Input/KeyCodeUtil.h"

#include <imgui.h>

namespace sw::editor
{
    SW_EDITOR_PANEL( InputMapPanel, "input_map", EditorPanelCategory::Tool, 1800 );

    namespace
    {
        /** @brief 이 TU 전용 도우미 모음입니다(유니티 빌드에서 이름이 충돌하지 않도록 TU 이름을 붙입니다). */
        struct InputMapPanelInternal
        {
            /** @brief 표의 열 하나입니다. 폭이 0 이면 남는 폭을 나눠 가집니다(Stretch). */
            struct TableColumn
            {
                const utf8* _pLabel;
                float32     _width;
            };

            /** @brief 표를 열고 열 · 머리 줄을 둡니다. 열렸으면 true 이고, 그때 부르는 쪽이 `ImGui::EndTable` 을 부릅니다. */
            template <size_t kColumnCount>
            static bool beginColumnTable( const utf8* pTableId, const TableColumn ( &arrColumn )[kColumnCount], ImGuiTableFlags flags )
            {
                if ( ImGui::BeginTable( pTableId, static_cast<int32>( kColumnCount ), flags ) == false )
                    return false;
                for ( const TableColumn& column : arrColumn )
                {
                    if ( column._width > 0.0f )
                        ImGui::TableSetupColumn( column._pLabel, ImGuiTableColumnFlags_WidthFixed, column._width );
                    else
                        ImGui::TableSetupColumn( column._pLabel, ImGuiTableColumnFlags_WidthStretch );
                }
                ImGui::TableHeadersRow();
                return true;
            }

            /** @brief 새 줄을 열고 첫 열에 이름을 쓴 뒤 둘째 열로 갑니다(액션 · 레이어 표의 줄 머리). */
            static void beginNamedRow( const hashed_string& name )
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted( name.c_str() );
                ImGui::TableNextColumn();
            }

            /** @brief ImGui 키 하나와 엔진 키 하나의 짝입니다. 두 열거형에서 연속이 아닌 키만 표로 둡니다. */
            struct ImGuiKeyPair
            {
                ImGuiKey _imguiKey;
                Key      _key;
            };

            static constexpr ImGuiKeyPair kArrNamedKey[] = {
                {           ImGuiKey_Tab,            Key::Tab},
                {     ImGuiKey_LeftArrow,           Key::Left},
                {    ImGuiKey_RightArrow,          Key::Right},
                {       ImGuiKey_UpArrow,             Key::Up},
                {     ImGuiKey_DownArrow,           Key::Down},
                {        ImGuiKey_PageUp,         Key::PageUp},
                {      ImGuiKey_PageDown,       Key::PageDown},
                {          ImGuiKey_Home,           Key::Home},
                {           ImGuiKey_End,            Key::End},
                {        ImGuiKey_Insert,         Key::Insert},
                {        ImGuiKey_Delete,         Key::Delete},
                {     ImGuiKey_Backspace,      Key::Backspace},
                {         ImGuiKey_Space,          Key::Space},
                {         ImGuiKey_Enter,          Key::Enter},
                {        ImGuiKey_Escape,         Key::Escape},
                {      ImGuiKey_LeftCtrl,    Key::LeftControl},
                {     ImGuiKey_LeftShift,      Key::LeftShift},
                {       ImGuiKey_LeftAlt,        Key::LeftAlt},
                {     ImGuiKey_LeftSuper,      Key::LeftSuper},
                {     ImGuiKey_RightCtrl,   Key::RightControl},
                {    ImGuiKey_RightShift,     Key::RightShift},
                {      ImGuiKey_RightAlt,       Key::RightAlt},
                {    ImGuiKey_RightSuper,     Key::RightSuper},
                {          ImGuiKey_Menu,           Key::Menu},
                {    ImGuiKey_Apostrophe,     Key::Apostrophe},
                {         ImGuiKey_Comma,          Key::Comma},
                {         ImGuiKey_Minus,          Key::Minus},
                {        ImGuiKey_Period,         Key::Period},
                {         ImGuiKey_Slash,          Key::Slash},
                {     ImGuiKey_Semicolon,      Key::Semicolon},
                {         ImGuiKey_Equal,          Key::Equal},
                {   ImGuiKey_LeftBracket,    Key::LeftBracket},
                {     ImGuiKey_Backslash,      Key::Backslash},
                {  ImGuiKey_RightBracket,   Key::RightBracket},
                {   ImGuiKey_GraveAccent,          Key::Grave},
                {      ImGuiKey_CapsLock,       Key::CapsLock},
                {    ImGuiKey_ScrollLock,     Key::ScrollLock},
                {       ImGuiKey_NumLock,        Key::NumLock},
                {   ImGuiKey_PrintScreen,    Key::PrintScreen},
                {         ImGuiKey_Pause,          Key::Pause},
                { ImGuiKey_KeypadDecimal,  Key::NumpadDecimal},
                {  ImGuiKey_KeypadDivide,   Key::NumpadDivide},
                {ImGuiKey_KeypadMultiply, Key::NumpadMultiply},
                {ImGuiKey_KeypadSubtract, Key::NumpadSubtract},
                {     ImGuiKey_KeypadAdd,      Key::NumpadAdd},
                {   ImGuiKey_KeypadEnter,    Key::NumpadEnter},
            };

            /** @brief ImGuiKey 의 연속 구간(시작 · 개수)을 엔진 키의 연속 구간으로 옮깁니다. */
            static Key findPressedInRange( ImGuiKey firstImGuiKey, Key firstKey, int32 count )
            {
                for ( int32 offset = 0; offset < count; ++offset )
                {
                    if ( ImGui::IsKeyPressed( static_cast<ImGuiKey>( firstImGuiKey + offset ), false ) )
                        return static_cast<Key>( static_cast<int32>( firstKey ) + offset );
                }
                return Key::Unknown;
            }

            /**
             * @brief 이번 프레임에 눌린 키를 ImGui 에서 읽습니다. 없으면 `Key::Unknown` 입니다.
             * @details 바인딩 창은 ImGui 모달이라 떠 있는 동안 ImGui 가 키보드를 쥐고 있고(`WantCaptureKeyboard`), 에디터는 그 키를
             *          게임 입력으로 넘기지 않습니다(`ImGuiEditor::processEvent`). 그래서 `InputManager::wasKeyPressed` 를
             *          물으면 키가 영원히 오지 않습니다. 키는 창이 받은 곳에서 읽습니다.
             */
            static Key findPressedKey()
            {
                Key key = findPressedInRange( ImGuiKey_A, Key::A, 26 );
                if ( key == Key::Unknown )
                    key = findPressedInRange( ImGuiKey_0, Key::Digit0, 10 );
                if ( key == Key::Unknown )
                    key = findPressedInRange( ImGuiKey_F1, Key::F1, 12 );
                if ( key == Key::Unknown )
                    key = findPressedInRange( ImGuiKey_Keypad0, Key::Numpad0, 10 );
                if ( key != Key::Unknown )
                    return key;
                for ( const ImGuiKeyPair& pair : kArrNamedKey )
                {
                    if ( ImGui::IsKeyPressed( pair._imguiKey, false ) )
                        return pair._key;
                }
                return Key::Unknown;
            }

            /**
             * @brief 활성 입력 장치 종류의 표시 이름입니다.
             * @details 값을 먼저 넣고 switch 로 덮어쓰면 둘 중 하나는 늘 쓰이지 않는 저장이 됩니다(분석기가 열거자를 모두 알기
             *          때문에 default 도 쓰이지 않습니다). 값을 반환하는 함수로 두면 그런 곳이 생기지 않습니다.
             */
            static const utf8* glyphStyleName( InputGlyphStyle type )
            {
                switch ( type )
                {
                    case InputGlyphStyle::KeyboardMouse:
                        return "Keyboard & Mouse";
                    case InputGlyphStyle::GamepadXbox:
                        return "Xbox Gamepad";
                    case InputGlyphStyle::GamepadPlayStation:
                        return "PlayStation Gamepad";
                    case InputGlyphStyle::GamepadSwitch:
                        return "Nintendo Switch Gamepad";
                }
                return "Unknown";
            }

            /** @brief 게임패드 배터리 잔량의 표시 이름입니다. */
            static const utf8* batteryLevelName( GamepadBatteryLevel level )
            {
                switch ( level )
                {
                    case GamepadBatteryLevel::Empty:
                        return "Empty";
                    case GamepadBatteryLevel::Low:
                        return "Low";
                    case GamepadBatteryLevel::Medium:
                        return "Medium";
                    case GamepadBatteryLevel::Full:
                        return "Full (100%)";
                }
                return "Unknown";
            }
        };
    } // namespace

    SW_LOG_CALLER( "InputMapPanel" );

    InputMapPanel::InputMapPanel()
        : IEditorPanel( false ) // 필요할 때 여는 도구라 닫힌 채 시작한다(열린 채 시작하면 떠 있는 창으로 화면 가운데를 덮는다)
        , _inputMap{}
        , _replay{}
        , _inputMapPath{ "engine/input/default.input.xml" }
        , _replayFilePath{ "engine/replay/demo_01.swreplay" }
        , _newActionName{ "" }
        , _selectedAction{ "" }
        , _testComboPattern{ "236P" }
        , _arrPlotLeftStickX{}
        , _arrPlotLeftStickY{}
        , _arrPlotMouseDeltaX{}
        , _arrPlotMouseDeltaY{}
        , _arrPlotTriggerL{}
        , _arrPlotTriggerR{}
        , _testVibLeft{ 0.5f }
        , _testVibRight{ 0.5f }
        , _simStick{}
        , _plotOffset{ 0 }
        , _recordedBeginFrameCount{ 0 }
        , _capturingBindIndex{ 0 }
        , _newActionValueType{ 0 }
        , _simKeyToInject{ 1 }
        , _selectedGlyphPlatform{ 0 }
        , _bLoaded{ SW_FALSE }
        , _bCapturingKey{ SW_FALSE }
        , _bPlotPaused{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void InputMapPanel::drawContent()
    {
        if ( _bLoaded == SW_FALSE )
        {
            reloadFromFile();
            _bLoaded = SW_TRUE;
        }

        InputManager* pInput = getService<InputManager>();
        if ( pInput != nullptr && _inputMap.getInputManager() != pInput )
            _inputMap.setInputManager( pInput );

        // ActionPhase 상태 머신 · 커맨드 히스토리 · 버퍼 만료 타이머 등은 update() 안에서만 갱신되므로,
        // 프레임마다 불러야 액션 테이블 · 콤보 테스터 · 버퍼링 데모가 실제로 동작한다.
        if ( pInput != nullptr )
            _inputMap.update( ImGui::GetIO().DeltaTime );

        // 실시간 시계열 샘플링
        if ( pInput != nullptr && _bPlotPaused == SW_FALSE )
        {
            float32        lx = 0.0f, ly = 0.0f, lt = 0.0f, rt = 0.0f;
            GamepadDevice* pGamepad = pInput->getGamepad( 0 );
            if ( pGamepad != nullptr && pGamepad->isConnected() )
            {
                const float2 vecLeftStick1 = pGamepad->getLeftStick();
                lx                         = vecLeftStick1._x;
                ly                         = vecLeftStick1._y;
                lt                         = pGamepad->getLeftTrigger();
                rt                         = pGamepad->getRightTrigger();
            }

            int32      mdx = 0, mdy = 0;
            const int2 vecMouseDelta1 = pInput->getMouseDelta();
            mdx                       = vecMouseDelta1._x;
            mdy                       = vecMouseDelta1._y;

            _arrPlotLeftStickX[_plotOffset]  = lx;
            _arrPlotLeftStickY[_plotOffset]  = ly;
            _arrPlotMouseDeltaX[_plotOffset] = static_cast<float32>( mdx );
            _arrPlotMouseDeltaY[_plotOffset] = static_cast<float32>( mdy );
            _arrPlotTriggerL[_plotOffset]    = lt;
            _arrPlotTriggerR[_plotOffset]    = rt;

            _plotOffset = ( _plotOffset + 1 ) % kPlotSampleCount;
        }

        // 입력 녹화 — 입력 프레임마다 한 번, 그 프레임에 장치에 적용된 사건을 적는다.
        if ( _replay.isRecording() && pInput != nullptr && pInput->getBeginFrameCount() != _recordedBeginFrameCount )
        {
            _recordedBeginFrameCount = pInput->getBeginFrameCount();
            _replay.recordFrame( ImGui::GetIO().DeltaTime, pInput->getLastFrameEvents() );
        }
        // 재생(가상 입력으로 붙인 리플레이)이 끝나면 뗀다.
        if ( pInput != nullptr && pInput->getVirtualInput() == &_replay && _replay.isFinished( pInput->getVirtualFrameIndex() ) )
            pInput->detachVirtualInput();

        if ( ImGui::BeginTabBar( "InputEditorTabs" ) )
        {
            if ( ImGui::BeginTabItem( "Action Maps & Bindings" ) )
            {
                drawInputMapTab();
                ImGui::EndTabItem();
            }

            if ( ImGui::BeginTabItem( "Live Device Monitor" ) )
            {
                drawDeviceMonitorTab();
                ImGui::EndTabItem();
            }

            if ( ImGui::BeginTabItem( "Key Conflict Matrix" ) )
            {
                drawConflictMatrixTab();
                ImGui::EndTabItem();
            }

            if ( ImGui::BeginTabItem( "Input Graphs" ) )
            {
                drawInputGraphTab();
                ImGui::EndTabItem();
            }

            if ( ImGui::BeginTabItem( "Virtual Input Injector" ) )
            {
                drawInputSimulatorTab();
                ImGui::EndTabItem();
            }

            if ( ImGui::BeginTabItem( "Input Replay & QA Playback" ) )
            {
                drawInputReplayTab();
                ImGui::EndTabItem();
            }

            if ( ImGui::BeginTabItem( "Multi-Platform Glyph Preview" ) )
            {
                drawGlyphPreviewerTab();
                ImGui::EndTabItem();
            }

            if ( ImGui::BeginTabItem( "Viewport Overlay HUD" ) )
            {
                drawViewportOverlayTab();
                ImGui::EndTabItem();
            }

            if ( ImGui::BeginTabItem( "Combos & Input Buffering" ) )
            {
                drawCombosAndBufferTab();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }

    void InputMapPanel::drawInputMapTab()
    {
        ImGui::Text( "InputMap Resource:" );
        ImGui::SameLine();
        EditorWidgets::drawTextField( "##InputMapPath", _inputMapPath, 260.0f );

        ImGui::SameLine();
        if ( ImGui::Button( "Reload" ) )
            reloadFromFile();

        ImGui::SameLine();
        if ( ImGui::Button( "Save XML" ) )
            (void)saveToFile(); // 실패는 saveToFile 이 알리고 dirty 가 남는다

        ImGui::SameLine();
        if ( ImGui::Button( "Revert All to Default" ) )
        {
            _inputMap.resetAllBindingsToDefault();
            markDocumentDirty();
        }

        if ( isDocumentDirty() )
        {
            ImGui::SameLine();
            EditorThemeUtil::textWarning( "* Unsaved changes" );
        }

        ImGui::Separator();

        drawLayerList();
        ImGui::Separator();
        drawActionTable();
        ImGui::Separator();
        drawAddActionSection();
        drawCaptureModal();
    }

    void InputMapPanel::drawLayerList()
    {
        const vector<hashed_string>& listLayer = _inputMap.getLayerNames();

        if ( ImGui::CollapsingHeader( "Input Layers", ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            static constexpr InputMapPanelInternal::TableColumn kArrLayerColumn[] = {
                {  "Layer Name",   0.0f},
                {      "Active",  60.0f},
                {    "Priority",  60.0f},
                {"Stack Status", 110.0f}
            };
            if ( InputMapPanelInternal::beginColumnTable( "LayerTable", kArrLayerColumn, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg ) )
            {
                for ( const hashed_string& layerName : listLayer )
                {
                    InputMapPanelInternal::beginNamedRow( layerName );
                    ImGui::PushID( layerName.c_str() );
                    bool bEnabled = _inputMap.isLayerEnabled( layerName );
                    if ( ImGui::Checkbox( "##Enabled", &bEnabled ) )
                    {
                        _inputMap.setLayerEnabled( sw::hashed_string( layerName.view() ), bEnabled );
                        markDocumentDirty();
                    }
                    ImGui::PopID();

                    ImGui::TableNextColumn();
                    ImGui::Text( "%d", _inputMap.getLayerPriority( layerName ) );

                    ImGui::TableNextColumn();
                    if ( _inputMap.getCurrentTopLayer() == layerName.view() )
                        EditorThemeUtil::textSuccess( "Top (Active)" );
                    else if ( bEnabled )
                        ImGui::Text( "Active" );
                    else
                        ImGui::TextDisabled( "Disabled" );
                }
                ImGui::EndTable();
            }
        }
    }

    void InputMapPanel::drawActionTable()
    {
        const vector<hashed_string>& listAction = _inputMap.getActionNames();

        static constexpr InputMapPanelInternal::TableColumn kArrActionColumn[] = {
            {       "Action",   0.0f},
            {      "Trigger", 110.0f},
            {     "UI Glyph",  90.0f},
            {"State / Phase", 100.0f},
            {    "Hold Time",  80.0f},
            {       "Rebind",  75.0f},
            {        "Reset",  60.0f}
        };
        if ( InputMapPanelInternal::beginColumnTable( "ActionTable", kArrActionColumn,
                                                      ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable ) )
        {
            for ( const hashed_string& actionName : listAction )
            {
                InputMapPanelInternal::beginNamedRow( actionName );
                const ActionTrigger trigger      = _inputMap.getBindingTrigger( actionName, 0 );
                const utf8*         pTriggerName = InputMap::actionTriggerToName( trigger );
                ImGui::TextUnformatted( pTriggerName != nullptr ? pTriggerName : "Unknown" );

                ImGui::TableNextColumn();
                const string glyph = _inputMap.getGlyphForAction( sw::hashed_string( actionName.view() ) );
                EditorThemeUtil::textInfo( glyph.c_str() );

                ImGui::TableNextColumn();
                const bool        bDown      = _inputMap.isActionDown( actionName );
                const bool        bTriggered = _inputMap.wasActionTriggered( actionName );
                const ActionPhase phase      = _inputMap.getActionPhase( actionName );
                if ( bTriggered )
                    EditorThemeUtil::textError( "TRIGGERED" );
                else if ( bDown )
                    EditorThemeUtil::textSuccess( "DOWN" );
                else if ( phase != ActionPhase::None )
                    EditorThemeUtil::textWarning( "ONGOING" );
                else
                    ImGui::TextDisabled( "Idle" );

                ImGui::TableNextColumn();
                const float32 holdSec = _inputMap.getActionHoldDuration( actionName );
                if ( holdSec > 0.0f )
                {
                    EditorThemeUtil::pushTextColor( EditorThemeUtil::getWarningColor() );
                    ImGui::Text( "%.2f s", static_cast<float64>( holdSec ) );
                    EditorThemeUtil::popTextColor();
                }
                else
                    ImGui::Text( "0.00 s" );

                ImGui::TableNextColumn();
                ImGui::PushID( actionName.c_str() );
                if ( ImGui::Button( "Rebind" ) )
                {
                    _selectedAction     = actionName.c_str();
                    _capturingBindIndex = 0;
                    _bCapturingKey      = SW_TRUE;
                }
                ImGui::PopID();

                ImGui::TableNextColumn();
                ImGui::PushID( ( string( actionName.c_str() ) + "_reset" ).c_str() );
                if ( ImGui::Button( "Reset" ) )
                {
                    _inputMap.resetActionToDefault( sw::hashed_string( actionName.view() ) );
                    markDocumentDirty();
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }

    void InputMapPanel::drawAddActionSection()
    {
        if ( ImGui::CollapsingHeader( "Add New Action / Layer" ) )
        {
            EditorWidgets::drawTextField( "New Action Name", _newActionName, 200.0f );

            ImGui::SameLine();
            const utf8* arrTypes[] = { "Boolean", "Axis 1D", "Vector 2D" };
            ImGui::SetNextItemWidth( 120.0f * EditorThemeUtil::getDpiScale() );
            ImGui::Combo( "Type", &_newActionValueType, arrTypes, 3 );

            ImGui::SameLine();
            if ( ImGui::Button( "Create Action" ) && _newActionName.empty() == false )
            {
                static constexpr InputActionValueType kArrValueType[] = { InputActionValueType::Boolean, InputActionValueType::Axis1D, InputActionValueType::Axis2D };
                const InputActionValueType            valueType       = kArrValueType[MathUtil::clamp( _newActionValueType, 0, 2 )];
                _inputMap.createAction( sw::hashed_string( _newActionName.c_str() ), valueType );
                _newActionName = "";
                markDocumentDirty();
            }
        }
    }

    void InputMapPanel::drawCaptureModal()
    {
        if ( _bCapturingKey == SW_FALSE )
            return;

        ImGui::OpenPopup( "Press Key / Button To Bind" );
        if ( ImGui::BeginPopupModal( "Press Key / Button To Bind", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
        {
            ImGui::Text( "Binding for Action: %s", _selectedAction.c_str() );
            ImGui::Text( "Press any keyboard key, or click a button below to bind..." );
            ImGui::Separator();

            // 실시간 활성 입력 감지 — 모달이 떠 있는 동안 키는 ImGui 로만 온다(`findPressedKey` 설명).
            const Key pressedKey = InputMapPanelInternal::findPressedKey();
            if ( pressedKey != Key::Unknown )
            {
                rebindSelectedAction( pressedKey );
                _bCapturingKey = SW_FALSE;
                ImGui::CloseCurrentPopup();
            }

            // 버튼 리스트 폴백
            ImGui::BeginChild( "KeyGrid", ImVec2( 450.0f * EditorThemeUtil::getDpiScale(), 200.0f * EditorThemeUtil::getDpiScale() ), true );
            for ( int32 keyIndex = 1; keyIndex < static_cast<int32>( Key::Count ); ++keyIndex )
            {
                const Key   key      = static_cast<Key>( keyIndex );
                const utf8* pKeyName = KeyCodeUtil::toName( key );
                if ( StringUtil::isNullOrEmpty( pKeyName ) == false )
                {
                    if ( ImGui::Button( pKeyName, ImVec2( 80.0f * EditorThemeUtil::getDpiScale(), 24.0f * EditorThemeUtil::getDpiScale() ) ) )

                    {
                        rebindSelectedAction( key );
                        _bCapturingKey = SW_FALSE;
                        ImGui::CloseCurrentPopup();
                        break;
                    }
                    if ( ( keyIndex % 5 ) != 0 )
                        ImGui::SameLine();
                }
            }
            ImGui::EndChild();

            if ( ImGui::Button( "Cancel", ImVec2( 120.0f * EditorThemeUtil::getDpiScale(), 0 ) ) )
            {
                _bCapturingKey = SW_FALSE;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    void InputMapPanel::rebindSelectedAction( sw::Key newKey )
    {
        // 어느 레이어에서 충돌을 따져야 하는지는 그 바인딩 자신이 안다.
        const sw::ActionBinding* pBinding = _inputMap.getBinding( sw::hashed_string( _selectedAction.c_str() ), _capturingBindIndex );
        const string_view        layer    = ( pBinding != nullptr ) ? pBinding->_layer.view() : string_view{};

        sw::string conflictingAction;
        if ( _inputMap.hasBindingConflict( sw::InputSlot::fromKey( newKey ), sw::hashed_string( layer ), conflictingAction ) && conflictingAction != _selectedAction )
        {
            SW_LOG_WARNING( "'%#' 을(를) %# 에 바인딩합니다 — 같은 레이어의 '%#' 과(와) 겹칩니다.",
                            _selectedAction.c_str(), sw::KeyCodeUtil::toName( newKey ), conflictingAction.c_str() );
        }

        // 키 하나로 바꿀 수 없는 바인딩(합성 축 · 스틱)이면 바뀌지 않는다 — 경고는 InputMap 이 남긴다. 그때 문서를 더럽히지 않는다.
        if ( _inputMap.rebindKey( sw::hashed_string( _selectedAction.c_str() ), newKey, _capturingBindIndex ) )
            markDocumentDirty();
    }

    void InputMapPanel::drawDeviceMonitorTab()
    {
        InputManager* pInput = getService<InputManager>();
        if ( pInput == nullptr )
        {
            EditorWidgets::drawEmptyHint( "InputManager service is not available." );
            return;
        }

        // 1) 활성 장치 상태
        const InputGlyphStyle devType   = pInput->getActiveGlyphStyle();
        const utf8*           pTypeName = InputMapPanelInternal::glyphStyleName( devType );

        ImGui::Text( "Active Device:" );

        ImGui::SameLine();
        EditorThemeUtil::textSuccess( pTypeName );
        ImGui::Separator();

        drawKeyboardMonitor();

        drawMouseMonitor();

        drawGamepadMonitor();
    }

    void InputMapPanel::drawKeyboardMonitor()
    {
        InputManager* pInput = getService<InputManager>();
        if ( pInput == nullptr )
            return;

        // 2) 키보드 실시간 모니터
        if ( ImGui::CollapsingHeader( "Keyboard Status", ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            ImGui::Text( "Held Keys:" );
            ImGui::SameLine();
            bool bAnyKey = false;
            for ( int32 keyIndex = 1; keyIndex < static_cast<int32>( Key::Count ); ++keyIndex )
            {
                const Key key = static_cast<Key>( keyIndex );
                if ( pInput->isKeyDown( key ) )
                {
                    const utf8* pName = KeyCodeUtil::toName( key );
                    if ( pName != nullptr )
                    {
                        ImGui::SameLine();
                        EditorThemeUtil::pushTextColor( EditorThemeUtil::getWarningColor() );
                        ImGui::Text( "[%s]", pName );
                        EditorThemeUtil::popTextColor();
                        bAnyKey = true;
                    }
                }
            }
            if ( bAnyKey == false )
                ImGui::TextDisabled( "None" );
        }
    }

    void InputMapPanel::drawMouseMonitor()
    {
        InputManager* pInput = getService<InputManager>();
        if ( pInput == nullptr )
            return;

        // 3) 마우스 실시간 모니터
        if ( ImGui::CollapsingHeader( "Mouse Status", ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            int32      mx = 0, my = 0;
            const int2 vecMousePos2 = pInput->getMousePosition();
            mx                      = vecMousePos2._x;
            my                      = vecMousePos2._y;
            int32      dx = 0, dy = 0;
            const int2 vecMouseDelta3 = pInput->getMouseDelta();
            dx                        = vecMouseDelta3._x;
            dy                        = vecMouseDelta3._y;
            float32      smoothDx = 0.0f, smoothDy = 0.0f;
            const float2 vecSmoothDelta2 = pInput->getMouse() != nullptr ? pInput->getMouse()->getSmoothDelta() : float2{};
            smoothDx                     = vecSmoothDelta2._x;
            smoothDy                     = vecSmoothDelta2._y;

            ImGui::Text( "Position: (%d, %d)", mx, my );
            ImGui::SameLine( 200.0f );
            ImGui::Text( "Delta: (%d, %d)", dx, dy );
            ImGui::SameLine( 350.0f );
            ImGui::Text( "Smooth Delta: (%.2f, %.2f)", static_cast<float64>( smoothDx ), static_cast<float64>( smoothDy ) );

            ImGui::Text( "Wheel: %.2f", static_cast<float64>( pInput->getMouseWheel() ) );
            ImGui::SameLine( 200.0f );
            ImGui::Text( "Buttons:" );
            ImGui::SameLine();
            ImGui::TextColored( pInput->isMouseButtonDown( MouseButton::Left ) ? ImVec4( 0.2f, 1.0f, 0.2f, 1.0f ) : ImVec4( 0.4f, 0.4f, 0.4f, 1.0f ), "[L]" );
            ImGui::SameLine();
            ImGui::TextColored( pInput->isMouseButtonDown( MouseButton::Right ) ? ImVec4( 0.2f, 1.0f, 0.2f, 1.0f ) : ImVec4( 0.4f, 0.4f, 0.4f, 1.0f ), "[R]" );
            ImGui::SameLine();
            ImGui::TextColored( pInput->isMouseButtonDown( MouseButton::Middle ) ? ImVec4( 0.2f, 1.0f, 0.2f, 1.0f ) : ImVec4( 0.4f, 0.4f, 0.4f, 1.0f ), "[M]" );
        }
    }

    void InputMapPanel::drawGamepadMonitor()
    {
        InputManager* pInput = getService<InputManager>();
        if ( pInput == nullptr )
            return;

        // 4) 게임패드 실시간 모니터
        if ( ImGui::CollapsingHeader( "Gamepad 0 Status", ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            GamepadDevice* pGamepad = pInput->getGamepad( 0 );
            if ( pGamepad != nullptr && pGamepad->isConnected() )
            {
                const GamepadBatteryInfo batteryInfo = pGamepad->getBatteryInfo();
                ImGui::Text( "Battery: %s", InputMapPanelInternal::batteryLevelName( batteryInfo._level ) );

                float32      lx = 0.0f, ly = 0.0f, rx = 0.0f, ry = 0.0f;
                const float2 vecLeftStick3  = pGamepad->getLeftStick();
                lx                          = vecLeftStick3._x;
                ly                          = vecLeftStick3._y;
                const float2 vecRightStick4 = pGamepad->getRightStick();
                rx                          = vecRightStick4._x;
                ry                          = vecRightStick4._y;

                drawGamepadStickVisualizer( "Left Stick", lx, ly, 0.15f );
                ImGui::SameLine( 180.0f );
                drawGamepadStickVisualizer( "Right Stick", rx, ry, 0.15f );

                ImGui::SameLine( 360.0f );
                ImGui::BeginGroup();
                ImGui::Text( "Left Trigger:  %.2f", static_cast<float64>( pGamepad->getLeftTrigger() ) );
                ImGui::ProgressBar( pGamepad->getLeftTrigger(), ImVec2( 150.0f * EditorThemeUtil::getDpiScale(), 14.0f * EditorThemeUtil::getDpiScale() ) );
                ImGui::Text( "Right Trigger: %.2f", static_cast<float64>( pGamepad->getRightTrigger() ) );
                ImGui::ProgressBar( pGamepad->getRightTrigger(), ImVec2( 150.0f * EditorThemeUtil::getDpiScale(), 14.0f * EditorThemeUtil::getDpiScale() ) );
                ImGui::EndGroup();

                ImGui::Separator();
                ImGui::Text( "Haptic Vibration Test:" );
                ImGui::SetNextItemWidth( 120.0f * EditorThemeUtil::getDpiScale() );
                ImGui::SliderFloat( "Left Motor", &_testVibLeft, 0.0f, 1.0f );
                ImGui::SameLine();
                ImGui::SetNextItemWidth( 120.0f * EditorThemeUtil::getDpiScale() );
                ImGui::SliderFloat( "Right Motor", &_testVibRight, 0.0f, 1.0f );
                ImGui::SameLine();
                if ( ImGui::Button( "Test Pulse (0.3s)" ) )
                    pGamepad->playVibration( _testVibLeft, _testVibRight, 0.3f );
                ImGui::SameLine();
                if ( ImGui::Button( "Stop" ) )
                    pGamepad->stopVibration();
            }
            else
            {
                EditorWidgets::drawEmptyHint( "No Gamepad Connected on Port 0." );
            }
        }
    }

    void InputMapPanel::drawGamepadStickVisualizer( const utf8* pLabel, float32 stickX, float32 stickY, float32 deadzone )
    {
        ImGui::BeginGroup();
        ImGui::Text( "%s", pLabel );
        const ImVec2  pos    = ImGui::GetCursorScreenPos();
        const float32 radius = 50.0f * EditorThemeUtil::getDpiScale();
        const ImVec2  center = ImVec2( pos.x + radius, pos.y + radius );

        ImDrawList* pDraw = ImGui::GetWindowDrawList();
        pDraw->AddCircleFilled( center, radius, IM_COL32( 30, 30, 30, 255 ) );
        pDraw->AddCircle( center, radius, IM_COL32( 100, 100, 100, 255 ) );
        pDraw->AddCircle( center, radius * deadzone, IM_COL32( 80, 40, 40, 255 ) );

        const ImVec2 dotPos = ImVec2( center.x + stickX * radius, center.y - stickY * radius );
        pDraw->AddCircleFilled( dotPos, 6.0f * EditorThemeUtil::getDpiScale(), IM_COL32( 50, 200, 50, 255 ) );

        ImGui::Dummy( ImVec2( radius * 2.0f, radius * 2.0f ) );
        ImGui::Text( "X: %+.2f  Y: %+.2f", static_cast<float64>( stickX ), static_cast<float64>( stickY ) );
        ImGui::EndGroup();
    }

    void InputMapPanel::drawConflictMatrixTab()
    {
        ImGui::Text( "Key Binding Conflict Matrix & One-Click Resolver" );
        ImGui::TextDisabled( "Detects duplicated key bindings across actions and provides instant collision resolution." );
        ImGui::Separator();

        const vector<hashed_string>& listAction     = _inputMap.getActionNames();
        bool                         bFoundConflict = false;

        static constexpr InputMapPanelInternal::TableColumn kArrConflictColumn[] = {
            {     "Action A",   0.0f},
            {     "Action B",   0.0f},
            {"Colliding Key", 100.0f},
            {         "Swap",  75.0f},
            {   "Override B",  85.0f}
        };
        if ( InputMapPanelInternal::beginColumnTable( "ConflictTable", kArrConflictColumn, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg ) )
        {

            for ( size_t idxA = 0; idxA < listAction.size(); ++idxA )
            {
                const hashed_string& nameA  = listAction[idxA];
                const string         glyphA = _inputMap.getGlyphForAction( sw::hashed_string( nameA.view() ) );
                if ( glyphA == "[ Unbound ]" || glyphA.empty() )
                    continue;

                for ( size_t idxB = idxA + 1; idxB < listAction.size(); ++idxB )
                {
                    const hashed_string& nameB  = listAction[idxB];
                    const string         glyphB = _inputMap.getGlyphForAction( sw::hashed_string( nameB.view() ) );

                    if ( glyphA == glyphB )
                    {
                        bFoundConflict = true;
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        EditorThemeUtil::textError( nameA.c_str() );

                        ImGui::TableNextColumn();
                        EditorThemeUtil::textError( nameB.c_str() );

                        ImGui::TableNextColumn();
                        EditorThemeUtil::textWarning( glyphA.c_str() );

                        ImGui::TableNextColumn();
                        ImGui::PushID( static_cast<int32>( idxA * 1000 + idxB ) );
                        if ( ImGui::Button( "Rebind A" ) )
                        {
                            _selectedAction     = nameA.c_str();
                            _capturingBindIndex = 0;
                            _bCapturingKey      = SW_TRUE;
                        }
                        ImGui::PopID();

                        ImGui::TableNextColumn();
                        ImGui::PushID( static_cast<int32>( idxA * 1000 + idxB + 500 ) );
                        if ( ImGui::Button( "Unbind B" ) )
                        {
                            _inputMap.rebindKey( sw::hashed_string( nameB.c_str() ), Key::Unknown, 0 );
                            markDocumentDirty();
                        }
                        ImGui::PopID();
                    }
                }
            }
            ImGui::EndTable();
        }

        if ( bFoundConflict == false )
        {
            ImGui::Spacing();
            EditorThemeUtil::textSuccess( "✓ Zero Conflicts Detected! All key bindings are completely unique." );
        }
    }

    void InputMapPanel::drawInputGraphTab()
    {
        ImGui::Text( "Real-Time Input Graphs (Last 120 Frames)" );
        ImGui::SameLine( 450.0f );
        bool bPaused = ( _bPlotPaused == SW_TRUE );
        if ( ImGui::Checkbox( "Pause Graph", &bPaused ) )
            _bPlotPaused = bPaused ? SW_TRUE : SW_FALSE;

        ImGui::Separator();

        ImGui::Text( "Gamepad Left Stick (X/Y):" );
        ImGui::PlotLines( "Stick X", _arrPlotLeftStickX, static_cast<int32>( kPlotSampleCount ), static_cast<int32>( _plotOffset ), "X [-1.0 ~ +1.0]", -1.0f, 1.0f, ImVec2( 0, 70.0f * EditorThemeUtil::getDpiScale() ) );
        ImGui::PlotLines( "Stick Y", _arrPlotLeftStickY, static_cast<int32>( kPlotSampleCount ), static_cast<int32>( _plotOffset ), "Y [-1.0 ~ +1.0]", -1.0f, 1.0f, ImVec2( 0, 70.0f * EditorThemeUtil::getDpiScale() ) );

        ImGui::Separator();
        ImGui::Text( "Analog Triggers (LT/RT):" );
        ImGui::PlotLines( "LT", _arrPlotTriggerL, static_cast<int32>( kPlotSampleCount ), static_cast<int32>( _plotOffset ), "Left [0.0 ~ 1.0]", 0.0f, 1.0f, ImVec2( 0, 60.0f * EditorThemeUtil::getDpiScale() ) );
        ImGui::PlotLines( "RT", _arrPlotTriggerR, static_cast<int32>( kPlotSampleCount ), static_cast<int32>( _plotOffset ), "Right [0.0 ~ 1.0]", 0.0f, 1.0f, ImVec2( 0, 60.0f * EditorThemeUtil::getDpiScale() ) );

        ImGui::Separator();
        ImGui::Text( "Mouse Delta Speed (dX/dY):" );
        ImGui::PlotLines( "dX", _arrPlotMouseDeltaX, static_cast<int32>( kPlotSampleCount ), static_cast<int32>( _plotOffset ), "Delta X", -100.0f, 100.0f, ImVec2( 0, 60.0f * EditorThemeUtil::getDpiScale() ) );
        ImGui::PlotLines( "dY", _arrPlotMouseDeltaY, static_cast<int32>( kPlotSampleCount ), static_cast<int32>( _plotOffset ), "Delta Y", -100.0f, 100.0f, ImVec2( 0, 60.0f * EditorThemeUtil::getDpiScale() ) );
    }

    void InputMapPanel::drawInputSimulatorTab()
    {
        InputManager* pInput = getService<InputManager>();
        if ( pInput == nullptr )
        {
            EditorWidgets::drawEmptyHint( "InputManager service is not available." );
            return;
        }

        ImGui::Text( "Virtual Input Injector & Gameplay Macro Simulator" );
        ImGui::TextDisabled( "Inject virtual key, mouse, or stick events directly into the engine without physical hardware." );
        ImGui::Separator();

        ImGui::Text( "1) Key Event Injector:" );
        const utf8* arrCommonKeys[] = { "Space", "Enter", "Escape", "W", "A", "S", "D", "E", "F", "Shift", "Control" };
        const Key   arrKeyValues[]  = { Key::Space, Key::Enter, Key::Escape, Key::W, Key::A, Key::S, Key::D, Key::E, Key::F, Key::LeftShift, Key::LeftControl };

        ImGui::SetNextItemWidth( 150.0f * EditorThemeUtil::getDpiScale() );
        ImGui::Combo( "Key", &_simKeyToInject, arrCommonKeys, 11 );
        ImGui::SameLine();
        if ( ImGui::Button( "Inject KeyDown" ) )
            pInput->postRawEvent( RawInputEvent::makeKeyDown( arrKeyValues[_simKeyToInject] ) );
        ImGui::SameLine();
        if ( ImGui::Button( "Inject KeyUp" ) )
            pInput->postRawEvent( RawInputEvent::makeKeyUp( arrKeyValues[_simKeyToInject] ) );
        ImGui::SameLine();
        if ( ImGui::Button( "Tap Key (Down + Up)" ) )
        {
            pInput->postRawEvent( RawInputEvent::makeKeyDown( arrKeyValues[_simKeyToInject] ) );
            pInput->postRawEvent( RawInputEvent::makeKeyUp( arrKeyValues[_simKeyToInject] ) );
        }

        ImGui::Separator();
        ImGui::Text( "2) Virtual Stick 2D Slider:" );
        ImGui::SliderFloat( "Sim Stick X", &_simStick._x, -1.0f, 1.0f );
        ImGui::SliderFloat( "Sim Stick Y", &_simStick._y, -1.0f, 1.0f );
        if ( ImGui::Button( "Inject Stick Tilt" ) )
        {
            GamepadDevice* pGamepad = pInput->getGamepad( 0 );
            if ( pGamepad != nullptr )
            {
                pGamepad->setAxis( 0, _simStick._x );
                pGamepad->setAxis( 1, _simStick._y );
            }
        }
        ImGui::SameLine();
        if ( ImGui::Button( "Reset Stick to Center" ) )
        {
            _simStick._x            = 0.0f;
            _simStick._y            = 0.0f;
            GamepadDevice* pGamepad = pInput->getGamepad( 0 );
            if ( pGamepad != nullptr )
            {
                pGamepad->setAxis( 0, 0.0f );
                pGamepad->setAxis( 1, 0.0f );
            }
        }

        ImGui::Separator();
        ImGui::Text( "3) One-Click Combat Macros:" );
        if ( ImGui::Button( "Inject 'Hadoken' (236 + Attack)" ) )
        {
            pInput->postRawEvent( RawInputEvent::makeKeyDown( Key::S ) );
            pInput->postRawEvent( RawInputEvent::makeKeyUp( Key::S ) );
            pInput->postRawEvent( RawInputEvent::makeKeyDown( Key::C ) );
            pInput->postRawEvent( RawInputEvent::makeKeyUp( Key::C ) );
            pInput->postRawEvent( RawInputEvent::makeKeyDown( Key::D ) );
            pInput->postRawEvent( RawInputEvent::makeKeyUp( Key::D ) );
            pInput->postRawEvent( RawInputEvent::makeKeyDown( Key::J ) );
            pInput->postRawEvent( RawInputEvent::makeKeyUp( Key::J ) );
            SW_LOG_INFO( "Injected Hadoken combo macro into InputManager!" );
        }
    }

    void InputMapPanel::drawInputReplayTab()
    {
        InputManager* pInput = getService<InputManager>();

        ImGui::Text( "Input Replay Recorder & Deterministic QA Playback" );
        ImGui::TextDisabled( "Playback attaches the replay as exclusive virtual input: recorded frame n is applied on the n-th input frame." );
        ImGui::Separator();

        EditorWidgets::drawTextField( "Replay File", _replayFilePath, 260.0f );

        ImGui::SameLine();
        if ( ImGui::Button( "Save Replay" ) )
        {
            if ( _replay.saveToFile( _replayFilePath.c_str() ) == false )
                SW_LOG_ERROR( "Could not save input replay '%#'", _replayFilePath.c_str() );
        }

        ImGui::SameLine();
        if ( ImGui::Button( "Load Replay" ) )
        {
            if ( pInput != nullptr && pInput->getVirtualInput() == &_replay )
                pInput->detachVirtualInput();
            if ( _replay.loadFromFile( _replayFilePath.c_str() ) == false )
                SW_LOG_ERROR( "Could not load input replay '%#'", _replayFilePath.c_str() );
        }

        if ( pInput == nullptr )
        {
            EditorWidgets::drawEmptyHint( "InputManager service is not available." );
            return;
        }

        ImGui::Separator();

        // 녹화 제어
        const bool bPlaying = pInput->getVirtualInput() == &_replay;
        if ( _replay.isRecording() )
        {
            EditorThemeUtil::pushTextColor( EditorThemeUtil::getErrorColor() );
            ImGui::Text( "● RECORDING LIVE INPUTS... (Frames: %u)", _replay.getFrameCount() );
            EditorThemeUtil::popTextColor();
            ImGui::SameLine();
            if ( ImGui::Button( "■ Stop Recording" ) )
                _replay.stopRecording();
        }
        else if ( bPlaying == false )
        {
            if ( ImGui::Button( "● Start Recording" ) )
            {
                _replay.startRecording( "GameplaySession" );
                _recordedBeginFrameCount = pInput->getBeginFrameCount();
            }
        }

        ImGui::Separator();

        // 재생 제어 — 재생은 붙이기, 일시정지는 그 프레임으로 seekTo(상태 재현 뒤 뗌), 탐색도 seekTo.
        ImGui::Text( "Playback Controls (Total Frames: %u, Duration: %.2f s):", _replay.getFrameCount(), static_cast<float64>( _replay.getTotalDuration() ) );
        const uint32 currentFrame = _replay.getStartFrameIndex() + ( bPlaying ? pInput->getVirtualFrameIndex() : 0u );
        if ( bPlaying )
        {
            if ( ImGui::Button( "⏸ Pause" ) )
                _replay.seekTo( *pInput, currentFrame );
            ImGui::SameLine();
            if ( ImGui::Button( "⏹ Stop" ) )
                _replay.seekTo( *pInput, 0 );
        }
        else if ( _replay.isRecording() == false && _replay.getFrameCount() > 0 )
        {
            if ( ImGui::Button( "▶ Play Replay" ) )
            {
                if ( currentFrame >= _replay.getFrameCount() )
                    _replay.seekTo( *pInput, 0 );
                pInput->attachVirtualInput( &_replay, VirtualInputMode::Exclusive, false );
            }
            ImGui::SameLine();
            if ( ImGui::Button( "⏮ Step Back" ) && currentFrame > 0 )
                _replay.seekTo( *pInput, currentFrame - 1 );
            ImGui::SameLine();
            if ( ImGui::Button( "⏭ Step Forward (1 Frame)" ) )
                _replay.seekTo( *pInput, currentFrame + 1 );

            int32       frameIdx  = static_cast<int32>( currentFrame );
            const int32 maxFrames = static_cast<int32>( _replay.getFrameCount() );
            if ( ImGui::SliderInt( "Timeline Frame", &frameIdx, 0, maxFrames ) )
                _replay.seekTo( *pInput, static_cast<uint32>( frameIdx ) );
        }

        if ( currentFrame < _replay.getFrameCount() )
        {
            const InputReplayFrame&               frame = _replay.getFrames()[currentFrame];
            fixed_string<constant::kMaxBuffer128> frameBuf;
            formatstring( frameBuf.data(), frameBuf.capacity(), "Frame #%u | DeltaTime: %#s | Events: %d", currentFrame,
                          Fmt( static_cast<float64>( frame._deltaTime ), Format().precision( 4 ) ), static_cast<int32>( frame._listRawEvent.size() ) );
            EditorThemeUtil::textInfo( frameBuf.c_str() );
        }
    }

    void InputMapPanel::drawGlyphPreviewerTab()
    {
        ImGui::Text( "Multi-Platform Action UI Glyph & Button Prompt Previewer" );
        ImGui::TextDisabled( "Preview how button prompts appear across Xbox, PlayStation, Nintendo Switch, and PC Keyboards." );
        ImGui::Separator();

        const utf8*                      arrPlatforms[]      = { "Xbox Controller", "PlayStation DualSense", "Nintendo Switch Pro", "PC Keyboard / Mouse" };
        static constexpr InputGlyphStyle kArrPreviewDevice[] = { InputGlyphStyle::GamepadXbox, InputGlyphStyle::GamepadPlayStation, InputGlyphStyle::GamepadSwitch, InputGlyphStyle::KeyboardMouse };
        ImGui::Combo( "Target Platform", &_selectedGlyphPlatform, arrPlatforms, 4 );
        // 미리보기 장치와 아래 표의 플랫폼 이름이 같은 자리를 읽는다 — 범위 제한을 한 번만 한다.
        const int32           platformIndex = MathUtil::clamp( _selectedGlyphPlatform, 0, 3 );
        const InputGlyphStyle previewDevice = kArrPreviewDevice[platformIndex];
        ImGui::Separator();

        const vector<hashed_string>&                        listAction        = _inputMap.getActionNames();
        static constexpr InputMapPanelInternal::TableColumn kArrGlyphColumn[] = {
            {      "Action Name",   0.0f},
            {      "Key Binding", 120.0f},
            {"UI Prompt (Glyph)", 140.0f},
            {   "Platform Style", 140.0f}
        };
        if ( InputMapPanelInternal::beginColumnTable( "GlyphTable", kArrGlyphColumn, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg ) )
        {
            for ( const hashed_string& actionName : listAction )
            {
                InputMapPanelInternal::beginNamedRow( actionName );
                const string glyph = _inputMap.getGlyphForAction( sw::hashed_string( actionName.view() ) );
                ImGui::TextUnformatted( glyph.c_str() );

                ImGui::TableNextColumn();
                const string previewGlyph = _inputMap.getGlyphForAction( sw::hashed_string( actionName.view() ), previewDevice );
                if ( platformIndex == 0 )
                    ImGui::TextColored( ImVec4( 0.2f, 1.0f, 0.4f, 1.0f ), "[ Ⓨ Xbox ] %s", previewGlyph.c_str() );
                else if ( platformIndex == 1 )
                    ImGui::TextColored( ImVec4( 0.3f, 0.6f, 1.0f, 1.0f ), "[ ▲ DualSense ] %s", previewGlyph.c_str() );
                else if ( platformIndex == 2 )
                    ImGui::TextColored( ImVec4( 1.0f, 0.3f, 0.3f, 1.0f ), "[ X Switch ] %s", previewGlyph.c_str() );
                else
                    ImGui::TextColored( ImVec4( 0.9f, 0.9f, 0.9f, 1.0f ), "[ KeyCap ] %s", previewGlyph.c_str() );

                ImGui::TableNextColumn();
                ImGui::TextDisabled( "%s", arrPlatforms[platformIndex] );
            }
            ImGui::EndTable();
        }
    }

    void InputMapPanel::drawViewportOverlayTab()
    {
        ImGui::Text( "Game Viewport On-Screen Controller Overlay HUD Settings" );
        ImGui::Separator();

        ViewportInputOverlayConfig& config = ViewportInputOverlay::getConfig();

        bool bEnabled = ( config._bEnabled == SW_TRUE );
        if ( ImGui::Checkbox( "Enable Viewport On-Screen HUD Overlay", &bEnabled ) )
            config._bEnabled = bEnabled ? SW_TRUE : SW_FALSE;

        ImGui::Separator();

        const utf8* arrPos[] = { "Bottom-Right", "Bottom-Left", "Top-Right", "Top-Left" };
        int32       posIndex = static_cast<int32>( config._position );
        if ( ImGui::Combo( "Screen Anchor Position", &posIndex, arrPos, 4 ) )
            config._position = static_cast<ViewportOverlayPosition>( posIndex );

        ImGui::SliderFloat( "HUD Opacity", &config._opacity, 0.1f, 1.0f );
        ImGui::SliderFloat( "HUD Scale", &config._scale, 0.5f, 2.0f );

        ImGui::Separator();
        ImGui::Text( "Visible HUD Elements:" );

        bool bStick = ( config._bShowStick == SW_TRUE );
        if ( ImGui::Checkbox( "Show 2D Stick & Trigger Gauges", &bStick ) )
            config._bShowStick = bStick ? SW_TRUE : SW_FALSE;

        bool bButtons = ( config._bShowButtons == SW_TRUE );
        if ( ImGui::Checkbox( "Show Face Buttons (A/B/X/Y)", &bButtons ) )
            config._bShowButtons = bButtons ? SW_TRUE : SW_FALSE;

        bool bShowHistory = ( config._bShowCommandHistory == SW_TRUE );
        if ( ImGui::Checkbox( "Show Live Action Trigger Stream", &bShowHistory ) )
            config._bShowCommandHistory = bShowHistory ? SW_TRUE : SW_FALSE;
    }

    void InputMapPanel::drawCombosAndBufferTab()
    {
        ImGui::Text( "Fighting Game Combo Tester & Input Buffer Inspector" );
        ImGui::Separator();

        EditorWidgets::drawTextField( "Combo Pattern (Numpad Notation)", _testComboPattern, 150.0f );

        ImGui::SameLine();
        const bool bPatternMatched = _inputMap.wasCommandPatternTriggered( sw::hashed_string( _testComboPattern.c_str() ), 0.8f );
        if ( bPatternMatched )
            EditorThemeUtil::textSuccess( "MATCHED! (Success)" );
        else
            ImGui::TextDisabled( "Waiting for input..." );

        ImGui::Text( "Legend: 236P = Hadoken (Down, DownRight, Right + Punch), 623P = Shoryuken" );
        ImGui::Separator();

        ImGui::Text( "Action Input Buffering:" );
        if ( ImGui::Button( "Buffer 'Attack' (0.3s)" ) )
            _inputMap.bufferAction( "Attack", 0.3f );
        ImGui::SameLine();
        if ( ImGui::Button( "Buffer 'Jump' (0.3s)" ) )
            _inputMap.bufferAction( "Jump", 0.3f );

        ImGui::SameLine();
        if ( ImGui::Button( "Consume 'Attack'" ) )
        {
            if ( _inputMap.consumeBufferedAction( "Attack" ) )
                SW_LOG_INFO( "Successfully consumed buffered 'Attack'!" );
        }
    }

    void InputMapPanel::reloadFromFile()
    {
        // 못 읽으면 편집 중인 바인딩과 dirty 를 그대로 둔다(dirty 를 지우고 "다시 읽었다" 고 하지 않는다).
        if ( _inputMap.loadFromResource( _inputMapPath.c_str() ) == false )
        {
            SW_LOG_ERROR( "Could not reload InputMap from %# - keeping the edited bindings", _inputMapPath.c_str() );
            return;
        }
        clearDocumentDirty();
        SW_LOG_INFO( "Reloaded InputMap from %#", _inputMapPath.c_str() );
    }

    bool InputMapPanel::saveToFile()
    {
        // 패널은 리소스의 기본 바인딩(`<InputMap>`)을 편집한다 — 다시 읽기(`reloadFromFile`)와 같은 형식 · 같은 자리에 쓴다.
        // 플레이어 리매핑은 UserSettings 의 `keyBinding` 설정이 사용자 파일에 든다.
        if ( _inputMap.saveToResource( _inputMapPath.c_str() ) == false )
        {
            SW_LOG_WARNING( "Failed to save InputMap to %#", _inputMapPath.c_str() );
            return false;
        }
        clearDocumentDirty();
        SW_LOG_INFO( "Saved InputMap to %#", _inputMapPath.c_str() );
        return true;
    }

    bool InputMapPanel::saveDocument()
    {
        return saveToFile();
    }

    void InputMapPanel::revertDocument()
    {
        reloadFromFile();
    }
} // namespace sw::editor
