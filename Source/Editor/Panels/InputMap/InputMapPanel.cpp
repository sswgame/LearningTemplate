#include "pch.h"

#include "Editor/Panels/InputMap/InputMapPanel.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Backend/ImGuiKeyForward.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Widgets/Model/EditorListFilter.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"
#include "Editor/Viewport/ViewportInputOverlay.h"

#include "Engine/Input/Devices/GamepadDevice.h"
#include "Engine/Input/Devices/KeyboardDevice.h"
#include "Engine/Input/Devices/MouseDevice.h"
#include "Engine/Input/GamepadButtonUtil.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/KeyCodeUtil.h"
#include "Engine/Input/Map/InputGlyphResolver.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Input/Virtual/InputReplay.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Reflection/EnumName.h"

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

    InputMapPanel::InputMapPanel()
        : IEditorDocumentPanel( false ) // 필요할 때 여는 도구이므로 닫힌 채 시작한다. 열린 채 시작하면 떠 있는 창이 화면 가운데를 덮는다
        , _document{}
        , _newActionName{ "" }
        , _newLayerName{ "" }
        , _arrActionFilter{}
        , _listVisibleAction{}
        , _listActionConflictView{}
        , _testComboPattern{ "236P" }
        , _testVibLeft{ 0.5f }
        , _testVibRight{ 0.5f }
        , _simStick{}
        , _newActionValueType{ 0 }
        , _newLayerPriority{ 0 }
        , _simKeyToInject{ 1 }
        , _bSelectFirstTab{ SW_TRUE }
        , _reserved{ 0 }
    {
    }

    void InputMapPanel::drawContent()
    {
        if ( _document.needsFirstLoad() )
            reloadFromFile();
        _document.tick( InputMapModel::getInputManager(), ImGui::GetIO().DeltaTime );

        // 탭은 셋이다. 편집(Mappings), 들여다보기(Debug), 녹화와 주입(Replay & Inject)이다. 언리얼의 Enhanced Input 은 액션, 매핑, 트리거를 한 화면에서 편집한다.
        if ( ImGui::BeginTabBar( "InputEditorTabs" ) )
        {
            const ImGuiTabItemFlags mappingsFlags = _bSelectFirstTab ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
            _bSelectFirstTab                      = SW_FALSE;
            if ( ImGui::BeginTabItem( SW_LOCKEY( "EdSection", "Mappings" ), nullptr, mappingsFlags ) )
            {
                drawMappingsTab();
                ImGui::EndTabItem();
            }
            const bool bDebugTab = ImGui::BeginTabItem( SW_LOCKEY( "EdSection", "Debug" ) );
            EditorSelfTestMarks::record( "inputMap.tab.debug" );
            if ( bDebugTab )
            {
                drawDebugTab();
                ImGui::EndTabItem();
            }
            if ( ImGui::BeginTabItem( SW_LOCKEY( "EdSection", "ReplayInject" ) ) )
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

    void InputMapPanel::onPanelCollapsed()
    {
        _document.tick( InputMapModel::getInputManager(), ImGui::GetIO().DeltaTime );
    }

    void InputMapPanel::drawMappingsTab()
    {
        ImGui::TextUnformatted( SW_LOCKEY( "EdText", "InputMap" ) );
        ImGui::SameLine();
        (void)_document.editInputMapPath().drawField( "##InputMapPath", 260.0f ); // 확정한 경로는 Reload · Save 가 쓴다 — 바꾸기만 해서는 다시 읽지 않는다
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "MapPathTip" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Reload" ) ) ); // 경로 칸이 고정 폭이라 좁은 패널에서는 단추가 창 밖에 놓이므로 줄을 바꾼다
        if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Reload" ) ) )
            reloadFromFile();
        EditorSelfTestMarks::record( "inputMap.reload" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "ReloadTip" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Save" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Save" ) ) )
            (void)saveToFile(); // 실패는 saveToFile 이 알리고 dirty 가 남는다
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "SaveTip" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "RevertAllToDefault" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "RevertAllToDefault" ) ) )
            _document.resetAllBindingsToDefault();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "RevertAllTip" ) );
        if ( isDocumentDirty() )
        {
            ImGui::SameLine();
            EditorThemeUtil::textWarning( SW_LOCKEY( "EditorInputMap", "UnsavedChanges" ) );
        }
        _document.refreshConflicts();
        const vector<InputMapConflict>& listConflict = _document.getConflicts();
        if ( listConflict.empty() == false )
        {
            ImGui::SameLine();
            const string conflictText = SW_LOCKEYFORMAT( "EditorInputMap", "ConflictCount",
                                                         TextArgumentList().addInteger( "count", static_cast<int64>( listConflict.size() ) ) );
            EditorThemeUtil::textError( conflictText.c_str() );
        }
        drawBundleFiles();
        ImGui::Separator();

        const float32 availWidth = ImGui::GetContentRegionAvail().x;
        const float32 listWidth  = MathUtil::max( availWidth * 0.42f, EditorChrome::measureTextExtent( EditorTextExtent::ofText( 27.0f, 0.0f ) ).x );
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

    void InputMapPanel::drawBundleFiles()
    {
        const InputMap& inputMap = _document.getInputMap();
        if ( _document.isBundlePath() == false || inputMap.getSourceCount() == 0 )
            return;
        const bool bOpen = ImGui::CollapsingHeader( SW_LOCKEY( "EditorInputMap", "BundleFiles" ) );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "BundleFilesTip" ) );
        if ( bOpen == false )
            return;
        for ( uint32 sourceIndex = 0; sourceIndex < inputMap.getSourceCount(); ++sourceIndex )
        {
            ImGui::TextUnformatted( inputMap.getSourceAssetPath( sourceIndex ).c_str() );
        }
    }

    void InputMapPanel::drawActionList()
    {
        ImGui::SeparatorText( SW_LOCKEY( "EdSection", "Actions" ) );
        EditorWidgets::drawSearchField( "##inputMapActionFilter", _arrActionFilter, constant::buffer::kMax64, SW_LOCKEY( "EditorInputMap", "FilterActionsHint" ), 0.0f, true, "inputMap.filter" );
        const EditorListFilter filter{ string_view( _arrActionFilter ) };
        const InputMap&        inputMap = _document.getInputMap();
        _document.collectVisibleActions( filter, _listVisibleAction );
        if ( _listVisibleAction.empty() )
        {
            if ( filter.isActive() )
                EditorWidgets::drawNoSearchResultHint( filter.getText() );
            else
                EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorInputMap", "NoActionsHint" ) );
        }
        for ( const hashed_string& actionName : _listVisibleAction )
        {
            const bool bConflict = InputMapConflict::involves( _document.getConflicts(), actionName );
            const bool bDown     = inputMap.isActionDown( actionName );
            ImGui::PushID( actionName.c_str() );
            if ( bConflict )
                EditorThemeUtil::pushTextColor( SW_THEME_COLOR( "Signal", "Error" ) );
            if ( ImGui::Selectable( actionName.c_str(), _document.getSelectedAction() == actionName.view() ) )
                _document.selectAction( actionName.view() );
            if ( bConflict )
                EditorThemeUtil::popTextColor();
            if ( EditorSelfTestMarks::isEnabled() )
                EditorSelfTestMarks::record( ( string{ "inputMap.action." } + actionName.c_str() ).c_str() );
            if ( bConflict )
                EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "ActionConflictTip" ) );
            // 키 글리프는 오른쪽 끝에 맞춘다 — 고정 폭으로 비워 두면 "[ LeftControl+... ]" 같은 긴 글리프가 잘린다.
            const string glyphText = bDown ? string{ "DOWN" } : InputGlyphResolver::findActionGlyph( inputMap, actionName );
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
        const string& selectedAction = _document.getSelectedAction();
        if ( selectedAction.empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorInputMap", "SelectActionHint" ) );
            return;
        }
        const InputMap&     inputMap = _document.getInputMap();
        const hashed_string action( selectedAction.c_str() );
        ImGui::SeparatorText( selectedAction.c_str() );
        // 묶음을 고칠 때는 저장이 어느 파일로 가는지 보인다 — 공통 파일을 고치면 다른 게임도 바뀐다.
        if ( _document.isBundlePath() )
        {
            const string sourceText = _document.describeActionSource( action );
            ImGui::TextDisabled( "%s", sourceText.c_str() );
        }
        ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "StateHoldS", TextArgumentList().addText( "phase", _document.describeActionState( action ) ).addDecimal( "holdSeconds", static_cast<float64>( inputMap.getActionHoldDuration( action ) ), 2 ) ).c_str() );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdLabel", "ResetToDefault" ) ) );
        if ( ImGui::SmallButton( SW_LOCKEY( "EdLabel", "ResetToDefault" ) ) )
            (void)_document.resetActionToDefault( action );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "ResetActionTip" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EditorInputMap", "DeleteAction" ) ) );
        const bool bDeleteAction = ImGui::SmallButton( SW_LOCKEY( "EditorInputMap", "DeleteAction" ) );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "DeleteActionTip" ) );

        // 바인딩마다 키(글리프), 레이어, 발화 규칙(트리거), 다시 잡기 · 지우기 버튼을 둔다.
        ImGui::SeparatorText( SW_LOCKEY( "EdSection", "Bindings" ) );
        const uint32 bindingCount = inputMap.getBindingCount( action );
        int32        removeIndex  = -1;
        for ( uint32 bindIndex = 0; bindIndex < bindingCount; ++bindIndex )
        {
            const ActionBinding* pBinding = inputMap.getBinding( action, bindIndex );
            if ( pBinding == nullptr )
                continue;
            ImGui::PushID( static_cast<int32>( bindIndex ) );
            if ( drawBindingRow( action, bindIndex, *pBinding ) )
                removeIndex = static_cast<int32>( bindIndex );
            ImGui::PopID();
        }
        if ( bindingCount == 0 )
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorInputMap", "NoBindings" ) );
        if ( ImGui::SmallButton( SW_LOCKEY( "EditorInputMap", "AddBinding" ) ) )
            _document.beginNewBindingCapture();
        EditorSelfTestMarks::record( "inputMap.addBinding" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "AddBindingTip" ) );

        drawActionConflicts( action );
        drawPromptGlyphs( action );

        // 그리는 동안 바인딩 목록과 고른 액션(`selectedAction` 은 그 참조다)을 건드리지 않는다.
        if ( removeIndex >= 0 )
            (void)_document.removeBinding( action, static_cast<uint32>( removeIndex ) );
        if ( bDeleteAction )
            (void)_document.deleteAction( action );
    }

    bool InputMapPanel::drawBindingRow( const hashed_string& action, uint32 bindIndex, const ActionBinding& binding )
    {
        const float32   rightEdgeX = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x; // 이 줄이 그릴 수 있는 오른쪽 끝(화면 좌표)
        InputSlot       slot{};
        const InputMap& inputMap    = _document.getInputMap();
        const bool      bRebindable = inputMap.findRebindSlot( action, bindIndex, slot );
        ImGui::Text( "%u  %s", bindIndex, _document.describeBinding( action, bindIndex ).c_str() );
        ImGui::SameLine( ImGui::GetFontSize() * SW_THEME_METRIC( "Layout", "InputMapLayerColumnEm" ) );
        drawBindingLayerCombo( action, bindIndex );
        const bool bTriggerFields = InputMapModel::savesTriggerFields( binding._kind ); // 파일에 남지 않는 칸은 보이지 않는다
        if ( bTriggerFields )
        {
            ImGui::SameLine( ImGui::GetFontSize() * SW_THEME_METRIC( "Layout", "InputMapActionColumnEm" ) );
            drawTriggerCombo( action, bindIndex );
        }
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "Rebind" ) ) );
        ImGui::BeginDisabled( bRebindable == false );
        if ( ImGui::SmallButton( SW_LOCKEY( "EdText", "Rebind" ) ) )
            _document.beginKeyCapture( bindIndex );
        ImGui::EndDisabled();
        if ( bRebindable == false )
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "NotRebindableTip" ) );
        else
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "RebindTip" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Remove" ) ) );
        const bool bRemove = ImGui::SmallButton( SW_LOCKEY( "EdVerb", "Remove" ) );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "RemoveBindingTip" ) );
        if ( bTriggerFields )
            drawTriggerOptions( action, bindIndex, binding, rightEdgeX );
        return bRemove;
    }

    void InputMapPanel::drawBindingLayerCombo( const hashed_string& action, uint32 bindIndex )
    {
        const InputMap&     inputMap = _document.getInputMap();
        const hashed_string current  = inputMap.findBindingLayer( action, bindIndex );
        // 레이어 열과 다음 열 사이를 채운다. 다음 열 앞에 한 칸을 띄운다.
        ImGui::SetNextItemWidth( ImGui::GetFontSize() * ( SW_THEME_METRIC( "Layout", "InputMapActionColumnEm" ) - SW_THEME_METRIC( "Layout", "InputMapLayerColumnEm" ) - 0.5f ) );
        const bool bOpen = ImGui::BeginCombo( "##layer", current.c_str() );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "BindingLayerTip" ) );
        if ( bOpen == false )
            return;
        for ( const hashed_string& layer : inputMap.getLayerNames() )
        {
            if ( ImGui::Selectable( layer.c_str(), layer == current ) && layer != current )
                (void)_document.setBindingLayer( action, bindIndex, layer );
        }
        ImGui::EndCombo();
    }

    void InputMapPanel::drawTriggerCombo( const hashed_string& action, uint32 bindIndex )
    {
        // 가장 긴 트리거 이름(HoldThreshold · HoldAndRelease)이 잘리지 않을 만큼 — 고정 8 em 이면 "HoldThresho" 로 끊긴다.
        ImGui::SetNextItemWidth( ImGui::CalcTextSize( "HoldAndRelease" ).x + ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 2.0f );
        const InputMap&     inputMap = _document.getInputMap();
        const ActionTrigger trigger  = inputMap.getBindingTrigger( action, bindIndex );
        const utf8*         pCurrent = getEnumName( trigger );
        const bool          bOpen    = ImGui::BeginCombo( "##trigger", pCurrent != nullptr ? pCurrent : "?" );
        if ( EditorSelfTestMarks::isEnabled() )
        {
            fixed_string<constant::buffer::kMax64> text2;
            formatstring( text2.data(), text2.capacity(), "inputMap.binding.%#.trigger", static_cast<uint64>( bindIndex ) );
            EditorSelfTestMarks::record( text2.c_str() );
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "TriggerTip" ) );
        if ( bOpen == false )
            return;
        for ( uint32 option = 0; option < static_cast<uint32>( ActionTrigger::Count ); ++option )
        {
            const ActionTrigger value = static_cast<ActionTrigger>( option );
            if ( ImGui::Selectable( getEnumName( value ), value == trigger ) )
                (void)_document.setBindingTrigger( action, bindIndex, value );
            if ( EditorSelfTestMarks::isEnabled() )
            {
                fixed_string<constant::buffer::kMax64> text;
                formatstring( text.data(), text.capacity(), "inputMap.trigger.%#", static_cast<uint64>( option ) );
                EditorSelfTestMarks::record( text.c_str() );
            }
        }
        ImGui::EndCombo();
    }

    void InputMapPanel::drawTriggerOptions( const hashed_string& action, uint32 bindIndex, const ActionBinding& binding, float32 rightEdgeX )
    {
        // 바인딩마다 발화 시간(초)과 값 부호 뒤집기. 시간을 쓰지 않는 트리거(Pressed 등)면 시간 필드는 숨긴다.
        const InputMap& inputMap      = _document.getInputMap();
        const float32   effectiveTime = inputMap.getEffectiveTriggerTime( binding );
        const bool      bUsesTime     = effectiveTime > 0.0f;
        const bool      bUsesTaps     = binding._trigger == ActionTrigger::MultiTap;
        // 시간 · 횟수 필드가 필요하거나 Negate 가 남은 폭에 안 들어가면 패널 오른쪽 밖으로 잘린다 — 콤보와 같은 열 위치에서 시작하는 다음 줄로 내린다.
        const float32 negateWidth       = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize( "Negate" ).x;
        const bool    bNegateFits       = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + negateWidth <= rightEdgeX;
        bool          bLineStart        = false;
        const bool    bWrapToSecondLine = bUsesTime || bUsesTaps || bNegateFits == false;
        if ( bWrapToSecondLine )
        {
            ImGui::SetCursorPosX( ImGui::GetFontSize() * SW_THEME_METRIC( "Layout", "InputMapActionColumnEm" ) );
            bLineStart = true;
        }
        if ( bUsesTime )
        {
            float32 editTime = effectiveTime;
            if ( bLineStart == false )
                ImGui::SameLine();
            bLineStart = false;
            ImGui::SetNextItemWidth( ImGui::GetFontSize() * SW_THEME_METRIC( "Layout", "FieldNumberEm" ) );
            if ( ImGui::DragFloat( "##time", &editTime, 0.01f, 0.01f, 10.0f, "%.2f s" ) )
                (void)_document.setBindingTriggerTime( action, bindIndex, editTime );
        }
        // MultiTap 만 누름 횟수를 고른다(2 이상).
        if ( bUsesTaps )
        {
            int32 editTaps = static_cast<int32>( binding._tapCount );
            if ( bLineStart == false )
                ImGui::SameLine();
            bLineStart = false;
            ImGui::SetNextItemWidth( ImGui::GetFontSize() * SW_THEME_METRIC( "Layout", "FieldTinyEm" ) );
            if ( ImGui::DragInt( "##taps", &editTaps, 0.1f, 2, 9, "%d taps" ) )
                (void)_document.setBindingTapCount( action, bindIndex, static_cast<uint8>( MathUtil::clamp( editTaps, 2, 9 ) ) );
        }
        bool bNegate = binding._bNegate;
        if ( bLineStart == false )
            EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( SW_LOCKEY( "EdLabel", "Negate" ) ) );
        if ( ImGui::Checkbox( SW_LOCKEY( "EdLabel", "Negate" ), &bNegate ) )
            (void)_document.setBindingNegate( action, bindIndex, bNegate );
    }

    void InputMapPanel::drawActionConflicts( const hashed_string& action )
    {
        // 충돌은 같은 레이어에서 한 입력에 함께 발화하는 다른 액션이다. 이쪽을 다시 잡거나 저쪽 바인딩을 지운다.
        InputMapConflict::collectFor( _document.getConflicts(), action, _listActionConflictView );
        if ( _listActionConflictView.empty() )
            return;
        ImGui::SeparatorText( SW_LOCKEY( "EdSection", "Conflicts" ) );
        for ( size_t index = 0; index < _listActionConflictView.size(); ++index )
        {
            const InputMapConflictView& view = _listActionConflictView[index];
            ImGui::PushID( static_cast<int32>( index ) );
            fixed_string<constant::buffer::kMax256> conflictText;
            formatstring( conflictText.data(), conflictText.capacity(), "%# on %# (layer %#)", InputGlyphResolver::makeSlotGlyph( view._slot, InputGlyphStyle::KeyboardMouse ).c_str(),
                          view._otherAction.c_str(), view._layer.c_str() );
            EditorThemeUtil::textError( conflictText.c_str() );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "RebindThis" ) ) );
            if ( ImGui::SmallButton( SW_LOCKEY( "EdText", "RebindThis" ) ) )
                _document.beginKeyCapture( view._bindIndex );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "RebindThisTip" ) );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "UnbindOther" ) ) );
            if ( ImGui::SmallButton( SW_LOCKEY( "EdText", "UnbindOther" ) ) )
                (void)_document.removeBinding( view._otherAction, view._otherBindIndex ); // 한 프레임에 눌리는 단추는 하나다. 번호가 당겨진 충돌 표는 다음 프레임에 다시 모은다
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "UnbindOtherTip" ) );
            ImGui::PopID();
        }
    }

    void InputMapPanel::drawPromptGlyphs( const hashed_string& action )
    {
        // 플랫폼 글리프는 화면에 표시할 버튼 아이콘이다. 언리얼 CommonUI 의 입력 표시에 해당한다.
        ImGui::SeparatorText( SW_LOCKEY( "EdSection", "PromptGlyphs" ) );
        static constexpr InputGlyphStyle kArrStyle[]     = { InputGlyphStyle::KeyboardMouse, InputGlyphStyle::GamepadXbox, InputGlyphStyle::GamepadPlayStation, InputGlyphStyle::GamepadSwitch };
        static constexpr const utf8*     kArrStyleName[] = { "Keyboard / Mouse", "Xbox", "PlayStation", "Switch" };
        for ( size_t index = 0; index < 4; ++index )
        {
            ImGui::TextDisabled( "%s", kArrStyleName[index] );
            ImGui::SameLine( ImGui::CalcTextSize( kArrStyleName[0] ).x + ImGui::GetStyle().ItemSpacing.x * 2.0f ); // 가장 긴 이름 뒤에 맞춘다(고정 폭이면 글꼴에 따라 겹친다)
            ImGui::TextUnformatted( InputGlyphResolver::findActionGlyph( _document.getInputMap(), action, kArrStyle[index] ).c_str() );
        }
    }

    void InputMapPanel::drawDebugTab()
    {
        if ( ImGui::CollapsingHeader( SW_LOCKEY( "EdSection", "Devices" ), ImGuiTreeNodeFlags_DefaultOpen ) )
            drawDeviceMonitorTab();
        if ( ImGui::CollapsingHeader( SW_LOCKEY( "EdSection", "Graphs" ) ) )
            drawInputGraphTab();
        if ( ImGui::CollapsingHeader( SW_LOCKEY( "EdSection", "ViewportHUD" ) ) )
            drawViewportOverlayTab();
        if ( ImGui::CollapsingHeader( SW_LOCKEY( "EdSection", "CommandPatternTester" ) ) )
            drawCommandPatternTester();
    }

    void InputMapPanel::drawCommandPatternTester()
    {
        EditorWidgets::drawTextField( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdWidget", "PatternNumpadNotation" ), "Pattern (numpad notation)" ), _testComboPattern, 150.0f );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "PatternTip" ) );
        ImGui::SameLine();
        if ( _document.getInputMap().getActionBuffer().wasCommandPatternTriggered( sw::hashed_string( _testComboPattern.c_str() ), 0.8f ) )
            EditorThemeUtil::textSuccess( "MATCHED" );
        else
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorInputMap", "WaitingForInput" ) );
        ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorInputMap", "PatternExample" ) );
    }

    void InputMapPanel::drawLayerList()
    {
        const InputMap&              inputMap  = _document.getInputMap();
        const vector<hashed_string>& listLayer = inputMap.getLayerNames();

        if ( ImGui::CollapsingHeader( SW_LOCKEY( "EdSection", "InputLayers" ) ) )
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
                    bool bEnabled = inputMap.isLayerEnabled( layerName );
                    if ( ImGui::Checkbox( "##Enabled", &bEnabled ) )
                    {
                        _document.setLayerEnabled( sw::hashed_string( layerName.view() ), bEnabled );
                    }
                    EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "LayerEnabledTip" ) );
                    ImGui::PopID();

                    ImGui::TableNextColumn();
                    ImGui::Text( "%d", inputMap.getLayerPriority( layerName ) );

                    ImGui::TableNextColumn();
                    if ( inputMap.getCurrentTopLayer() == layerName.view() )
                        EditorThemeUtil::textSuccess( "Top (Active)" );
                    else if ( bEnabled )
                        ImGui::Text( "Active" );
                    else
                        ImGui::TextDisabled( "%s", SW_LOCKEY( "EdText", "Disabled" ) );
                }
                ImGui::EndTable();
            }
        }
    }

    void InputMapPanel::drawAddActionSection()
    {
        if ( ImGui::CollapsingHeader( SW_LOCKEY( "EdSection", "AddNewActionLayer" ) ) )
        {
            EditorWidgets::drawTextField( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdWidget", "NewActionName" ), "New Action Name" ), _newActionName, 200.0f );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "NewActionNameTip" ) );

            ImGui::SameLine();
            const utf8* arrTypes[] = { "Boolean", "Axis 1D", "Vector 2D" };
            EditorWidgets::drawInlineLabel( SW_LOCKEY( "EdField", "Type" ) );
            EditorChrome::setNextItemWidthColumns( 16.0f );
            ImGui::Combo( "##Type", &_newActionValueType, arrTypes, 3 );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "NewActionTypeTip" ) );

            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "CreateAction" ) ) );
            ImGui::BeginDisabled( _document.canCreateAction( _newActionName ) == false ); // 빈 이름 · 있는 이름은 만들 수 없다 — 눌러도 아무 일이 없는 단추로 두지 않는다
            if ( ImGui::Button( SW_LOCKEY( "EdText", "CreateAction" ) ) && _document.createAction( _newActionName, static_cast<InputActionValueType>( _newActionValueType ) ) )
            {
                _newActionName = "";
            }
            ImGui::EndDisabled();
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "CreateActionTip" ) );

            EditorWidgets::drawTextField( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdWidget", "NewLayerName" ), "New Layer Name" ), _newLayerName, 200.0f );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "NewLayerNameTip" ) );
            ImGui::SameLine();
            EditorWidgets::drawInlineLabel( SW_LOCKEY( "EdField", "Priority" ) );
            EditorChrome::setNextItemWidthColumns( 8.0f );
            ImGui::InputInt( "##LayerPriority", &_newLayerPriority );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "CreateLayer" ) ) );
            ImGui::BeginDisabled( _document.canCreateLayer( _newLayerName ) == false );
            if ( ImGui::Button( SW_LOCKEY( "EdText", "CreateLayer" ) ) && _document.createLayer( _newLayerName, _newLayerPriority ) )
            {
                _newLayerName = "";
            }
            ImGui::EndDisabled();
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "CreateLayerTip" ) );
        }
    }

    void InputMapPanel::drawCaptureModal()
    {
        if ( _document.isCapturingKey() == false )
            return;

        EditorInputGUI::openPopup( "Press Key / Button To Bind" );
        if ( EditorWidgets::beginCenteredModal( "Press Key / Button To Bind" ) )
        {
            ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "BindingForAction", TextArgumentList().addText( "action", _document.getSelectedAction().c_str() ) ).c_str() );
            ImGui::TextUnformatted( SW_LOCKEY( "EditorInputMap", "CaptureInstruction" ) );
            ImGui::Separator();

            // 실시간 활성 입력 감지. 바인딩 창은 ImGui 모달이라 떠 있는 동안 ImGui 가 키보드를 가져가고(`WantCaptureKeyboard`),
            // 에디터는 그 키를 게임 입력으로 넘기지 않는다(`ImGuiEditor::processEvent`). 그래서 키는 창이 받은 곳(ImGui)에서 읽는다.
            // 판단(조합 · Esc · 수식 키 하나)은 문서가 한다. 패드 버튼은 게임 입력에 그대로 닿는다.
            InputMapCaptureFrame captureFrame{};
            (void)ImGuiKeyForward::findPressedChord( captureFrame._chordKey, captureFrame._modifierMask ); // 없으면 칸이 비어 있다
            captureFrame._pressedKey           = ImGuiKeyForward::findPressedKey();
            captureFrame._bPendingModifierDown = ImGuiKeyForward::isKeyDown( _document.getPendingCaptureModifier() );
            _document.advanceKeyCapture( captureFrame );
            _document.pollGamepadCapture( InputMapModel::getInputManager() );
            if ( _document.isCapturingKey() == false )
                ImGui::CloseCurrentPopup();

            // 키를 누를 수 없는 키(Esc · 창이 먼저 받는 키)는 버튼 목록으로 고른다
            ImGui::BeginChild( "KeyGrid", EditorChrome::measureTextExtent( EditorTextExtent::ofText( 60.0f, 11.0f ) ), true );
            for ( int32 keyIndex = 1; keyIndex < static_cast<int32>( Key::Count ); ++keyIndex )
            {
                const Key   key      = static_cast<Key>( keyIndex );
                const utf8* pKeyName = KeyCodeUtil::toName( key );
                if ( StringUtil::isNullOrEmpty( pKeyName ) == false )
                {
                    if ( ImGui::Button( pKeyName, EditorChrome::measureTextExtent( EditorTextExtent::ofText( 11.0f, 1.25f ) ) ) )

                    {
                        (void)_document.finishKeyCapture( key );
                        ImGui::CloseCurrentPopup();
                        break;
                    }
                    if ( ( keyIndex % 5 ) != 0 )
                        ImGui::SameLine();
                }
            }
            ImGui::EndChild();

            if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Cancel" ), EditorChrome::measureTextExtent( EditorTextExtent::ofText( 16.0f, 0.0f ) ) ) )
            {
                _document.cancelKeyCapture();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    void InputMapPanel::drawDeviceMonitorTab()
    {
        InputManager& input = InputMapModel::getInputManager();

        // 1) 활성 장치 상태
        const InputGlyphStyle devType   = input.getActiveGlyphStyle();
        const utf8*           pTypeName = InputMapPanelInternal::glyphStyleName( devType );

        ImGui::TextUnformatted( SW_LOCKEY( "EdText", "ActiveDevice" ) );

        ImGui::SameLine();
        EditorThemeUtil::textSuccess( pTypeName );
        ImGui::Separator();

        drawKeyboardMonitor();

        drawMouseMonitor();

        drawGamepadMonitor();
    }

    void InputMapPanel::drawKeyboardMonitor()
    {
        InputManager& input = InputMapModel::getInputManager();

        // 2) 키보드 실시간 모니터
        if ( ImGui::CollapsingHeader( SW_LOCKEY( "EdSection", "KeyboardStatus" ), ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            ImGui::TextUnformatted( SW_LOCKEY( "EdText", "HeldKeys" ) );
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
                        EditorThemeUtil::pushTextColor( SW_THEME_COLOR( "Signal", "Warning" ) );
                        ImGui::Text( "[%s]", pName );
                        EditorThemeUtil::popTextColor();
                        bAnyKey = true;
                    }
                }
            }
            if ( bAnyKey == false )
                ImGui::TextDisabled( "%s", SW_LOCKEY( "EdText", "None" ) );
        }
    }

    void InputMapPanel::drawMouseMonitor()
    {
        InputManager& input = InputMapModel::getInputManager();

        // 3) 마우스 실시간 모니터
        if ( ImGui::CollapsingHeader( SW_LOCKEY( "EdSection", "MouseStatus" ), ImGuiTreeNodeFlags_DefaultOpen ) )
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

            ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "Position", TextArgumentList().addInteger( "mx", static_cast<int64>( mx ) ).addInteger( "my", static_cast<int64>( my ) ) ).c_str() );
            ImGui::SameLine( 200.0f );
            ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "Delta", TextArgumentList().addInteger( "dx", static_cast<int64>( dx ) ).addInteger( "dy", static_cast<int64>( dy ) ) ).c_str() );
            ImGui::SameLine( 350.0f );
            ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "SmoothDelta", TextArgumentList().addDecimal( "smoothDx", static_cast<float64>( smoothDx ), 2 ).addDecimal( "smoothDy", static_cast<float64>( smoothDy ), 2 ) ).c_str() );

            ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "Wheel", TextArgumentList().addDecimal( "mouseWheel", static_cast<float64>( input.getMouseWheel() ), 2 ) ).c_str() );
            ImGui::SameLine( 200.0f );
            ImGui::TextUnformatted( SW_LOCKEY( "EdText", "Buttons" ) );
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
        InputManager& input = InputMapModel::getInputManager();

        // 4) 게임패드 실시간 모니터
        if ( ImGui::CollapsingHeader( SW_LOCKEY( "EdSection", "Gamepad0Status" ), ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            GamepadDevice* pGamepad = input.getGamepad( 0 );
            if ( pGamepad != nullptr && pGamepad->isConnected() )
            {
                const GamepadBatteryInfo batteryInfo = pGamepad->getBatteryInfo();
                ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "Battery", TextArgumentList().addText( "level", InputMapPanelInternal::batteryLevelName( batteryInfo._level ) ) ).c_str() );

                float32      lx = 0.0f, ly = 0.0f, rx = 0.0f, ry = 0.0f;
                const float2 vecLeftStick3  = pGamepad->getLeftStick();
                lx                          = vecLeftStick3._x;
                ly                          = vecLeftStick3._y;
                const float2 vecRightStick4 = pGamepad->getRightStick();
                rx                          = vecRightStick4._x;
                ry                          = vecRightStick4._y;

                drawGamepadStickVisualizer( SW_LOCKEY( "EdWidget", "LeftStick" ), lx, ly, 0.15f );
                ImGui::SameLine( 180.0f );
                drawGamepadStickVisualizer( SW_LOCKEY( "EdWidget", "RightStick" ), rx, ry, 0.15f );

                ImGui::SameLine( 360.0f );
                ImGui::BeginGroup();
                ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "LeftTrigger", TextArgumentList().addDecimal( "leftTrigger", static_cast<float64>( pGamepad->getLeftTrigger() ), 2 ) ).c_str() );
                ImGui::ProgressBar( pGamepad->getLeftTrigger(), EditorChrome::measureTextExtent( EditorTextExtent::ofText( 20.0f, 0.75f ) ) );
                ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "RightTrigger", TextArgumentList().addDecimal( "rightTrigger", static_cast<float64>( pGamepad->getRightTrigger() ), 2 ) ).c_str() );
                ImGui::ProgressBar( pGamepad->getRightTrigger(), EditorChrome::measureTextExtent( EditorTextExtent::ofText( 20.0f, 0.75f ) ) );
                ImGui::EndGroup();

                ImGui::Separator();
                ImGui::TextUnformatted( SW_LOCKEY( "EdText", "HapticVibrationTest" ) );
                EditorWidgets::drawInlineLabel( SW_LOCKEY( "EdField", "LeftMotor" ) );
                EditorChrome::setNextItemWidthColumns( 16.0f );
                ImGui::SliderFloat( "##LeftMotor", &_testVibLeft, 0.0f, 1.0f );
                EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "LeftMotorTip" ) );
                ImGui::SameLine();
                EditorWidgets::drawInlineLabel( SW_LOCKEY( "EdField", "RightMotor" ) );
                EditorChrome::setNextItemWidthColumns( 16.0f );
                ImGui::SliderFloat( "##RightMotor", &_testVibRight, 0.0f, 1.0f );
                EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "RightMotorTip" ) );
                EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "TestPulse03s" ) ) );
                if ( ImGui::Button( SW_LOCKEY( "EdText", "TestPulse03s" ) ) )
                    pGamepad->playVibration( _testVibLeft, _testVibRight, 0.3f );
                EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "TestPulseTip" ) );
                EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Stop" ) ) );
                if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Stop" ) ) )
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
        ImGui::TextUnformatted( SW_LOCKEY( "EdText", "RealTimeInputGraphsLast120Frames" ) );
        ImGui::SameLine( 450.0f );
        bool bPaused = _document.isPlotPaused();
        if ( ImGui::Checkbox( SW_LOCKEY( "EdLabel", "PauseGraph" ), &bPaused ) )
            _document.setPlotPaused( bPaused );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "PauseGraphTip" ) );

        ImGui::Separator();

        const InputMapPlotSamples& plot        = _document.getPlotSamples();
        const int32                sampleCount = static_cast<int32>( InputMapPlotSamples::kSampleCount );
        const int32                plotOffset  = static_cast<int32>( plot._offset );
        ImGui::TextUnformatted( SW_LOCKEY( "EdText", "GamepadLeftStickXY" ) );
        ImGui::PlotLines( "Stick X", plot._arrLeftStickX, sampleCount, plotOffset, "X [-1.0 ~ +1.0]", -1.0f, 1.0f, EditorChrome::measureTextExtent( EditorTextExtent::ofText( 0.0f, 3.5f ) ) );
        ImGui::PlotLines( "Stick Y", plot._arrLeftStickY, sampleCount, plotOffset, "Y [-1.0 ~ +1.0]", -1.0f, 1.0f, EditorChrome::measureTextExtent( EditorTextExtent::ofText( 0.0f, 3.5f ) ) );

        ImGui::Separator();
        ImGui::TextUnformatted( SW_LOCKEY( "EdText", "AnalogTriggersLTRT" ) );
        ImGui::PlotLines( "LT", plot._arrTriggerL, sampleCount, plotOffset, "Left [0.0 ~ 1.0]", 0.0f, 1.0f, EditorChrome::measureTextExtent( EditorTextExtent::ofText( 0.0f, 3.0f ) ) );
        ImGui::PlotLines( "RT", plot._arrTriggerR, sampleCount, plotOffset, "Right [0.0 ~ 1.0]", 0.0f, 1.0f, EditorChrome::measureTextExtent( EditorTextExtent::ofText( 0.0f, 3.0f ) ) );

        ImGui::Separator();
        ImGui::TextUnformatted( SW_LOCKEY( "EdText", "MouseDeltaSpeedDXDY" ) );
        ImGui::PlotLines( "dX", plot._arrMouseDeltaX, sampleCount, plotOffset, "Delta X", -100.0f, 100.0f, EditorChrome::measureTextExtent( EditorTextExtent::ofText( 0.0f, 3.0f ) ) );
        ImGui::PlotLines( "dY", plot._arrMouseDeltaY, sampleCount, plotOffset, "Delta Y", -100.0f, 100.0f, EditorChrome::measureTextExtent( EditorTextExtent::ofText( 0.0f, 3.0f ) ) );
    }

    void InputMapPanel::drawInputSimulatorTab()
    {
        InputManager& input = InputMapModel::getInputManager();

        ImGui::TextUnformatted( SW_LOCKEY( "EdText", "VirtualInputInjectorGameplayMacroSimulator" ) );
        ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorInputMap", "InjectorHint" ) );
        ImGui::Separator();

        ImGui::Text( "1) Key Event Injector:" );
        const utf8* arrCommonKeys[] = { "Space", "Enter", "Escape", "W", "A", "S", "D", "E", "F", "Shift", "Control" };
        const Key   arrKeyValues[]  = { Key::Space, Key::Enter, Key::Escape, Key::W, Key::A, Key::S, Key::D, Key::E, Key::F, Key::LeftShift, Key::LeftControl };

        EditorWidgets::drawInlineLabel( SW_LOCKEY( "EdField", "Key" ) );
        EditorChrome::setNextItemWidthColumns( 20.0f );
        ImGui::Combo( "##Key", &_simKeyToInject, arrCommonKeys, 11 );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "InjectKeyDown" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "InjectKeyDown" ) ) )
            input.postRawEvent( RawInputEvent::makeKeyDown( arrKeyValues[_simKeyToInject] ) );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "InjectKeyDownTip" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "InjectKeyUp" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "InjectKeyUp" ) ) )
            input.postRawEvent( RawInputEvent::makeKeyUp( arrKeyValues[_simKeyToInject] ) );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "InjectKeyUpTip" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "TapKeyDownUp" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "TapKeyDownUp" ) ) )
        {
            input.postRawEvent( RawInputEvent::makeKeyDown( arrKeyValues[_simKeyToInject] ) );
            input.postRawEvent( RawInputEvent::makeKeyUp( arrKeyValues[_simKeyToInject] ) );
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "TapKeyTip" ) );

        ImGui::Separator();
        ImGui::Text( "2) Virtual Stick 2D Slider:" );
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "SimStickX" ) );
        ImGui::SliderFloat( "##SimStickX", &_simStick._x, -1.0f, 1.0f );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "SimStickXTip" ) );
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "SimStickY" ) );
        ImGui::SliderFloat( "##SimStickY", &_simStick._y, -1.0f, 1.0f );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "SimStickYTip" ) );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "InjectStickTilt" ) ) )
        {
            GamepadDevice* pGamepad = input.getGamepad( 0 );
            if ( pGamepad != nullptr )
            {
                pGamepad->setAxis( 0, _simStick._x );
                pGamepad->setAxis( 1, _simStick._y );
            }
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "InjectStickTip" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "ResetStickToCenter" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "ResetStickToCenter" ) ) )
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
        InputManager& input = InputMapModel::getInputManager();

        ImGui::TextUnformatted( SW_LOCKEY( "EdText", "InputReplayRecorderDeterministicQAPlayback" ) );
        ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorInputMap", "ReplayHint" ) );
        ImGui::Separator();

        (void)_document.editReplayFilePath().drawField( "##ReplayFile", 260.0f ); // 확정한 경로만 Save · Load 가 쓴다
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "ReplayFileTip" ) );

        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "SaveReplay" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "SaveReplay" ) ) )
            _document.saveReplay();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "SaveReplayTip" ) );

        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "LoadReplay" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "LoadReplay" ) ) )
            _document.loadReplay( input );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "LoadReplayTip" ) );

        ImGui::Separator();

        const InputReplay& replay   = _document.getReplay();
        const bool         bPlaying = _document.isReplayPlaying( input );
        drawRecordControls( input, bPlaying );

        ImGui::Separator();

        // 재생 제어. 재생은 리플레이를 붙이는 것이고, 일시정지는 그 프레임으로 seekTo 하는 것이다(상태를 재현한 뒤 분리한다). 탐색도 seekTo 다.
        ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "PlaybackControlsTotalFrames", TextArgumentList().addInteger( "frameCount", static_cast<int64>( replay.getFrameCount() ) ).addDecimal( "totalDuration", static_cast<float64>( replay.getTotalDuration() ), 2 ) ).c_str() );
        const uint32 currentFrame = _document.getReplayCurrentFrame( input );
        drawPlaybackControls( input, bPlaying, currentFrame );

        if ( currentFrame < replay.getFrameCount() )
        {
            const InputReplayFrame&                 frame = replay.getFrames()[currentFrame];
            fixed_string<constant::buffer::kMax128> frameBuf;
            formatstring( frameBuf.data(), frameBuf.capacity(), "Frame #%u | DeltaTime: %#s | Events: %d", currentFrame,
                          Fmt( static_cast<float64>( frame._deltaTime ), Format().precision( 4 ) ), static_cast<int32>( frame._listRawEvent.size() ) );
            EditorThemeUtil::textInfo( frameBuf.c_str() );
        }
    }

    void InputMapPanel::drawRecordControls( InputManager& input, bool bPlaying )
    {
        // 녹화 제어
        const InputReplay& replay = _document.getReplay();
        if ( replay.isRecording() )
        {
            EditorThemeUtil::pushTextColor( SW_THEME_COLOR( "Signal", "Error" ) );
            ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "RECORDINGLIVEINPUTSFrames", TextArgumentList().addInteger( "frameCount", static_cast<int64>( replay.getFrameCount() ) ) ).c_str() );
            EditorThemeUtil::popTextColor();
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "■ Stop Recording" ) );
            if ( ImGui::Button( "■ Stop Recording" ) )
                _document.stopRecording();
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "StopRecordingTip" ) );
        }
        else if ( bPlaying == false )
        {
            if ( ImGui::Button( "● Start Recording" ) )
                _document.startRecording( input );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "StartRecordingTip" ) );
        }
    }

    void InputMapPanel::drawPlaybackControls( InputManager& input, bool bPlaying, uint32 currentFrame )
    {
        if ( bPlaying )
        {
            if ( ImGui::Button( "⏸ Pause" ) )
                _document.seekReplay( input, currentFrame );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "PauseReplayTip" ) );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "⏹ Stop" ) );
            if ( ImGui::Button( "⏹ Stop" ) )
                _document.seekReplay( input, 0 );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "StopReplayTip" ) );
        }
        else if ( _document.getReplay().isRecording() == false && _document.getReplay().getFrameCount() > 0 )
        {
            if ( ImGui::Button( "▶ Play Replay" ) )
                _document.playReplay( input );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "PlayReplayTip" ) );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "⏮ Step Back" ) );
            if ( ImGui::Button( "⏮ Step Back" ) && currentFrame > 0 )
                _document.seekReplay( input, currentFrame - 1 );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "StepBackTip" ) );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "⏭ Step Forward (1 Frame)" ) );
            if ( ImGui::Button( "⏭ Step Forward (1 Frame)" ) )
                _document.seekReplay( input, currentFrame + 1 );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "StepForwardTip" ) );

            int32       frameIdx  = static_cast<int32>( currentFrame );
            const int32 maxFrames = static_cast<int32>( _document.getReplay().getFrameCount() );
            EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "TimelineFrame" ) );
            if ( ImGui::SliderInt( "##TimelineFrame", &frameIdx, 0, maxFrames ) )
                _document.seekReplay( input, static_cast<uint32>( frameIdx ) );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "TimelineFrameTip" ) );
        }
    }

    void InputMapPanel::drawViewportOverlayTab()
    {
        ImGui::TextUnformatted( SW_LOCKEY( "EdText", "GameViewportOnScreenControllerOverlayHUDSettings" ) );
        ImGui::Separator();

        ViewportInputOverlay& config = InputMapModel::editViewportOverlay();

        bool bEnabled = ( config._bEnabled == SW_TRUE );
        if ( ImGui::Checkbox( SW_LOCKEY( "EdLabel", "EnableViewportOnScreenHUDOverlay" ), &bEnabled ) )
            config._bEnabled = bEnabled ? SW_TRUE : SW_FALSE;
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "OverlayEnableTip" ) );

        ImGui::Separator();

        const utf8* arrPos[] = { "Bottom-Right", "Bottom-Left", "Top-Right", "Top-Left" };
        int32       posIndex = static_cast<int32>( config._position );
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "ScreenAnchorPosition" ) );
        if ( ImGui::Combo( "##ScreenAnchorPosition", &posIndex, arrPos, 4 ) )
            config._position = static_cast<ViewportOverlayPosition>( posIndex );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "OverlayAnchorTip" ) );

        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "HUDOpacity" ) );
        ImGui::SliderFloat( "##HUDOpacity", &config._opacity, 0.1f, 1.0f );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "OverlayOpacityTip" ) );
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "HUDScale" ) );
        ImGui::SliderFloat( "##HUDScale", &config._scale, 0.5f, 2.0f );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "OverlayScaleTip" ) );

        ImGui::Separator();
        ImGui::TextUnformatted( SW_LOCKEY( "EdText", "VisibleHUDElements" ) );

        bool bStick = ( config._bShowStick == SW_TRUE );
        if ( ImGui::Checkbox( SW_LOCKEY( "EdLabel", "Show2DStickTriggerGauges" ), &bStick ) )
            config._bShowStick = bStick ? SW_TRUE : SW_FALSE;

        bool bButtons = ( config._bShowButtons == SW_TRUE );
        if ( ImGui::Checkbox( SW_LOCKEY( "EdLabel", "ShowFaceButtonsABXY" ), &bButtons ) )
            config._bShowButtons = bButtons ? SW_TRUE : SW_FALSE;

        bool bShowHistory = ( config._bShowCommandHistory == SW_TRUE );
        if ( ImGui::Checkbox( SW_LOCKEY( "EdLabel", "ShowLiveActionTriggerStream" ), &bShowHistory ) )
            config._bShowCommandHistory = bShowHistory ? SW_TRUE : SW_FALSE;
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorInputMap", "OverlayHistoryTip" ) );
    }

    void InputMapPanel::reloadFromFile()
    {
        (void)_document.reload(); // 실패는 문서가 알리고 편집 중인 바인딩 · 표시는 그대로다
    }

    bool InputMapPanel::saveToFile()
    {
        return _document.saveDocument();
    }

    void InputMapPanel::syncDocumentDirty()
    {
        if ( _document.isDirty() )
            markDocumentDirty();
        else
            clearDocumentDirty();
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
