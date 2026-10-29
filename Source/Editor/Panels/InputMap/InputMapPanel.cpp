#include "pch.h"

#include "Editor/Panels/InputMap/InputMapPanel.h"

#include "Core/Container/StringUtil.h"
#include "Core/Log/LogOnce.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Backend/ImGuiKeyForward.h"
#include "Editor/Common/Config/EditorToolDefaults.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorTextExtentImGui.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Widgets/Model/EditorListFilter.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"
#include "Editor/Viewport/ViewportInputOverlay.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/FileSystem/AssetPaths.h"
#include "Engine/Input/Devices/GamepadDevice.h"
#include "Engine/Input/Devices/KeyboardDevice.h"
#include "Engine/Input/Devices/MouseDevice.h"
#include "Engine/Input/GamepadButtonUtil.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/KeyCodeUtil.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Input/Virtual/InputReplay.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

#include <imgui.h>

namespace sw::editor
{
    SW_EDITOR_PANEL( InputMapPanel, InputMapPanel::kPanelID, EditorPanelCategory::Tool, 1800 );

    namespace
    {
        /** @brief 이 TU 전용 도우미 모음입니다. 유니티 빌드에서 이름이 충돌하지 않도록 TU 이름을 붙입니다. */
        struct InputMapPanelInternal
        {
            static constexpr Color kStickBg{ Color::fromBytes( 30, 30, 30, 255 ) };
            static constexpr Color kStickRing{ Color::fromBytes( 100, 100, 100, 255 ) };
            static constexpr Color kStickDeadzone{ Color::fromBytes( 80, 40, 40, 255 ) };
            static constexpr Color kStickDot{ Color::fromBytes( 50, 200, 50, 255 ) };

            /** @brief 마우스 버튼 상태 문구의 색입니다. 눌림과 뗌에 따라 다릅니다. */
            static constexpr Color kButtonDownText{ 0.2f, 1.0f, 0.2f, 1.0f };
            static constexpr Color kButtonUpText{ 0.4f, 0.4f, 0.4f, 1.0f };
            /** @brief 테이블의 열 하나입니다. 폭이 0 이면 남는 폭을 나눠 가집니다(Stretch). */
            struct TableColumn
            {
                const utf8* _pLabel;
                float32     _width;
            };

            /** @brief 테이블을 열고 열과 헤더 행을 만듭니다. 열렸으면 true 이며, 이때 호출하는 쪽이 `ImGui::EndTable` 을 호출해야 합니다. */
            template <size_t kColumnCount>
            static bool beginColumnTable( const utf8* pTableID, const TableColumn ( &arrColumn )[kColumnCount], ImGuiTableFlags flags )
            {
                if ( ImGui::BeginTable( pTableID, static_cast<int32>( kColumnCount ), flags ) == false )
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

            /** @brief 새 행을 열고 첫 열에 이름을 쓴 뒤 둘째 열로 이동합니다. 액션과 레이어 테이블의 행 머리입니다. */
            static void beginNamedRow( const hashed_string& name )
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted( name.c_str() );
                ImGui::TableNextColumn();
            }

            /**
             * @brief 활성 입력 장치 종류의 표시 이름을 반환합니다.
             * @details 값을 먼저 넣고 switch 로 덮어쓰면 둘 중 하나는 항상 쓰이지 않는 저장이 된다. 분석기가 열거자를 모두 알기
             *          때문에 default 도 쓰이지 않는다. 값을 반환하는 함수로 두면 이런 문제가 생기지 않는다.
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

            /** @brief 게임패드 배터리 잔량의 표시 이름을 반환합니다. */
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
        : IEditorPanel( false ) // 필요할 때 여는 도구이므로 닫힌 채 시작한다. 열린 채 시작하면 떠 있는 창이 화면 가운데를 덮는다
        , _inputMap{}
        , _replay{}
        , _inputMapPath{ getSubsystem<const EngineDefaultAssets>()._debugKeyInputMap } // 처음 여는 맵은 디버그 키 맵이다 — 경로의 원본은 enginedefaultassets.xml
        , _replayFilePath{ getEditorToolDefaults()._inputReplayFile }                  // 처음 가리키는 파일 — 원본은 editortooldefaults.json
        , _newActionName{ "" }
        , _selectedAction{ "" }
        , _arrActionFilter{}
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
        , _bLoaded{ SW_FALSE }
        , _bCapturingKey{ SW_FALSE }
        , _bPlotPaused{ SW_FALSE }
        , _bSelectFirstTab{ SW_TRUE }
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

        InputManager& input = editor::getSubsystem<InputManager>();
        if ( _inputMap.getInputManager() != &input )
            _inputMap.setInputManager( &input );

        // ActionPhase 상태 머신, 커맨드 히스토리, 버퍼 만료 타이머 등은 update() 안에서만 갱신되므로,
        // 프레임마다 호출해야 액션 테이블, 콤보 테스터, 버퍼링 데모가 실제로 동작한다.
        _inputMap.update( ImGui::GetIO().DeltaTime );

        // 실시간 시계열 샘플링
        if ( _bPlotPaused == SW_FALSE )
        {
            float32        lx = 0.0f, ly = 0.0f, lt = 0.0f, rt = 0.0f;
            GamepadDevice* pGamepad = input.getGamepad( 0 );
            if ( pGamepad != nullptr && pGamepad->isConnected() )
            {
                const float2 vecLeftStick1 = pGamepad->getLeftStick();
                lx                         = vecLeftStick1._x;
                ly                         = vecLeftStick1._y;
                lt                         = pGamepad->getLeftTrigger();
                rt                         = pGamepad->getRightTrigger();
            }

            int32      mdx = 0, mdy = 0;
            const int2 vecMouseDelta1 = input.getMouseDelta();
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

        // 입력 녹화는 입력 프레임마다 한 번, 그 프레임에 장치에 적용된 이벤트를 기록한다.
        const bool bNewRecordingFrame = _replay.isRecording() && input.getBeginFrameCount() != _recordedBeginFrameCount;
        if ( bNewRecordingFrame )
        {
            _recordedBeginFrameCount = input.getBeginFrameCount();
            _replay.recordFrame( ImGui::GetIO().DeltaTime, input.getLastFrameEvents() );
        }
        // 재생(가상 입력으로 붙인 리플레이)이 끝나면 해제한다.
        const bool bReplayFinished = input.getVirtualInput() == &_replay && _replay.isFinished( input.getVirtualFrameIndex() );
        if ( bReplayFinished )
            input.detachVirtualInput();

        // 탭은 셋이다. 편집(Mappings), 들여다보기(Debug), 녹화와 주입(Replay & Inject)이다. 언리얼의 Enhanced Input 은 액션, 매핑, 트리거를 한 화면에서 편집한다.
        if ( ImGui::BeginTabBar( "InputEditorTabs" ) )
        {
            const ImGuiTabItemFlags mappingsFlags = _bSelectFirstTab ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
            _bSelectFirstTab                      = SW_FALSE;
            if ( ImGui::BeginTabItem( "Mappings", nullptr, mappingsFlags ) )
            {
                drawMappingsTab();
                ImGui::EndTabItem();
            }
            const bool bDebugTab = ImGui::BeginTabItem( "Debug" );
            EditorSelfTestMarks::note( "inputMap.tab.debug" );
            if ( bDebugTab )
            {
                drawDebugTab();
                ImGui::EndTabItem();
            }
            if ( ImGui::BeginTabItem( "Replay & Inject" ) )
            {
                drawInputReplayTab();
                ImGui::Separator();
                drawInputSimulatorTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        drawCaptureModal();
    }

    bool InputMapPanel::readSelectedBindingCount( uint32& outCount ) const
    {
        if ( _selectedAction.empty() )
            return false;
        outCount = _inputMap.getBindingCount( hashed_string( _selectedAction.c_str() ) );
        return true;
    }

    bool InputMapPanel::readSelectedTrigger( uint32& outTrigger ) const
    {
        if ( _selectedAction.empty() || _inputMap.getBindingCount( hashed_string( _selectedAction.c_str() ) ) == 0 )
            return false;
        outTrigger = static_cast<uint32>( _inputMap.getBindingTrigger( hashed_string( _selectedAction.c_str() ), 0 ) );
        return true;
    }

    void InputMapPanel::drawMappingsTab()
    {
        ImGui::TextUnformatted( "Input Map" );
        ImGui::SameLine();
        (void)_inputMapPath.drawField( "##InputMapPath", 260.0f ); // 확정한 경로는 Reload · Save 가 쓴다 — 바꾸기만 해서는 다시 읽지 않는다
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "MapPathTip" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Reload" ) ); // 경로 칸이 고정 폭이라 좁은 패널에서는 단추가 창 밖에 놓이므로 줄을 바꾼다
        if ( ImGui::Button( "Reload" ) )
            reloadFromFile();
        EditorSelfTestMarks::note( "inputMap.reload" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "ReloadTip" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Save" ) );
        if ( ImGui::Button( "Save" ) )
            (void)saveToFile(); // 실패는 saveToFile 이 알리고 dirty 가 남는다
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "SaveTip" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Revert All to Default" ) );
        if ( ImGui::Button( "Revert All to Default" ) )
        {
            _inputMap.resetAllBindingsToDefault();
            markDocumentDirty();
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "RevertAllTip" ) );
        if ( isDocumentDirty() )
        {
            ImGui::SameLine();
            EditorThemeUtil::textWarning( SW_LOCKEY( "EditorInputMap", "UnsavedChanges" ) );
        }
        InputMapConflicts::collect( _inputMap, _listConflict );
        if ( _listConflict.empty() == false )
        {
            ImGui::SameLine();
            const string conflictText = SW_LOCKEYFORMAT( "EditorInputMap", "ConflictCount",
                                                         TextArgumentList().addInteger( "count", static_cast<int64>( _listConflict.size() ) ) );
            EditorThemeUtil::textError( conflictText.c_str() );
        }
        ImGui::Separator();

        const float32 availWidth = ImGui::GetContentRegionAvail().x;
        const float32 listWidth  = MathUtil::max( availWidth * 0.42f, measureExtent( EditorTextExtent::ofText( 27.0f, 0.0f ) ).x );
        if ( ImGui::BeginChild( "InputMapLeft", ImVec2{ listWidth, 0.0f }, ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX ) )
        {
            drawActionList();
            drawLayerList();
            drawAddActionSection();
        }
        ImGui::EndChild();
        ImGui::SameLine();
        if ( ImGui::BeginChild( "InputMapRight", ImVec2{ 0.0f, 0.0f }, ImGuiChildFlags_Borders ) )
            drawActionDetails();
        ImGui::EndChild();
    }

    void InputMapPanel::drawActionList()
    {
        ImGui::SeparatorText( "Actions" );
        EditorWidgets::drawSearchField( "##inputMapActionFilter", _arrActionFilter, constant::kMaxBuffer64, SW_LOCKEY( "EditorInputMap", "FilterActionsHint" ) );
        EditorSelfTestMarks::note( "inputMap.filter" );
        const EditorListFilter filter{ string_view( _arrActionFilter ) };
        for ( const hashed_string& actionName : _inputMap.getActionNames() )
        {
            if ( filter.matches( actionName.view() ) == false )
                continue;
            const bool bConflict = InputMapConflicts::involves( _listConflict, actionName );
            const bool bDown     = _inputMap.isActionDown( actionName );
            ImGui::PushID( actionName.c_str() );
            if ( bConflict )
                EditorThemeUtil::pushTextColor( EditorThemeUtil::getErrorColor() );
            if ( ImGui::Selectable( actionName.c_str(), _selectedAction == actionName.view() ) )
                _selectedAction = actionName.c_str();
            if ( bConflict )
                EditorThemeUtil::popTextColor();
            if ( EditorSelfTestMarks::isEnabled() )
                EditorSelfTestMarks::note( ( string{ "inputMap.action." } + actionName.c_str() ).c_str() );
            if ( bConflict )
                EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "ActionConflictTip" ) );
            // 키 글리프는 오른쪽 끝에 맞춘다 — 고정 폭으로 비워 두면 "[ LeftControl+... ]" 같은 긴 글리프가 잘린다.
            const string glyphText = bDown ? string{ "DOWN" } : _inputMap.getGlyphForAction( actionName );
            ImGui::SameLine( MathUtil::max( ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize( glyphText.c_str() ).x - ImGui::GetStyle().ItemSpacing.x, 0.0f ) );
            if ( bDown )
                EditorThemeUtil::textSuccess( glyphText.c_str() );
            else
                ImGui::TextDisabled( "%s", glyphText.c_str() );
            ImGui::PopID();
        }
    }

    void InputMapPanel::drawActionDetails()
    {
        if ( _selectedAction.empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorInputMap", "SelectActionHint" ) );
            return;
        }
        const hashed_string action( _selectedAction.c_str() );
        ImGui::SeparatorText( _selectedAction.c_str() );
        const ActionPhase phase = _inputMap.getActionPhase( action );
        ImGui::Text( "State: %s  hold %.2f s", _inputMap.wasActionTriggered( action ) ? "TRIGGERED" : ( _inputMap.isActionDown( action ) ? "DOWN" : ( phase != ActionPhase::None ? "ONGOING" : "Idle" ) ),
                     static_cast<float64>( _inputMap.getActionHoldDuration( action ) ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Reset to Default" ) );
        if ( ImGui::SmallButton( "Reset to Default" ) && _inputMap.resetActionToDefault( action ) )
            markDocumentDirty();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "ResetActionTip" ) );

        // 바인딩마다 키(글리프), 레이어, 발화 규칙(트리거), 다시 잡기 버튼을 둔다.
        ImGui::SeparatorText( "Bindings" );
        const uint32 bindingCount = _inputMap.getBindingCount( action );
        for ( uint32 bindIndex = 0; bindIndex < bindingCount; ++bindIndex )
        {
            const ActionBinding* pBinding = _inputMap.getBinding( action, bindIndex );
            if ( pBinding == nullptr )
                continue;
            ImGui::PushID( static_cast<int32>( bindIndex ) );
            drawBindingRow( action, bindIndex, *pBinding );
            ImGui::PopID();
        }
        if ( bindingCount == 0 )
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorInputMap", "NoBindings" ) );

        drawActionConflicts( action );
        drawPromptGlyphs( action );
    }

    void InputMapPanel::drawBindingRow( const hashed_string& action, uint32 bindIndex, const ActionBinding& binding )
    {
        const float32 rightEdgeX = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x; // 이 줄이 그릴 수 있는 오른쪽 끝(화면 좌표)
        InputSlot     slot{};
        const bool    bRebindable = _inputMap.findRebindSlot( action, bindIndex, slot );
        const string  slotGlyph   = bRebindable ? InputMap::getGlyphForSlot( slot, InputGlyphStyle::KeyboardMouse ) : string{ BindingKinds::toName( binding._kind ) };
        ImGui::Text( "%u  %s", bindIndex, slotGlyph.c_str() );
        ImGui::SameLine( ImGui::GetFontSize() * 9.0f );
        ImGui::TextDisabled( "%s", _inputMap.findBindingLayer( action, bindIndex ).c_str() );
        ImGui::SameLine( ImGui::GetFontSize() * 15.0f );
        drawTriggerCombo( action, bindIndex );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Rebind" ) );
        ImGui::BeginDisabled( bRebindable == false );
        if ( ImGui::SmallButton( "Rebind" ) )
        {
            _capturingBindIndex = bindIndex;
            _bCapturingKey      = SW_TRUE;
        }
        ImGui::EndDisabled();
        if ( bRebindable == false )
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "NotRebindableTip" ) );
        else
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "RebindTip" ) );
        drawTriggerOptions( action, bindIndex, binding, rightEdgeX );
    }

    void InputMapPanel::drawTriggerCombo( const hashed_string& action, uint32 bindIndex )
    {
        // 가장 긴 트리거 이름(HoldThreshold · HoldAndRelease)이 잘리지 않을 만큼 — 고정 8 em 이면 "HoldThresho" 로 끊긴다.
        ImGui::SetNextItemWidth( ImGui::CalcTextSize( "HoldAndRelease" ).x + ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 2.0f );
        const ActionTrigger trigger  = _inputMap.getBindingTrigger( action, bindIndex );
        const utf8*         pCurrent = InputMap::actionTriggerToName( trigger );
        const bool          bOpen    = ImGui::BeginCombo( "##trigger", pCurrent != nullptr ? pCurrent : "?" );
        if ( EditorSelfTestMarks::isEnabled() )
            EditorSelfTestMarks::note( ( "inputMap.binding." + to_string( static_cast<uint64>( bindIndex ) ) + ".trigger" ).c_str() );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "TriggerTip" ) );
        if ( bOpen == false )
            return;
        for ( uint32 option = 0; option < static_cast<uint32>( ActionTrigger::Count ); ++option )
        {
            const ActionTrigger value = static_cast<ActionTrigger>( option );
            if ( ImGui::Selectable( InputMap::actionTriggerToName( value ), value == trigger ) && _inputMap.setBindingTrigger( action, bindIndex, value ) )
                markDocumentDirty();
            if ( EditorSelfTestMarks::isEnabled() )
                EditorSelfTestMarks::note( ( "inputMap.trigger." + to_string( static_cast<uint64>( option ) ) ).c_str() );
        }
        ImGui::EndCombo();
    }

    void InputMapPanel::drawTriggerOptions( const hashed_string& action, uint32 bindIndex, const ActionBinding& binding, float32 rightEdgeX )
    {
        // 바인딩마다 발화 시간(초)과 값 부호 뒤집기. 시간을 쓰지 않는 트리거(Pressed 등)면 시간 필드는 숨긴다.
        const float32 effectiveTime = _inputMap.getEffectiveTriggerTime( binding );
        const bool    bUsesTime     = effectiveTime > 0.0f;
        const bool    bUsesTaps     = binding._trigger == ActionTrigger::MultiTap;
        // 시간 · 횟수 필드가 필요하거나 Negate 가 남은 폭에 안 들어가면 패널 오른쪽 밖으로 잘린다 — 콤보와 같은 열 위치에서 시작하는 다음 줄로 내린다.
        const float32 negateWidth       = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize( "Negate" ).x;
        const bool    bNegateFits       = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + negateWidth <= rightEdgeX;
        bool          bLineStart        = false;
        const bool    bWrapToSecondLine = bUsesTime || bUsesTaps || bNegateFits == false;
        if ( bWrapToSecondLine )
        {
            ImGui::SetCursorPosX( ImGui::GetFontSize() * 15.0f );
            bLineStart = true;
        }
        if ( bUsesTime )
        {
            float32 editTime = effectiveTime;
            if ( bLineStart == false )
                ImGui::SameLine();
            bLineStart = false;
            ImGui::SetNextItemWidth( ImGui::GetFontSize() * 4.0f );
            if ( ImGui::DragFloat( "s##time", &editTime, 0.01f, 0.01f, 10.0f, "%.2f" ) && _inputMap.setBindingTriggerTime( action, bindIndex, editTime ) )
                markDocumentDirty();
        }
        // MultiTap 만 누름 횟수를 고른다(2 이상).
        if ( bUsesTaps )
        {
            int32 editTaps = static_cast<int32>( binding._tapCount );
            if ( bLineStart == false )
                ImGui::SameLine();
            bLineStart = false;
            ImGui::SetNextItemWidth( ImGui::GetFontSize() * 3.0f );
            if ( ImGui::DragInt( "taps##taps", &editTaps, 0.1f, 2, 9 ) && _inputMap.setBindingTapCount( action, bindIndex, static_cast<uint8>( MathUtil::clamp( editTaps, 2, 9 ) ) ) )
                markDocumentDirty();
        }
        bool bNegate = binding._bNegate;
        if ( bLineStart == false )
            EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( "Negate" ) );
        if ( ImGui::Checkbox( "Negate", &bNegate ) && _inputMap.setBindingNegate( action, bindIndex, bNegate ) )
            markDocumentDirty();
    }

    void InputMapPanel::drawActionConflicts( const hashed_string& action )
    {
        // 충돌은 같은 레이어에서 같은 키를 쓰는 다른 액션이다. 이쪽을 다시 잡거나 저쪽 바인딩을 비운다.
        if ( InputMapConflicts::involves( _listConflict, action ) == false )
            return;
        ImGui::SeparatorText( "Conflicts" );
        for ( size_t index = 0; index < _listConflict.size(); ++index )
        {
            const InputMapConflict& conflict = _listConflict[index];
            const bool              bIsA     = conflict._actionA == action;
            if ( bIsA == false && conflict._actionB != action )
                continue;
            const hashed_string& other      = bIsA ? conflict._actionB : conflict._actionA;
            const uint32         otherIndex = bIsA ? conflict._bindIndexB : conflict._bindIndexA;
            ImGui::PushID( static_cast<int32>( index ) );
            fixed_string<constant::kMaxBuffer256> conflictText;
            formatstring( conflictText.data(), conflictText.capacity(), "%# on %# (layer %#)", InputMap::getGlyphForSlot( conflict._slot, InputGlyphStyle::KeyboardMouse ).c_str(),
                          other.c_str(), conflict._layer.c_str() );
            EditorThemeUtil::textError( conflictText.c_str() );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Rebind this" ) );
            if ( ImGui::SmallButton( "Rebind this" ) )
            {
                _capturingBindIndex = bIsA ? conflict._bindIndexA : conflict._bindIndexB;
                _bCapturingKey      = SW_TRUE;
            }
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "RebindThisTip" ) );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Unbind other" ) );
            if ( ImGui::SmallButton( "Unbind other" ) && _inputMap.rebindKey( other, Key::Unknown, otherIndex ) )
                markDocumentDirty();
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "UnbindOtherTip" ) );
            ImGui::PopID();
        }
    }

    void InputMapPanel::drawPromptGlyphs( const hashed_string& action )
    {
        // 플랫폼 글리프는 화면에 표시할 버튼 아이콘이다. 언리얼 CommonUI 의 입력 표시에 해당한다.
        ImGui::SeparatorText( "Prompt Glyphs" );
        static constexpr InputGlyphStyle kArrStyle[]     = { InputGlyphStyle::KeyboardMouse, InputGlyphStyle::GamepadXbox, InputGlyphStyle::GamepadPlayStation, InputGlyphStyle::GamepadSwitch };
        static constexpr const utf8*     kArrStyleName[] = { "Keyboard / Mouse", "Xbox", "PlayStation", "Switch" };
        for ( size_t index = 0; index < 4; ++index )
        {
            ImGui::TextDisabled( "%s", kArrStyleName[index] );
            ImGui::SameLine( ImGui::CalcTextSize( kArrStyleName[0] ).x + ImGui::GetStyle().ItemSpacing.x * 2.0f ); // 가장 긴 이름 뒤에 맞춘다(고정 폭이면 글꼴에 따라 겹친다)
            ImGui::TextUnformatted( _inputMap.getGlyphForAction( action, kArrStyle[index] ).c_str() );
        }
    }

    void InputMapPanel::drawDebugTab()
    {
        if ( ImGui::CollapsingHeader( "Devices", ImGuiTreeNodeFlags_DefaultOpen ) )
            drawDeviceMonitorTab();
        if ( ImGui::CollapsingHeader( "Graphs" ) )
            drawInputGraphTab();
        if ( ImGui::CollapsingHeader( "Viewport HUD" ) )
            drawViewportOverlayTab();
        if ( ImGui::CollapsingHeader( "Command Pattern Tester" ) )
            drawCommandPatternTester();
    }

    void InputMapPanel::drawCommandPatternTester()
    {
        EditorWidgets::drawTextField( "Pattern (numpad notation)", _testComboPattern, 150.0f );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "PatternTip" ) );
        ImGui::SameLine();
        if ( _inputMap.getActionBuffer().wasCommandPatternTriggered( sw::hashed_string( _testComboPattern.c_str() ), 0.8f ) )
            EditorThemeUtil::textSuccess( "MATCHED" );
        else
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorInputMap", "WaitingForInput" ) );
        ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorInputMap", "PatternExample" ) );
    }

    void InputMapPanel::drawLayerList()
    {
        const vector<hashed_string>& listLayer = _inputMap.getLayerNames();

        if ( ImGui::CollapsingHeader( "Input Layers" ) )
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
                    EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "LayerEnabledTip" ) );
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

    void InputMapPanel::drawAddActionSection()
    {
        if ( ImGui::CollapsingHeader( "Add New Action / Layer" ) )
        {
            EditorWidgets::drawTextField( "New Action Name", _newActionName, 200.0f );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "NewActionNameTip" ) );

            ImGui::SameLine();
            const utf8* arrTypes[] = { "Boolean", "Axis 1D", "Vector 2D" };
            EditorChrome::setNextItemWidthColumns( 16.0f );
            ImGui::Combo( "Type", &_newActionValueType, arrTypes, 3 );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "NewActionTypeTip" ) );

            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Create Action" ) );
            if ( ImGui::Button( "Create Action" ) && _newActionName.empty() == false )
            {
                static constexpr InputActionValueType kArrValueType[] = { InputActionValueType::Boolean, InputActionValueType::Axis1D, InputActionValueType::Axis2D };
                const InputActionValueType            valueType       = kArrValueType[MathUtil::clamp( _newActionValueType, 0, 2 )];
                _inputMap.createAction( sw::hashed_string( _newActionName.c_str() ), valueType );
                _newActionName = "";
                markDocumentDirty();
            }
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "CreateActionTip" ) );
        }
    }

    void InputMapPanel::drawCaptureModal()
    {
        if ( _bCapturingKey == SW_FALSE )
            return;

        EditorInputGUI::openPopup( "Press Key / Button To Bind" );
        if ( EditorWidgets::beginCenteredModal( "Press Key / Button To Bind" ) )
        {
            ImGui::Text( "Binding for Action: %s", _selectedAction.c_str() );
            ImGui::TextUnformatted( SW_LOCKEY( "EditorInputMap", "CaptureInstruction" ) );
            ImGui::Separator();

            // 실시간 활성 입력 감지. 바인딩 창은 ImGui 모달이라 떠 있는 동안 ImGui 가 키보드를 가져가고(`WantCaptureKeyboard`),
            // 에디터는 그 키를 게임 입력으로 넘기지 않는다(`ImGuiEditor::processEvent`). 그래서 키는 창이 받은 곳(ImGui)에서 읽는다.
            const Key pressedKey = ImGuiKeyForward::findPressedKey();
            if ( pressedKey != Key::Unknown )
            {
                rebindSelectedAction( pressedKey );
                _bCapturingKey = SW_FALSE;
                ImGui::CloseCurrentPopup();
            }

            // 버튼 리스트 폴백
            ImGui::BeginChild( "KeyGrid", measureExtent( EditorTextExtent::ofText( 60.0f, 11.0f ) ), true );
            for ( int32 keyIndex = 1; keyIndex < static_cast<int32>( Key::Count ); ++keyIndex )
            {
                const Key   key      = static_cast<Key>( keyIndex );
                const utf8* pKeyName = KeyCodeUtil::toName( key );
                if ( StringUtil::isNullOrEmpty( pKeyName ) == false )
                {
                    if ( ImGui::Button( pKeyName, measureExtent( EditorTextExtent::ofText( 11.0f, 1.25f ) ) ) )

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

            if ( ImGui::Button( "Cancel", measureExtent( EditorTextExtent::ofText( 16.0f, 0.0f ) ) ) )
            {
                _bCapturingKey = SW_FALSE;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    void InputMapPanel::rebindSelectedAction( sw::Key newKey )
    {
        // 어느 레이어에서 충돌을 따져야 하는지는 그 바인딩이 직접 안다.
        const sw::ActionBinding* pBinding = _inputMap.getBinding( sw::hashed_string( _selectedAction.c_str() ), _capturingBindIndex );
        const string_view        layer    = ( pBinding != nullptr ) ? pBinding->_layer.view() : string_view{};

        sw::string conflictingAction;
        if ( _inputMap.hasBindingConflict( sw::InputSlot::fromKey( newKey ), sw::hashed_string( layer ), conflictingAction ) && conflictingAction != _selectedAction )
        {
            SW_LOG_WARNING( "Binding '%#' to %# - conflicts with '%#' on the same layer.",
                            _selectedAction.c_str(), sw::KeyCodeUtil::toName( newKey ), conflictingAction.c_str() );
        }

        // 키 하나로 바꿀 수 없는 바인딩(합성 축, 스틱)이면 바뀌지 않는다. 경고는 InputMap 이 남긴다. 이때는 문서를 dirty 로 표시하지 않는다.
        if ( _inputMap.rebindKey( sw::hashed_string( _selectedAction.c_str() ), newKey, _capturingBindIndex ) )
            markDocumentDirty();
    }

    void InputMapPanel::drawDeviceMonitorTab()
    {
        InputManager& input = editor::getSubsystem<InputManager>();

        // 1) 활성 장치 상태
        const InputGlyphStyle devType   = input.getActiveGlyphStyle();
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
        InputManager& input = editor::getSubsystem<InputManager>();

        // 2) 키보드 실시간 모니터
        if ( ImGui::CollapsingHeader( "Keyboard Status", ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            ImGui::Text( "Held Keys:" );
            ImGui::SameLine();
            bool bAnyKey = false;
            for ( int32 keyIndex = 1; keyIndex < static_cast<int32>( Key::Count ); ++keyIndex )
            {
                const Key key = static_cast<Key>( keyIndex );
                if ( input.isKeyDown( key ) )
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
        InputManager& input = editor::getSubsystem<InputManager>();

        // 3) 마우스 실시간 모니터
        if ( ImGui::CollapsingHeader( "Mouse Status", ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            int32      mx = 0, my = 0;
            const int2 vecMousePos2 = input.getMousePosition();
            mx                      = vecMousePos2._x;
            my                      = vecMousePos2._y;
            int32      dx = 0, dy = 0;
            const int2 vecMouseDelta3 = input.getMouseDelta();
            dx                        = vecMouseDelta3._x;
            dy                        = vecMouseDelta3._y;
            float32      smoothDx = 0.0f, smoothDy = 0.0f;
            const float2 vecSmoothDelta2 = input.getMouse() != nullptr ? input.getMouse()->getSmoothDelta() : float2{};
            smoothDx                     = vecSmoothDelta2._x;
            smoothDy                     = vecSmoothDelta2._y;

            ImGui::Text( "Position: (%d, %d)", mx, my );
            ImGui::SameLine( 200.0f );
            ImGui::Text( "Delta: (%d, %d)", dx, dy );
            ImGui::SameLine( 350.0f );
            ImGui::Text( "Smooth Delta: (%.2f, %.2f)", static_cast<float64>( smoothDx ), static_cast<float64>( smoothDy ) );

            ImGui::Text( "Wheel: %.2f", static_cast<float64>( input.getMouseWheel() ) );
            ImGui::SameLine( 200.0f );
            ImGui::Text( "Buttons:" );
            ImGui::SameLine();
            ImGui::TextColored( EditorColorUtil::toImVec4( input.isMouseButtonDown( MouseButton::Left ) ? InputMapPanelInternal::kButtonDownText : InputMapPanelInternal::kButtonUpText ), "[L]" );
            ImGui::SameLine();
            ImGui::TextColored( EditorColorUtil::toImVec4( input.isMouseButtonDown( MouseButton::Right ) ? InputMapPanelInternal::kButtonDownText : InputMapPanelInternal::kButtonUpText ), "[R]" );
            ImGui::SameLine();
            ImGui::TextColored( EditorColorUtil::toImVec4( input.isMouseButtonDown( MouseButton::Middle ) ? InputMapPanelInternal::kButtonDownText : InputMapPanelInternal::kButtonUpText ), "[M]" );
        }
    }

    void InputMapPanel::drawGamepadMonitor()
    {
        InputManager& input = editor::getSubsystem<InputManager>();

        // 4) 게임패드 실시간 모니터
        if ( ImGui::CollapsingHeader( "Gamepad 0 Status", ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            GamepadDevice* pGamepad = input.getGamepad( 0 );
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
                ImGui::ProgressBar( pGamepad->getLeftTrigger(), measureExtent( EditorTextExtent::ofText( 20.0f, 0.75f ) ) );
                ImGui::Text( "Right Trigger: %.2f", static_cast<float64>( pGamepad->getRightTrigger() ) );
                ImGui::ProgressBar( pGamepad->getRightTrigger(), measureExtent( EditorTextExtent::ofText( 20.0f, 0.75f ) ) );
                ImGui::EndGroup();

                ImGui::Separator();
                ImGui::Text( "Haptic Vibration Test:" );
                EditorChrome::setNextItemWidthColumns( 16.0f );
                ImGui::SliderFloat( "Left Motor", &_testVibLeft, 0.0f, 1.0f );
                EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "LeftMotorTip" ) );
                ImGui::SameLine();
                EditorChrome::setNextItemWidthColumns( 16.0f );
                ImGui::SliderFloat( "Right Motor", &_testVibRight, 0.0f, 1.0f );
                EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "RightMotorTip" ) );
                EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Test Pulse (0.3s)" ) );
                if ( ImGui::Button( "Test Pulse (0.3s)" ) )
                    pGamepad->playVibration( _testVibLeft, _testVibRight, 0.3f );
                EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "TestPulseTip" ) );
                EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Stop" ) );
                if ( ImGui::Button( "Stop" ) )
                    pGamepad->stopVibration();
                EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "StopVibrationTip" ) );
            }
            else
            {
                EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorInputMap", "NoGamepad" ) );
            }
        }
    }

    void InputMapPanel::drawGamepadStickVisualizer( const utf8* pLabel, float32 stickX, float32 stickY, float32 deadzone )
    {
        ImGui::BeginGroup();
        ImGui::Text( "%s", pLabel );
        const ImVec2  pos    = ImGui::GetCursorScreenPos();
        const float32 radius = 50.0f * EditorThemeUtil::getDPIScale();
        const ImVec2  center = ImVec2( pos.x + radius, pos.y + radius );

        ImDrawList* pDraw = ImGui::GetWindowDrawList();
        pDraw->AddCircleFilled( center, radius, EditorColorUtil::toImU32( InputMapPanelInternal::kStickBg ) );
        pDraw->AddCircle( center, radius, EditorColorUtil::toImU32( InputMapPanelInternal::kStickRing ) );
        pDraw->AddCircle( center, radius * deadzone, EditorColorUtil::toImU32( InputMapPanelInternal::kStickDeadzone ) );

        const ImVec2 dotPos = ImVec2( center.x + stickX * radius, center.y - stickY * radius );
        pDraw->AddCircleFilled( dotPos, 6.0f * EditorThemeUtil::getDPIScale(), EditorColorUtil::toImU32( InputMapPanelInternal::kStickDot ) );

        ImGui::Dummy( ImVec2( radius * 2.0f, radius * 2.0f ) );
        ImGui::Text( "X: %+.2f  Y: %+.2f", static_cast<float64>( stickX ), static_cast<float64>( stickY ) );
        ImGui::EndGroup();
    }

    void InputMapPanel::drawInputGraphTab()
    {
        ImGui::Text( "Real-Time Input Graphs (Last 120 Frames)" );
        ImGui::SameLine( 450.0f );
        bool bPaused = ( _bPlotPaused == SW_TRUE );
        if ( ImGui::Checkbox( "Pause Graph", &bPaused ) )
            _bPlotPaused = bPaused ? SW_TRUE : SW_FALSE;
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "PauseGraphTip" ) );

        ImGui::Separator();

        ImGui::Text( "Gamepad Left Stick (X/Y):" );
        ImGui::PlotLines( "Stick X", _arrPlotLeftStickX, static_cast<int32>( kPlotSampleCount ), static_cast<int32>( _plotOffset ), "X [-1.0 ~ +1.0]", -1.0f, 1.0f, measureExtent( EditorTextExtent::ofText( 0.0f, 3.5f ) ) );
        ImGui::PlotLines( "Stick Y", _arrPlotLeftStickY, static_cast<int32>( kPlotSampleCount ), static_cast<int32>( _plotOffset ), "Y [-1.0 ~ +1.0]", -1.0f, 1.0f, measureExtent( EditorTextExtent::ofText( 0.0f, 3.5f ) ) );

        ImGui::Separator();
        ImGui::Text( "Analog Triggers (LT/RT):" );
        ImGui::PlotLines( "LT", _arrPlotTriggerL, static_cast<int32>( kPlotSampleCount ), static_cast<int32>( _plotOffset ), "Left [0.0 ~ 1.0]", 0.0f, 1.0f, measureExtent( EditorTextExtent::ofText( 0.0f, 3.0f ) ) );
        ImGui::PlotLines( "RT", _arrPlotTriggerR, static_cast<int32>( kPlotSampleCount ), static_cast<int32>( _plotOffset ), "Right [0.0 ~ 1.0]", 0.0f, 1.0f, measureExtent( EditorTextExtent::ofText( 0.0f, 3.0f ) ) );

        ImGui::Separator();
        ImGui::Text( "Mouse Delta Speed (dX/dY):" );
        ImGui::PlotLines( "dX", _arrPlotMouseDeltaX, static_cast<int32>( kPlotSampleCount ), static_cast<int32>( _plotOffset ), "Delta X", -100.0f, 100.0f, measureExtent( EditorTextExtent::ofText( 0.0f, 3.0f ) ) );
        ImGui::PlotLines( "dY", _arrPlotMouseDeltaY, static_cast<int32>( kPlotSampleCount ), static_cast<int32>( _plotOffset ), "Delta Y", -100.0f, 100.0f, measureExtent( EditorTextExtent::ofText( 0.0f, 3.0f ) ) );
    }

    void InputMapPanel::drawInputSimulatorTab()
    {
        InputManager& input = editor::getSubsystem<InputManager>();

        ImGui::Text( "Virtual Input Injector & Gameplay Macro Simulator" );
        ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorInputMap", "InjectorHint" ) );
        ImGui::Separator();

        ImGui::Text( "1) Key Event Injector:" );
        const utf8* arrCommonKeys[] = { "Space", "Enter", "Escape", "W", "A", "S", "D", "E", "F", "Shift", "Control" };
        const Key   arrKeyValues[]  = { Key::Space, Key::Enter, Key::Escape, Key::W, Key::A, Key::S, Key::D, Key::E, Key::F, Key::LeftShift, Key::LeftControl };

        EditorChrome::setNextItemWidthColumns( 20.0f );
        ImGui::Combo( "Key", &_simKeyToInject, arrCommonKeys, 11 );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Inject KeyDown" ) );
        if ( ImGui::Button( "Inject KeyDown" ) )
            input.postRawEvent( RawInputEvent::makeKeyDown( arrKeyValues[_simKeyToInject] ) );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "InjectKeyDownTip" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Inject KeyUp" ) );
        if ( ImGui::Button( "Inject KeyUp" ) )
            input.postRawEvent( RawInputEvent::makeKeyUp( arrKeyValues[_simKeyToInject] ) );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "InjectKeyUpTip" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Tap Key (Down + Up)" ) );
        if ( ImGui::Button( "Tap Key (Down + Up)" ) )
        {
            input.postRawEvent( RawInputEvent::makeKeyDown( arrKeyValues[_simKeyToInject] ) );
            input.postRawEvent( RawInputEvent::makeKeyUp( arrKeyValues[_simKeyToInject] ) );
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "TapKeyTip" ) );

        ImGui::Separator();
        ImGui::Text( "2) Virtual Stick 2D Slider:" );
        ImGui::SliderFloat( "Sim Stick X", &_simStick._x, -1.0f, 1.0f );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "SimStickXTip" ) );
        ImGui::SliderFloat( "Sim Stick Y", &_simStick._y, -1.0f, 1.0f );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "SimStickYTip" ) );
        if ( ImGui::Button( "Inject Stick Tilt" ) )
        {
            GamepadDevice* pGamepad = input.getGamepad( 0 );
            if ( pGamepad != nullptr )
            {
                pGamepad->setAxis( 0, _simStick._x );
                pGamepad->setAxis( 1, _simStick._y );
            }
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "InjectStickTip" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Reset Stick to Center" ) );
        if ( ImGui::Button( "Reset Stick to Center" ) )
        {
            _simStick._x            = 0.0f;
            _simStick._y            = 0.0f;
            GamepadDevice* pGamepad = input.getGamepad( 0 );
            if ( pGamepad != nullptr )
            {
                pGamepad->setAxis( 0, 0.0f );
                pGamepad->setAxis( 1, 0.0f );
            }
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "ResetStickTip" ) );
    }

    void InputMapPanel::drawInputReplayTab()
    {
        InputManager& input = editor::getSubsystem<InputManager>();

        ImGui::Text( "Input Replay Recorder & Deterministic QA Playback" );
        ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorInputMap", "ReplayHint" ) );
        ImGui::Separator();

        (void)_replayFilePath.drawField( "##ReplayFile", 260.0f ); // 확정한 경로만 Save · Load 가 쓴다
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "ReplayFileTip" ) );

        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Save Replay" ) );
        if ( ImGui::Button( "Save Replay" ) )
        {
            const string diskPath = AssetPaths::resolveWritePath( _replayFilePath.get() ); // 파일은 디스크 경로로만 열린다 — 리소스 id 를 작업 디렉터리 상대로 쓰지 않는다
            if ( diskPath.empty() || _replay.saveToFile( diskPath ) == false )
                SW_LOG_ERROR( "Could not save input replay '%#'", _replayFilePath.c_str() );
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "SaveReplayTip" ) );

        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Load Replay" ) );
        if ( ImGui::Button( "Load Replay" ) )
        {
            if ( input.getVirtualInput() == &_replay )
                input.detachVirtualInput();
            const string diskPath = AssetPaths::findLooseFile( _replayFilePath.get() ).getDiskPath();
            if ( diskPath.empty() )
                SW_LOG_ERROR( "Could not load input replay '%#': no such file on disk", _replayFilePath.c_str() );
            else if ( _replay.loadFromFile( diskPath ) == false )
                SW_LOG_ERROR( "Could not load input replay '%#' from '%#'", _replayFilePath.c_str(), diskPath.c_str() );
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "LoadReplayTip" ) );

        ImGui::Separator();

        const bool bPlaying = input.getVirtualInput() == &_replay;
        drawRecordControls( input, bPlaying );

        ImGui::Separator();

        // 재생 제어. 재생은 리플레이를 붙이는 것이고, 일시정지는 그 프레임으로 seekTo 하는 것이다(상태를 재현한 뒤 분리한다). 탐색도 seekTo 다.
        ImGui::Text( "Playback Controls (Total Frames: %u, Duration: %.2f s):", _replay.getFrameCount(), static_cast<float64>( _replay.getTotalDuration() ) );
        const uint32 currentFrame = _replay.getStartFrameIndex() + ( bPlaying ? input.getVirtualFrameIndex() : 0u );
        drawPlaybackControls( input, bPlaying, currentFrame );

        if ( currentFrame < _replay.getFrameCount() )
        {
            const InputReplayFrame&               frame = _replay.getFrames()[currentFrame];
            fixed_string<constant::kMaxBuffer128> frameBuf;
            formatstring( frameBuf.data(), frameBuf.capacity(), "Frame #%u | DeltaTime: %#s | Events: %d", currentFrame,
                          Fmt( static_cast<float64>( frame._deltaTime ), Format().precision( 4 ) ), static_cast<int32>( frame._listRawEvent.size() ) );
            EditorThemeUtil::textInfo( frameBuf.c_str() );
        }
    }

    void InputMapPanel::drawRecordControls( InputManager& input, bool bPlaying )
    {
        // 녹화 제어
        if ( _replay.isRecording() )
        {
            EditorThemeUtil::pushTextColor( EditorThemeUtil::getErrorColor() );
            ImGui::Text( "● RECORDING LIVE INPUTS... (Frames: %u)", _replay.getFrameCount() );
            EditorThemeUtil::popTextColor();
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "■ Stop Recording" ) );
            if ( ImGui::Button( "■ Stop Recording" ) )
                _replay.stopRecording();
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "StopRecordingTip" ) );
        }
        else if ( bPlaying == false )
        {
            if ( ImGui::Button( "● Start Recording" ) )
            {
                _replay.startRecording( "GameplaySession" );
                _recordedBeginFrameCount = input.getBeginFrameCount();
            }
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "StartRecordingTip" ) );
        }
    }

    void InputMapPanel::drawPlaybackControls( InputManager& input, bool bPlaying, uint32 currentFrame )
    {
        if ( bPlaying )
        {
            if ( ImGui::Button( "⏸ Pause" ) )
                _replay.seekTo( input, currentFrame );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "PauseReplayTip" ) );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "⏹ Stop" ) );
            if ( ImGui::Button( "⏹ Stop" ) )
                _replay.seekTo( input, 0 );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "StopReplayTip" ) );
        }
        else if ( _replay.isRecording() == false && _replay.getFrameCount() > 0 )
        {
            if ( ImGui::Button( "▶ Play Replay" ) )
            {
                if ( currentFrame >= _replay.getFrameCount() )
                    _replay.seekTo( input, 0 );
                input.attachVirtualInput( &_replay, VirtualInputMode::Exclusive, false );
            }
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "PlayReplayTip" ) );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "⏮ Step Back" ) );
            if ( ImGui::Button( "⏮ Step Back" ) && currentFrame > 0 )
                _replay.seekTo( input, currentFrame - 1 );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "StepBackTip" ) );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "⏭ Step Forward (1 Frame)" ) );
            if ( ImGui::Button( "⏭ Step Forward (1 Frame)" ) )
                _replay.seekTo( input, currentFrame + 1 );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "StepForwardTip" ) );

            int32       frameIdx  = static_cast<int32>( currentFrame );
            const int32 maxFrames = static_cast<int32>( _replay.getFrameCount() );
            if ( ImGui::SliderInt( "Timeline Frame", &frameIdx, 0, maxFrames ) )
                _replay.seekTo( input, static_cast<uint32>( frameIdx ) );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "TimelineFrameTip" ) );
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
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "OverlayEnableTip" ) );

        ImGui::Separator();

        const utf8* arrPos[] = { "Bottom-Right", "Bottom-Left", "Top-Right", "Top-Left" };
        int32       posIndex = static_cast<int32>( config._position );
        if ( ImGui::Combo( "Screen Anchor Position", &posIndex, arrPos, 4 ) )
            config._position = static_cast<ViewportOverlayPosition>( posIndex );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "OverlayAnchorTip" ) );

        ImGui::SliderFloat( "HUD Opacity", &config._opacity, 0.1f, 1.0f );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "OverlayOpacityTip" ) );
        ImGui::SliderFloat( "HUD Scale", &config._scale, 0.5f, 2.0f );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "OverlayScaleTip" ) );

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
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "OverlayHistoryTip" ) );
    }

    void InputMapPanel::reloadFromFile()
    {
        // 읽지 못하면 편집 중인 바인딩과 dirty 를 그대로 둔다. dirty 를 지우고 "다시 읽었다" 고 표시하지 않는다.
        if ( _inputMap.loadFromAsset( AssetID{ _inputMapPath.c_str() } ) == false )
        {
            SW_LOG_ERROR( "Could not reload InputMap from %# - keeping the edited bindings", _inputMapPath.c_str() );
            return;
        }
        clearDocumentDirty();
        SW_LOG_INFO( "Reloaded InputMap from %#", _inputMapPath.c_str() );
    }

    bool InputMapPanel::saveToFile()
    {
        // 패널은 리소스의 기본 바인딩(`<InputMap>`)을 편집한다. 다시 읽기(`reloadFromFile`)와 같은 형식, 같은 위치에 쓴다.
        // 플레이어 리매핑은 UserSettings 의 `keyBinding` 설정이 사용자 파일에 보관한다.
        if ( _inputMap.saveToAsset( _inputMapPath.c_str() ) == false )
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
