#include "pch.h"

#include "Editor/Panels/InputMapEditorPanel.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Widgets/ViewportInputOverlay.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Input/ActionMap.h"
#include "Engine/Input/Devices/GamepadDevice.h"
#include "Engine/Input/Devices/KeyboardDevice.h"
#include "Engine/Input/Devices/MouseDevice.h"
#include "Engine/Input/GamepadButtons.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputReplay.h"
#include "Engine/Input/KeyCodes.h"
#include "Engine/Utility/Xml/XmlDocument.h"

#include <imgui.h>

namespace sw::editor
{
    SW_LOG_CALLER( "InputMapEditorPanel" );

    InputMapEditorPanel::InputMapEditorPanel()
        : _actionMap{}
        , _replay{}
        , _inputMapPath{ "engine/input/default.input.xml" }
        , _replayFilePath{ "engine/replay/demo_01.swreplay" }
        , _newActionName{ "" }
        , _newLayerName{ "" }
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
        , _simStickX{ 0.0f }
        , _simStickY{ 0.0f }
        , _plotOffset{ 0 }
        , _capturingBindIndex{ 0 }
        , _newActionValueType{ 0 }
        , _simKeyToInject{ 1 }
        , _selectedGlyphPlatform{ 0 }
        , _bLoaded{ SW_FALSE }
        , _bCapturingKey{ SW_FALSE }
        , _bDirty{ SW_FALSE }
        , _bPlotPaused{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void InputMapEditorPanel::drawContent()
    {
        if ( _bLoaded == SW_FALSE )
        {
            reloadFromFile();
            _bLoaded = SW_TRUE;
        }

        InputManager* pInput = getService<InputManager>();
        if ( pInput != nullptr && _actionMap.getInputManager() != pInput )
        {
            _actionMap.setInputManager( pInput );
        }

        // ActionPhase 상태 머신·커맨드 히스토리·버퍼 만료 타이머 등은 update() 안에서만 갱신되므로
        // 매 프레임 호출해야 액션 테이블/콤보 테스터/버퍼링 데모가 실제로 동작합니다.
        if ( pInput != nullptr )
            _actionMap.update( ImGui::GetIO().DeltaTime );

        // 실시간 시계열 샘플링
        if ( pInput != nullptr && _bPlotPaused == SW_FALSE )
        {
            float32        lx = 0.0f, ly = 0.0f, lt = 0.0f, rt = 0.0f;
            GamepadDevice* pGamepad = pInput->getGamepad( 0 );
            if ( pGamepad != nullptr && pGamepad->isConnected() )
            {
                pGamepad->getLeftStick( lx, ly );
                lt = pGamepad->getLeftTrigger();
                rt = pGamepad->getRightTrigger();
            }

            int32 mdx = 0, mdy = 0;
            pInput->getMouseDelta( mdx, mdy );

            _arrPlotLeftStickX[_plotOffset]  = lx;
            _arrPlotLeftStickY[_plotOffset]  = ly;
            _arrPlotMouseDeltaX[_plotOffset] = static_cast<float32>( mdx );
            _arrPlotMouseDeltaY[_plotOffset] = static_cast<float32>( mdy );
            _arrPlotTriggerL[_plotOffset]    = lt;
            _arrPlotTriggerR[_plotOffset]    = rt;

            _plotOffset = ( _plotOffset + 1 ) % kPlotSampleCount;
        }

        // 리플레이 재생 업데이트
        if ( _replay.isPlaying() && pInput != nullptr )
        {
            _replay.updatePlayback( ImGui::GetIO().DeltaTime, pInput );
        }

        if ( ImGui::BeginTabBar( "InputEditorTabs" ) )
        {
            if ( ImGui::BeginTabItem( "Action Maps & Bindings" ) )
            {
                drawActionMapTab();
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

            if ( ImGui::BeginTabItem( "Input Oscilloscope (Graphs)" ) )
            {
                drawOscilloscopeTab();
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

    void InputMapEditorPanel::drawActionMapTab()
    {
        ImGui::Text( "InputMap Resource:" );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( 260.0f );
        fixed_string<constant::kMaxBuffer128> pathBuf{ _inputMapPath.c_str() };
        if ( ImGui::InputText( "##InputMapPath", pathBuf.data(), pathBuf.capacity() ) )
        {
            _inputMapPath = pathBuf.c_str();
        }

        ImGui::SameLine();
        if ( ImGui::Button( "Reload" ) )
            reloadFromFile();

        ImGui::SameLine();
        if ( ImGui::Button( "Save XML" ) )
            saveToFile();

        ImGui::SameLine();
        if ( ImGui::Button( "Revert All to Default" ) )
        {
            _actionMap.resetAllBindingsToDefault();
            _bDirty = SW_TRUE;
        }

        if ( _bDirty == SW_TRUE )
        {
            ImGui::SameLine();
            ImGui::TextColored( ImVec4( 1.0f, 0.8f, 0.2f, 1.0f ), "* Unsaved changes" );
        }

        ImGui::Separator();

        drawLayerList();
        ImGui::Separator();
        drawActionTable();
        ImGui::Separator();
        drawAddActionSection();
        drawCaptureModal();
    }

    void InputMapEditorPanel::drawLayerList()
    {
        const vector<hashed_string>& listLayer = _actionMap.getLayerNames();

        if ( ImGui::CollapsingHeader( "Input Layers", ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            if ( ImGui::BeginTable( "LayerTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg ) )
            {
                ImGui::TableSetupColumn( "Layer Name", ImGuiTableColumnFlags_WidthStretch );
                ImGui::TableSetupColumn( "Active", ImGuiTableColumnFlags_WidthFixed, 60.0f );
                ImGui::TableSetupColumn( "Priority", ImGuiTableColumnFlags_WidthFixed, 60.0f );
                ImGui::TableSetupColumn( "Stack Status", ImGuiTableColumnFlags_WidthFixed, 110.0f );
                ImGui::TableHeadersRow();

                for ( const hashed_string& layerName : listLayer )
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted( layerName.c_str() );

                    ImGui::TableNextColumn();
                    ImGui::PushID( layerName.c_str() );
                    bool bEnabled = _actionMap.isLayerEnabled( layerName );
                    if ( ImGui::Checkbox( "##Enabled", &bEnabled ) )
                    {
                        _actionMap.setLayerEnabled( layerName.view(), bEnabled );
                        _bDirty = SW_TRUE;
                    }
                    ImGui::PopID();

                    ImGui::TableNextColumn();
                    ImGui::Text( "%d", _actionMap.getLayerPriority( layerName ) );

                    ImGui::TableNextColumn();
                    if ( _actionMap.getCurrentTopLayer() == layerName.view() )
                        ImGui::TextColored( ImVec4( 0.2f, 1.0f, 0.2f, 1.0f ), "Top (Active)" );
                    else if ( bEnabled )
                        ImGui::Text( "Active" );
                    else
                        ImGui::TextDisabled( "Disabled" );
                }
                ImGui::EndTable();
            }
        }
    }

    void InputMapEditorPanel::drawActionTable()
    {
        const vector<hashed_string>& listAction = _actionMap.getActionNames();

        if ( ImGui::BeginTable( "ActionTable", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable ) )
        {
            ImGui::TableSetupColumn( "Action", ImGuiTableColumnFlags_WidthStretch );
            ImGui::TableSetupColumn( "Trigger", ImGuiTableColumnFlags_WidthFixed, 110.0f );
            ImGui::TableSetupColumn( "UI Glyph", ImGuiTableColumnFlags_WidthFixed, 90.0f );
            ImGui::TableSetupColumn( "State / Phase", ImGuiTableColumnFlags_WidthFixed, 100.0f );
            ImGui::TableSetupColumn( "Hold Time", ImGuiTableColumnFlags_WidthFixed, 80.0f );
            ImGui::TableSetupColumn( "Rebind", ImGuiTableColumnFlags_WidthFixed, 75.0f );
            ImGui::TableSetupColumn( "Reset", ImGuiTableColumnFlags_WidthFixed, 60.0f );
            ImGui::TableHeadersRow();

            for ( const hashed_string& actionName : listAction )
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted( actionName.c_str() );

                ImGui::TableNextColumn();
                const ActionTrigger trig      = _actionMap.getBindingTrigger( actionName, 0 );
                const utf8*         pTrigName = ActionMap::actionTriggerToName( trig );
                ImGui::TextUnformatted( pTrigName != nullptr ? pTrigName : "Unknown" );

                ImGui::TableNextColumn();
                const string glyph = _actionMap.getGlyphForAction( actionName.view() );
                ImGui::TextColored( ImVec4( 0.3f, 0.8f, 1.0f, 1.0f ), "%s", glyph.c_str() );

                ImGui::TableNextColumn();
                const bool        bDown = _actionMap.isActionDown( actionName );
                const bool        bTrig = _actionMap.wasActionTriggered( actionName );
                const ActionPhase phase = _actionMap.getActionPhase( actionName );
                if ( bTrig )
                    ImGui::TextColored( ImVec4( 1.0f, 0.3f, 0.3f, 1.0f ), "TRIGGERED" );
                else if ( bDown )
                    ImGui::TextColored( ImVec4( 0.2f, 1.0f, 0.3f, 1.0f ), "DOWN" );
                else if ( phase != ActionPhase::None )
                    ImGui::TextColored( ImVec4( 0.8f, 0.8f, 0.2f, 1.0f ), "ONGOING" );
                else
                    ImGui::TextDisabled( "Idle" );

                ImGui::TableNextColumn();
                const float32 holdSec = _actionMap.getActionHoldDuration( actionName );
                if ( holdSec > 0.0f )
                    ImGui::TextColored( ImVec4( 0.9f, 0.7f, 0.2f, 1.0f ), "%.2f s", static_cast<float64>( holdSec ) );
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
                    _actionMap.resetActionToDefault( actionName.view() );
                    _bDirty = SW_TRUE;
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }

    void InputMapEditorPanel::drawAddActionSection()
    {
        if ( ImGui::CollapsingHeader( "Add New Action / Layer" ) )
        {
            fixed_string<constant::kMaxBuffer64> nameBuf{ _newActionName.c_str() };
            ImGui::SetNextItemWidth( 200.0f );
            if ( ImGui::InputText( "New Action Name", nameBuf.data(), nameBuf.capacity() ) )
                _newActionName = nameBuf.c_str();

            ImGui::SameLine();
            const utf8* arrTypes[] = { "Boolean", "Axis 1D", "Vector 2D" };
            ImGui::SetNextItemWidth( 120.0f );
            ImGui::Combo( "Type", &_newActionValueType, arrTypes, 3 );

            ImGui::SameLine();
            if ( ImGui::Button( "Create Action" ) && _newActionName.empty() == false )
            {
                static constexpr InputActionValueType kArrValueType[] = { InputActionValueType::Boolean, InputActionValueType::Axis1D, InputActionValueType::Axis2D };
                const InputActionValueType            valueType       = kArrValueType[MathUtil::clamp( _newActionValueType, 0, 2 )];
                _actionMap.createAction( _newActionName.c_str(), valueType );
                _newActionName = "";
                _bDirty        = SW_TRUE;
            }
        }
    }

    void InputMapEditorPanel::drawCaptureModal()
    {
        if ( _bCapturingKey == SW_FALSE )
            return;

        ImGui::OpenPopup( "Press Key / Button To Bind" );
        if ( ImGui::BeginPopupModal( "Press Key / Button To Bind", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
        {
            ImGui::Text( "Binding for Action: %s", _selectedAction.c_str() );
            ImGui::Text( "Press any keyboard key, or click a button below to bind..." );
            ImGui::Separator();

            // 실시간 활성 입력 감지
            InputManager* pInput = getService<InputManager>();
            if ( pInput != nullptr )
            {
                for ( int32 kIdx = 1; kIdx < static_cast<int32>( Key::Count ); ++kIdx )
                {
                    const Key k = static_cast<Key>( kIdx );
                    if ( pInput->wasKeyPressed( k ) )
                    {
                        _actionMap.rebindKey( _selectedAction.c_str(), k, _capturingBindIndex );
                        _bDirty        = SW_TRUE;
                        _bCapturingKey = SW_FALSE;
                        ImGui::CloseCurrentPopup();
                        break;
                    }
                }
            }

            // 버튼 리스트 폴백
            ImGui::BeginChild( "KeyGrid", ImVec2( 450, 200 ), true );
            for ( int32 keyIndex = 1; keyIndex < static_cast<int32>( Key::Count ); ++keyIndex )
            {
                const Key   k        = static_cast<Key>( keyIndex );
                const utf8* pKeyName = KeyCodes::toName( k );
                if ( StringUtil::isNullOrEmpty( pKeyName ) == false )
                {
                    if ( ImGui::Button( pKeyName, ImVec2( 80, 24 ) ) )

                    {
                        _actionMap.rebindKey( _selectedAction.c_str(), k, _capturingBindIndex );
                        _bDirty        = SW_TRUE;
                        _bCapturingKey = SW_FALSE;
                        ImGui::CloseCurrentPopup();
                        break;
                    }
                    if ( ( keyIndex % 5 ) != 0 )
                        ImGui::SameLine();
                }
            }
            ImGui::EndChild();

            if ( ImGui::Button( "Cancel", ImVec2( 120, 0 ) ) )
            {
                _bCapturingKey = SW_FALSE;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    void InputMapEditorPanel::drawDeviceMonitorTab()
    {
        InputManager* pInput = getService<InputManager>();
        if ( pInput == nullptr )
        {
            ImGui::TextDisabled( "InputManager service is not available." );
            return;
        }

        // 1) 활성 장치 상태
        const InputDeviceType devType   = pInput->getActiveDeviceType();
        const utf8*           pTypeName = "Unknown";
        switch ( devType )
        {
            case InputDeviceType::KeyboardMouse:
                pTypeName = "Keyboard & Mouse";
                break;
            case InputDeviceType::GamepadXbox:
                pTypeName = "Xbox Gamepad";
                break;
            case InputDeviceType::GamepadPlayStation:
                pTypeName = "PlayStation Gamepad";
                break;
            case InputDeviceType::GamepadSwitch:
                pTypeName = "Nintendo Switch Gamepad";
                break;
            default:
                pTypeName = "Unknown";
                break;
        }

        ImGui::Text( "Active Device:" );

        ImGui::SameLine();
        ImGui::TextColored( ImVec4( 0.2f, 1.0f, 0.5f, 1.0f ), "%s", pTypeName );
        ImGui::Separator();

        // 2) 키보드 실시간 모니터
        if ( ImGui::CollapsingHeader( "Keyboard Status", ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            ImGui::Text( "Held Keys:" );
            ImGui::SameLine();
            bool bAnyKey = false;
            for ( int32 kIdx = 1; kIdx < static_cast<int32>( Key::Count ); ++kIdx )
            {
                const Key k = static_cast<Key>( kIdx );
                if ( pInput->isKeyDown( k ) )
                {
                    const utf8* pName = KeyCodes::toName( k );
                    if ( pName != nullptr )
                    {
                        ImGui::SameLine();
                        ImGui::TextColored( ImVec4( 1.0f, 0.8f, 0.2f, 1.0f ), "[%s]", pName );
                        bAnyKey = true;
                    }
                }
            }
            if ( bAnyKey == false )
                ImGui::TextDisabled( "None" );
        }

        // 3) 마우스 실시간 모니터
        if ( ImGui::CollapsingHeader( "Mouse Status", ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            int32 mx = 0, my = 0;
            pInput->getMousePosition( mx, my );
            int32 dx = 0, dy = 0;
            pInput->getMouseDelta( dx, dy );
            float32 smoothDx = 0.0f, smoothDy = 0.0f;
            pInput->getSmoothMouseDelta( smoothDx, smoothDy );

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

        // 4) 게임패드 실시간 모니터
        if ( ImGui::CollapsingHeader( "Gamepad 0 Status", ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            GamepadDevice* pGamepad = pInput->getGamepad( 0 );
            if ( pGamepad != nullptr && pGamepad->isConnected() )
            {
                const GamepadBatteryInfo batInfo = pGamepad->getBatteryInfo();
                const utf8*              pBatStr = "Unknown";
                switch ( batInfo._level )
                {
                    case GamepadBatteryLevel::Empty:
                        pBatStr = "Empty";
                        break;
                    case GamepadBatteryLevel::Low:
                        pBatStr = "Low";
                        break;
                    case GamepadBatteryLevel::Medium:
                        pBatStr = "Medium";
                        break;
                    case GamepadBatteryLevel::Full:
                        pBatStr = "Full (100%)";
                        break;
                    default:
                        pBatStr = "Unknown";
                        break;
                }
                ImGui::Text( "Battery: %s", pBatStr );

                float32 lx = 0.0f, ly = 0.0f, rx = 0.0f, ry = 0.0f;
                pGamepad->getLeftStick( lx, ly );
                pGamepad->getRightStick( rx, ry );

                drawGamepadStickVisualizer( "Left Stick", lx, ly, 0.15f );
                ImGui::SameLine( 180.0f );
                drawGamepadStickVisualizer( "Right Stick", rx, ry, 0.15f );

                ImGui::SameLine( 360.0f );
                ImGui::BeginGroup();
                ImGui::Text( "Left Trigger:  %.2f", static_cast<float64>( pGamepad->getLeftTrigger() ) );
                ImGui::ProgressBar( pGamepad->getLeftTrigger(), ImVec2( 150, 14 ) );
                ImGui::Text( "Right Trigger: %.2f", static_cast<float64>( pGamepad->getRightTrigger() ) );
                ImGui::ProgressBar( pGamepad->getRightTrigger(), ImVec2( 150, 14 ) );
                ImGui::EndGroup();

                ImGui::Separator();
                ImGui::Text( "Haptic Vibration Test:" );
                ImGui::SetNextItemWidth( 120.0f );
                ImGui::SliderFloat( "Left Motor", &_testVibLeft, 0.0f, 1.0f );
                ImGui::SameLine();
                ImGui::SetNextItemWidth( 120.0f );
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
                ImGui::TextDisabled( "No Gamepad Connected on Port 0." );
            }
        }
    }

    void InputMapEditorPanel::drawGamepadStickVisualizer( const utf8* pLabel, float32 stickX, float32 stickY, float32 deadzone )
    {
        ImGui::BeginGroup();
        ImGui::Text( "%s", pLabel );
        const ImVec2  pos    = ImGui::GetCursorScreenPos();
        const float32 radius = 50.0f;
        const ImVec2  center = ImVec2( pos.x + radius, pos.y + radius );

        ImDrawList* pDraw = ImGui::GetWindowDrawList();
        pDraw->AddCircleFilled( center, radius, IM_COL32( 30, 30, 30, 255 ) );
        pDraw->AddCircle( center, radius, IM_COL32( 100, 100, 100, 255 ) );
        pDraw->AddCircle( center, radius * deadzone, IM_COL32( 80, 40, 40, 255 ) );

        const ImVec2 dotPos = ImVec2( center.x + stickX * radius, center.y - stickY * radius );
        pDraw->AddCircleFilled( dotPos, 6.0f, IM_COL32( 50, 200, 50, 255 ) );

        ImGui::Dummy( ImVec2( radius * 2.0f, radius * 2.0f ) );
        ImGui::Text( "X: %+.2f  Y: %+.2f", static_cast<float64>( stickX ), static_cast<float64>( stickY ) );
        ImGui::EndGroup();
    }

    void InputMapEditorPanel::drawConflictMatrixTab()
    {
        ImGui::Text( "Key Binding Conflict Matrix & One-Click Resolver" );
        ImGui::TextDisabled( "Detects duplicated key bindings across actions and provides instant collision resolution." );
        ImGui::Separator();

        const vector<hashed_string>& listAction     = _actionMap.getActionNames();
        bool                         bFoundConflict = false;

        if ( ImGui::BeginTable( "ConflictTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg ) )
        {
            ImGui::TableSetupColumn( "Action A", ImGuiTableColumnFlags_WidthStretch );
            ImGui::TableSetupColumn( "Action B", ImGuiTableColumnFlags_WidthStretch );
            ImGui::TableSetupColumn( "Colliding Key", ImGuiTableColumnFlags_WidthFixed, 100.0f );
            ImGui::TableSetupColumn( "Swap", ImGuiTableColumnFlags_WidthFixed, 75.0f );
            ImGui::TableSetupColumn( "Override B", ImGuiTableColumnFlags_WidthFixed, 85.0f );
            ImGui::TableHeadersRow();

            for ( size_t idxA = 0; idxA < listAction.size(); ++idxA )
            {
                const hashed_string& nameA  = listAction[idxA];
                const string         glyphA = _actionMap.getGlyphForAction( nameA.view() );
                if ( glyphA == "[ Unbound ]" || glyphA.empty() )
                    continue;

                for ( size_t idxB = idxA + 1; idxB < listAction.size(); ++idxB )
                {
                    const hashed_string& nameB  = listAction[idxB];
                    const string         glyphB = _actionMap.getGlyphForAction( nameB.view() );

                    if ( glyphA == glyphB )
                    {
                        bFoundConflict = true;
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TextColored( ImVec4( 1.0f, 0.4f, 0.4f, 1.0f ), "%s", nameA.c_str() );

                        ImGui::TableNextColumn();
                        ImGui::TextColored( ImVec4( 1.0f, 0.4f, 0.4f, 1.0f ), "%s", nameB.c_str() );

                        ImGui::TableNextColumn();
                        ImGui::TextColored( ImVec4( 1.0f, 0.8f, 0.2f, 1.0f ), "%s", glyphA.c_str() );

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
                            _actionMap.rebindKey( nameB.c_str(), Key::Unknown, 0 );
                            _bDirty = SW_TRUE;
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
            ImGui::TextColored( ImVec4( 0.2f, 1.0f, 0.2f, 1.0f ), "✓ Zero Conflicts Detected! All key bindings are completely unique." );
        }
    }

    void InputMapEditorPanel::drawOscilloscopeTab()
    {
        ImGui::Text( "Real-Time Input Time-Series Oscilloscope (Last 120 Frames)" );
        ImGui::SameLine( 450.0f );
        bool bPaused = ( _bPlotPaused == SW_TRUE );
        if ( ImGui::Checkbox( "Pause Graph", &bPaused ) )
            _bPlotPaused = bPaused ? SW_TRUE : SW_FALSE;

        ImGui::Separator();

        ImGui::Text( "Gamepad Left Stick (X/Y):" );
        ImGui::PlotLines( "Stick X", _arrPlotLeftStickX, static_cast<int32>( kPlotSampleCount ), static_cast<int32>( _plotOffset ), "X [-1.0 ~ +1.0]", -1.0f, 1.0f, ImVec2( 0, 70 ) );
        ImGui::PlotLines( "Stick Y", _arrPlotLeftStickY, static_cast<int32>( kPlotSampleCount ), static_cast<int32>( _plotOffset ), "Y [-1.0 ~ +1.0]", -1.0f, 1.0f, ImVec2( 0, 70 ) );

        ImGui::Separator();
        ImGui::Text( "Analog Triggers (LT/RT):" );
        ImGui::PlotLines( "LT", _arrPlotTriggerL, static_cast<int32>( kPlotSampleCount ), static_cast<int32>( _plotOffset ), "Left [0.0 ~ 1.0]", 0.0f, 1.0f, ImVec2( 0, 60 ) );
        ImGui::PlotLines( "RT", _arrPlotTriggerR, static_cast<int32>( kPlotSampleCount ), static_cast<int32>( _plotOffset ), "Right [0.0 ~ 1.0]", 0.0f, 1.0f, ImVec2( 0, 60 ) );

        ImGui::Separator();
        ImGui::Text( "Mouse Delta Speed (dX/dY):" );
        ImGui::PlotLines( "dX", _arrPlotMouseDeltaX, static_cast<int32>( kPlotSampleCount ), static_cast<int32>( _plotOffset ), "Delta X", -100.0f, 100.0f, ImVec2( 0, 60 ) );
        ImGui::PlotLines( "dY", _arrPlotMouseDeltaY, static_cast<int32>( kPlotSampleCount ), static_cast<int32>( _plotOffset ), "Delta Y", -100.0f, 100.0f, ImVec2( 0, 60 ) );
    }

    void InputMapEditorPanel::drawInputSimulatorTab()
    {
        InputManager* pInput = getService<InputManager>();
        if ( pInput == nullptr )
        {
            ImGui::TextDisabled( "InputManager service is not available." );
            return;
        }

        ImGui::Text( "Virtual Input Injector & Gameplay Macro Simulator" );
        ImGui::TextDisabled( "Inject virtual key, mouse, or stick events directly into the engine without physical hardware." );
        ImGui::Separator();

        ImGui::Text( "1) Key Event Injector:" );
        const utf8* arrCommonKeys[] = { "Space", "Enter", "Escape", "W", "A", "S", "D", "E", "F", "Shift", "Control" };
        const Key   arrKeyValues[]  = { Key::Space, Key::Enter, Key::Escape, Key::W, Key::A, Key::S, Key::D, Key::E, Key::F, Key::LeftShift, Key::LeftControl };

        ImGui::SetNextItemWidth( 150.0f );
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
        ImGui::SliderFloat( "Sim Stick X", &_simStickX, -1.0f, 1.0f );
        ImGui::SliderFloat( "Sim Stick Y", &_simStickY, -1.0f, 1.0f );
        if ( ImGui::Button( "Inject Stick Tilt" ) )
        {
            GamepadDevice* pGamepad = pInput->getGamepad( 0 );
            if ( pGamepad != nullptr )
            {
                pGamepad->setAxis( 0, _simStickX );
                pGamepad->setAxis( 1, _simStickY );
            }
        }
        ImGui::SameLine();
        if ( ImGui::Button( "Reset Stick to Center" ) )
        {
            _simStickX              = 0.0f;
            _simStickY              = 0.0f;
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

    void InputMapEditorPanel::drawInputReplayTab()
    {
        InputManager* pInput = getService<InputManager>();

        ImGui::Text( "Input Replay Recorder & Deterministic QA Playback" );
        ImGui::Separator();

        fixed_string<constant::kMaxBuffer128> pathBuf{ _replayFilePath.c_str() };
        ImGui::SetNextItemWidth( 260.0f );
        if ( ImGui::InputText( "Replay File", pathBuf.data(), pathBuf.capacity() ) )
            _replayFilePath = pathBuf.c_str();

        ImGui::SameLine();
        if ( ImGui::Button( "Save Replay" ) )
            _replay.saveToFile( _replayFilePath.c_str() );

        ImGui::SameLine();
        if ( ImGui::Button( "Load Replay" ) )
            _replay.loadFromFile( _replayFilePath.c_str() );

        ImGui::Separator();

        // 녹화 제어
        if ( _replay.isRecording() )
        {
            ImGui::TextColored( ImVec4( 1.0f, 0.2f, 0.2f, 1.0f ), "● RECORDING LIVE INPUTS... (Frames: %u)", _replay.getFrameCount() );
            ImGui::SameLine();
            if ( ImGui::Button( "■ Stop Recording" ) )
                _replay.stopRecording();
        }
        else
        {
            if ( ImGui::Button( "● Start Recording" ) )
                _replay.startRecording( "GameplaySession" );
        }

        ImGui::Separator();

        // 재생 제어
        ImGui::Text( "Playback Controls (Total Frames: %u, Duration: %.2f s):", _replay.getFrameCount(), static_cast<float64>( _replay.getTotalDuration() ) );

        if ( _replay.isPlaying() )
        {
            if ( ImGui::Button( "⏸ Pause" ) )
                _replay.pause();
            ImGui::SameLine();
            if ( ImGui::Button( "⏹ Stop" ) )
                _replay.stop();
        }
        else if ( _replay.isPaused() )
        {
            if ( ImGui::Button( "▶ Resume" ) )
                _replay.resume();
            ImGui::SameLine();
            if ( ImGui::Button( "⏹ Stop" ) )
                _replay.stop();
        }
        else
        {
            if ( ImGui::Button( "▶ Play Replay" ) )
                _replay.play();
        }

        ImGui::SameLine();
        if ( ImGui::Button( "⏮ Step Back" ) )
            _replay.stepBackward( pInput );

        ImGui::SameLine();
        if ( ImGui::Button( "⏭ Step Forward (1 Frame)" ) )
            _replay.stepForward( pInput );

        int32       frameIdx  = static_cast<int32>( _replay.getCurrentFrameIndex() );
        const int32 maxFrames = _replay.getFrameCount() > 0 ? static_cast<int32>( _replay.getFrameCount() - 1 ) : 0;
        if ( ImGui::SliderInt( "Timeline Frame", &frameIdx, 0, maxFrames ) )
        {
            _replay.seek( static_cast<uint32>( frameIdx ) );
        }

        const InputReplayFrame* pCurrentFrame = _replay.getCurrentFrame();
        if ( pCurrentFrame != nullptr )
        {
            fixed_string<constant::kMaxBuffer128> frameBuf;
            formatstring( frameBuf.data(), frameBuf.capacity(),
                          "Frame #%u | DeltaTime: %#s | ButtonMask: 0x%X | Events: %d",
                          pCurrentFrame->_tickNumber,
                          Fmt( static_cast<float64>( pCurrentFrame->_deltaTime ), Format().precision( 4 ) ),
                          static_cast<uint32>( pCurrentFrame->_snapshot._buttonMask ),
                          static_cast<int32>( pCurrentFrame->_listRawEvent.size() ) );
            ImGui::TextColored( ImVec4( 0.3f, 0.8f, 1.0f, 1.0f ), "%s", frameBuf.c_str() );
        }
    }

    void InputMapEditorPanel::drawGlyphPreviewerTab()
    {
        ImGui::Text( "Multi-Platform Action UI Glyph & Button Prompt Previewer" );
        ImGui::TextDisabled( "Preview how button prompts appear across Xbox, PlayStation, Nintendo Switch, and PC Keyboards." );
        ImGui::Separator();

        const utf8*                      arrPlatforms[]      = { "Xbox Controller", "PlayStation DualSense", "Nintendo Switch Pro", "PC Keyboard / Mouse" };
        static constexpr InputDeviceType kArrPreviewDevice[] = { InputDeviceType::GamepadXbox, InputDeviceType::GamepadPlayStation, InputDeviceType::GamepadSwitch, InputDeviceType::KeyboardMouse };
        ImGui::Combo( "Target Platform", &_selectedGlyphPlatform, arrPlatforms, 4 );
        const InputDeviceType previewDevice = kArrPreviewDevice[MathUtil::clamp( _selectedGlyphPlatform, 0, 3 )];
        ImGui::Separator();

        const vector<hashed_string>& listAction = _actionMap.getActionNames();
        if ( ImGui::BeginTable( "GlyphTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg ) )
        {
            ImGui::TableSetupColumn( "Action Name", ImGuiTableColumnFlags_WidthStretch );
            ImGui::TableSetupColumn( "Key Binding", ImGuiTableColumnFlags_WidthFixed, 120.0f );
            ImGui::TableSetupColumn( "UI Prompt (Glyph)", ImGuiTableColumnFlags_WidthFixed, 140.0f );
            ImGui::TableSetupColumn( "Platform Style", ImGuiTableColumnFlags_WidthFixed, 140.0f );
            ImGui::TableHeadersRow();

            for ( const hashed_string& actionName : listAction )
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted( actionName.c_str() );

                ImGui::TableNextColumn();
                const string glyph = _actionMap.getGlyphForAction( actionName.view() );
                ImGui::TextUnformatted( glyph.c_str() );

                ImGui::TableNextColumn();
                const string previewGlyph = _actionMap.getGlyphForAction( actionName.view(), previewDevice );
                if ( _selectedGlyphPlatform == 0 )
                    ImGui::TextColored( ImVec4( 0.2f, 1.0f, 0.4f, 1.0f ), "[ Ⓨ Xbox ] %s", previewGlyph.c_str() );
                else if ( _selectedGlyphPlatform == 1 )
                    ImGui::TextColored( ImVec4( 0.3f, 0.6f, 1.0f, 1.0f ), "[ ▲ DualSense ] %s", previewGlyph.c_str() );
                else if ( _selectedGlyphPlatform == 2 )
                    ImGui::TextColored( ImVec4( 1.0f, 0.3f, 0.3f, 1.0f ), "[ X Switch ] %s", previewGlyph.c_str() );
                else
                    ImGui::TextColored( ImVec4( 0.9f, 0.9f, 0.9f, 1.0f ), "[ KeyCap ] %s", previewGlyph.c_str() );

                ImGui::TableNextColumn();
                ImGui::TextDisabled( "%s", arrPlatforms[_selectedGlyphPlatform] );
            }
            ImGui::EndTable();
        }
    }

    void InputMapEditorPanel::drawViewportOverlayTab()
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

        bool bFeed = ( config._bShowCommandHistory == SW_TRUE );
        if ( ImGui::Checkbox( "Show Live Action Trigger Stream", &bFeed ) )
            config._bShowCommandHistory = bFeed ? SW_TRUE : SW_FALSE;
    }

    void InputMapEditorPanel::drawCombosAndBufferTab()
    {
        ImGui::Text( "Fighting Game Combo Tester & Input Buffer Inspector" );
        ImGui::Separator();

        fixed_string<constant::kMaxBuffer64> patternBuf{ _testComboPattern.c_str() };
        ImGui::SetNextItemWidth( 150.0f );
        if ( ImGui::InputText( "Combo Pattern (Numpad Notation)", patternBuf.data(), patternBuf.capacity() ) )
            _testComboPattern = patternBuf.c_str();

        ImGui::SameLine();
        const bool bPatternMatched = _actionMap.checkCommandPattern( _testComboPattern.c_str(), 0.8f );
        if ( bPatternMatched )
            ImGui::TextColored( ImVec4( 0.2f, 1.0f, 0.2f, 1.0f ), "MATCHED! (Success)" );
        else
            ImGui::TextDisabled( "Waiting for input..." );

        ImGui::Text( "Legend: 236P = Hadoken (Down, DownRight, Right + Punch), 623P = Shoryuken" );
        ImGui::Separator();

        ImGui::Text( "Action Input Buffering:" );
        if ( ImGui::Button( "Buffer 'Attack' (0.3s)" ) )
            _actionMap.bufferAction( "Attack", 0.3f );
        ImGui::SameLine();
        if ( ImGui::Button( "Buffer 'Jump' (0.3s)" ) )
            _actionMap.bufferAction( "Jump", 0.3f );

        ImGui::SameLine();
        if ( ImGui::Button( "Consume 'Attack'" ) )
        {
            if ( _actionMap.consumeBufferedAction( "Attack" ) )
                SW_LOG_INFO( "Successfully consumed buffered 'Attack'!" );
        }
    }

    void InputMapEditorPanel::reloadFromFile()
    {
        _actionMap.loadFromResource( _inputMapPath.c_str() );
        _bDirty = SW_FALSE;
        SW_LOG_INFO( "Reloaded InputMap from %#", _inputMapPath.c_str() );
    }

    void InputMapEditorPanel::saveToFile()
    {
        _actionMap.saveUserBindings( _inputMapPath.c_str() );
        _bDirty = SW_FALSE;
        SW_LOG_INFO( "Saved InputMap to %#", _inputMapPath.c_str() );
    }
} // namespace sw::editor
