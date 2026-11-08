#include "pch.h"

#include "Editor/Common/GUI/Panel/EditorPreviewPanel.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/LogOnce.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/Undo/CommandStack.h"
#include "Editor/Common/Workspace/Undo/ObjectSnapshot.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        /** @brief 가장 최근에 그린 미리보기 창의 재생 막대 상태 — 감시 값이 읽는다(창은 RTTI 없이 찾을 수 없다). */
        struct EditorPreviewBarState
        {
            float32 _timeScale{ 1.0f };
            bool    _bFloorShown{ SW_TRUE }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
            bool    _bAxesShown{ SW_TRUE };  ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        };

        struct EditorPreviewPanelInternal
        {
            /// @brief 재생 속도 칸 — 언리얼 Persona 재생 속도 메뉴 · 유니티 타임라인 배속과 같은 결의 고정 단계.
            static constexpr float32     kArrPreviewSpeed[] = { 0.1f, 0.25f, 0.5f, 1.0f, 2.0f };
            static constexpr uint32      kDefaultSpeedIndex = 3;
            static constexpr const utf8* kArrSpeedLabel[]   = { "0.1x", "0.25x", "0.5x", "1x", "2x" };
        };

        [[nodiscard]] bool readPreviewTimeScale( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            outValue = static_cast<float64>( getEditorState<EditorPreviewBarState>()._timeScale );
            return true;
        }

        [[nodiscard]] bool readPreviewFloorShown( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            outValue = getEditorState<EditorPreviewBarState>()._bFloorShown ? 1.0 : 0.0;
            return true;
        }

        [[nodiscard]] bool readPreviewAxesShown( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            outValue = getEditorState<EditorPreviewBarState>()._bAxesShown ? 1.0 : 0.0;
            return true;
        }

        /** @brief 감시 값 `Editor.PreviewTickCount` — 씬 매니저가 미리보기 씬들을 틱한 횟수의 합입니다. 숨은 창이면 늘지 않아야 한다. */
        [[nodiscard]] bool readPreviewTickCount( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const SceneManager* pSceneManager = findSubsystem<SceneManager>();
            if ( pSceneManager == nullptr )
                return false;
            uint64 tickCount = 0;
            for ( const PreviewSceneEntry& entry : pSceneManager->getPreviewScenes() )
            {
                tickCount += entry._tickCount;
            }
            outValue = static_cast<float64>( tickCount );
            return true;
        }
    } // namespace

    SW_AUTOMATION_WATCH( editorPreviewTimeScale, "Editor.PreviewTimeScale", "Time scale the preview bar of the preview window drawn most recently gave its scene (0 while paused)",
                         &readPreviewTimeScale );
    SW_AUTOMATION_WATCH( editorPreviewFloorShown, "Editor.PreviewFloorShown", "1 when the preview window drawn most recently shows its floor", &readPreviewFloorShown );
    SW_AUTOMATION_WATCH( editorPreviewAxesShown, "Editor.PreviewAxesShown", "1 when the preview window drawn most recently draws its axis indicator", &readPreviewAxesShown );
    SW_AUTOMATION_WATCH( editorPreviewTickCount, "Editor.PreviewTickCount", "Ticks the scene manager gave all preview scenes (a hidden preview window adds none)",
                         &readPreviewTickCount );

    EditorPreviewPanel::EditorPreviewPanel( const EditorAssetType& kind, bool bLoadOnOpen )
        : EditorDocumentPanel{ kind, bLoadOnOpen }
        , _viewport{}
        , _pCommandStack{ make_unique<CommandStack>() }
        , _pSelection{ make_unique<EditorSelection>() }
        , _bFocusApplied{ SW_FALSE }
        , _bPreviewPaused{ SW_FALSE }
        , _bFloorShown{ SW_TRUE }
        , _bAxesShown{ SW_TRUE }
        , _seenCommandCount{ 0 }
        , _previewBarSize{}
        , _speedIndex{ EditorPreviewPanelInternal::kDefaultSpeedIndex }
        , _stepFramesLeft{ 0 }
    {
    }

    EditorPreviewPanel::~EditorPreviewPanel()
    {
        applyWindowFocus( false );
        _pSelection->unbindPreviewScope();
        _viewport.shutdown();
    }

    void EditorPreviewPanel::shutdown()
    {
        applyWindowFocus( false );
        resetPreviewEdits();
        _pSelection->unbindPreviewScope();
        _viewport.shutdown();
    }

    void EditorPreviewPanel::acknowledgePreviewEdits()
    {
        _seenCommandCount = _pCommandStack->getCommandCount();
    }

    void EditorPreviewPanel::resetPreviewEdits()
    {
        _pCommandStack->clear();
        _seenCommandCount = 0;
    }

    void EditorPreviewPanel::syncDocumentDirty()
    {
        const size_t count = _pCommandStack->getCommandCount();
        if ( count == _seenCommandCount )
            return;
        _seenCommandCount = count;
        markDocumentDirty();
    }

    ScenePreviewPolicy EditorPreviewPanel::getPreviewPolicy() const
    {
        // 틱은 창이 이번 프레임에 그려질 때만이다(`ScenePreviewTick::WhenDrawn`) — 뒤 탭 · 접힌 창의 씬은 시간이 멈춘다.
        ScenePreviewPolicy policy;
        policy._tick     = ScenePreviewTick::WhenDrawn;
        policy._bPhysics = false;
        policy._bAudio   = false;
        policy._bPlay    = false;
        return policy;
    }

    void EditorPreviewPanel::beginPreviewFrame()
    {
        if ( _viewport.isInitialized() == false && _viewport.initialize( getPanelTitle(), getPreviewPolicy(), getPreviewEnvironment() ) )
        {
            Scene* pScene = _viewport.getScene();
            if ( pScene != nullptr )
            {
                _pSelection->bindPreviewScope( *pScene, *_pCommandStack );
                populatePreviewScene( *pScene );
                framePreviewSubject(); // 처음 카메라 = 맞춤(F) — 창마다 따로 두던 frameSphere 를 한 곳으로
            }
        }
        applyWindowFocus( ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows ) );
    }

    float2 EditorPreviewPanel::computeViewportFillSize( float32 minHeight ) const
    {
        const ImVec2  available  = ImGui::GetContentRegionAvail();
        const bool    bScrollbar = ImGui::GetScrollMaxY() > 0.0f; // 이 창에 지금 세로 스크롤바가 있다 — 그 폭은 이미 빠졌다
        const float32 width      = available.x - ( bScrollbar ? 0.0f : ImGui::GetStyle().ScrollbarSize );
        return float2{ width > 1.0f ? width : 1.0f, available.y > minHeight ? available.y : minHeight };
    }

    void EditorPreviewPanel::drawPreviewViewport( const float2& size, bool bOrbitBlocked )
    {
        const bool   bFocused = ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows );
        const bool   bHovered = ImGui::IsWindowHovered( ImGuiHoveredFlags_RootAndChildWindows );
        const ImVec2 origin   = ImGui::GetCursorScreenPos();
        // 막대는 이미지보다 먼저 연 자식 창이다 — 자식 창은 부모 내용 위에 그려지고 마우스도 먼저 받으며, 뒤에 놓는 이미지가 커서를 정상으로 되돌린다.
        drawPreviewBar( float2{ origin.x, origin.y }, size );
        ImGui::SetCursorScreenPos( origin );
        applyPreviewTimeScale();
        _viewport.draw( size, ( bFocused || bHovered ) && bOrbitBlocked == false );
        const bool bImageHovered = ImGui::IsItemHovered();
        if ( _bAxesShown )
            _viewport.drawAxisIndicator();
        if ( bImageHovered && EditorInputGUI::wasActionTriggered( "view.frameHovered" ) )
            framePreviewSubject();
        EditorPreviewBarState& state = getEditorState<EditorPreviewBarState>();
        state._timeScale             = _bPreviewPaused ? 0.0f : EditorPreviewPanelInternal::kArrPreviewSpeed[_speedIndex];
        state._bFloorShown           = _bFloorShown && _viewport.hasFloor();
        state._bAxesShown            = _bAxesShown;
    }

    void EditorPreviewPanel::applyPreviewTimeScale()
    {
        Scene*        pScene        = _viewport.getScene();
        SceneManager* pSceneManager = findSubsystem<SceneManager>();
        const bool    bOwnScene     = pScene != nullptr && pSceneManager != nullptr && pSceneManager->findPreviewPolicy( pScene ) != nullptr;
        if ( bOwnScene == false )
            return; // 붙인 씬(넷 재생)은 가진 쪽이 시간을 정한다
        const float32 speed  = EditorPreviewPanelInternal::kArrPreviewSpeed[_speedIndex];
        const bool    bStep  = _bPreviewPaused && _stepFramesLeft > 0;
        const float32 wanted = _bPreviewPaused && bStep == false ? 0.0f : speed;
        if ( bStep )
            --_stepFramesLeft;
        const ScenePreviewPolicy* pPolicy = pSceneManager->findPreviewPolicy( pScene );
        if ( pPolicy->_timeScale == wanted )
            return;
        ScenePreviewPolicy policy = *pPolicy;
        policy._timeScale         = wanted;
        (void)pSceneManager->setPreviewPolicy( pScene, policy ); // 이 창이 만든 미리보기 씬이라 늘 성공한다
    }

    void EditorPreviewPanel::framePreviewSubject()
    {
        float3  center{};
        float32 radius{ 0.0f };
        if ( findPreviewFrameSphere( center, radius ) )
            _viewport.frameSphere( center, radius );
    }

    void EditorPreviewPanel::drawPreviewBar( const float2& origin, const float2& size )
    {
        if ( _viewport.getScene() == nullptr || size._x <= 1.0f || size._y <= 1.0f )
            return;
        const string  scope  = string{ getDocumentKind().getID() } + ".preview";
        const float32 margin = ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetCursorScreenPos( ImVec2{ origin._x + margin, origin._y + margin } );
        ImGui::PushStyleColor( ImGuiCol_ChildBg, EditorColorUtil::toImVec4( SW_THEME_COLOR( "Base", "PanelBg" ) ) );
        const ImGuiChildFlags childFlags = ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding;
        if ( ImGui::BeginChild( "##previewBar", ImVec2{ 0.0f, 0.0f }, childFlags, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings ) )
        {
            const ImVec4 toggleOn  = EditorColorUtil::toImVec4( SW_THEME_COLOR( "Base", "ToggleActive" ) );
            const ImVec4 toggleOff = EditorColorUtil::toImVec4( SW_THEME_COLOR( "Base", "ToggleInactive" ) );
            // 재생 · 멈춤 한 단추(라벨이 상태를 말한다) — 언리얼 Persona · 유니티 미리보기의 재생 단추.
            if ( ImGui::SmallButton( _bPreviewPaused ? EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdCommon", "PreviewPlay" ), "PreviewPlayPause" )
                                                     : EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdCommon", "PreviewPause" ), "PreviewPlayPause" ) ) )
                _bPreviewPaused = _bPreviewPaused == false;
            EditorSelfTestMarks::record( ( scope + ".pause" ).c_str() );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "PreviewPauseTip" ) );
            ImGui::SameLine();
            ImGui::BeginDisabled( _bPreviewPaused == false );
            if ( ImGui::SmallButton( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdCommon", "PreviewStep" ), "PreviewStep" ) ) )
                _stepFramesLeft = 1;
            ImGui::EndDisabled();
            EditorSelfTestMarks::record( ( scope + ".step" ).c_str() );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "PreviewStepTip" ) );
            ImGui::SameLine();
            EditorChrome::setNextItemWidthColumns( 6.0f );
            if ( ImGui::BeginCombo( "##previewSpeed", EditorPreviewPanelInternal::kArrSpeedLabel[_speedIndex] ) )
            {
                for ( uint32 index = 0; index < static_cast<uint32>( std::size( EditorPreviewPanelInternal::kArrPreviewSpeed ) ); ++index )
                {
                    if ( ImGui::Selectable( EditorPreviewPanelInternal::kArrSpeedLabel[index], index == _speedIndex ) )
                        _speedIndex = index;
                }
                ImGui::EndCombo();
            }
            EditorSelfTestMarks::record( ( scope + ".speed" ).c_str() );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "PreviewSpeedTip" ) );

            float32 time{ 0.0f };
            float32 duration{ 0.0f };
            if ( readPreviewTimeline( time, duration ) && duration > 0.0f )
            {
                ImGui::SameLine();
                EditorChrome::setNextItemWidthColumns( 16.0f );
                if ( ImGui::SliderFloat( "##previewScrub", &time, 0.0f, duration, "%.2f s" ) )
                {
                    _bPreviewPaused = SW_TRUE; // 문지르는 동안은 멈춘다(언리얼 Persona 타임라인과 같다)
                    seekPreviewTimeline( time );
                }
                EditorSelfTestMarks::record( ( scope + ".scrub" ).c_str() );
                EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "PreviewScrubTip" ) );
            }

            float3     center{};
            float32    radius{ 0.0f };
            const bool bCanFrame = findPreviewFrameSphere( center, radius );
            ImGui::SameLine();
            ImGui::BeginDisabled( bCanFrame == false );
            if ( ImGui::SmallButton( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdCommon", "PreviewFrame" ), "PreviewFrame" ) ) )
                framePreviewSubject();
            ImGui::EndDisabled();
            EditorSelfTestMarks::record( ( scope + ".frame" ).c_str() );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "PreviewFrameTip" ) );
            if ( _viewport.hasFloor() || _bFloorShown == false )
            {
                ImGui::SameLine();
                ImGui::PushStyleColor( ImGuiCol_Button, _bFloorShown ? toggleOn : toggleOff );
                if ( ImGui::SmallButton( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdCommon", "PreviewFloor" ), "PreviewFloor" ) ) )
                {
                    _bFloorShown = _bFloorShown == false;
                    _viewport.setFloorVisible( _bFloorShown );
                }
                ImGui::PopStyleColor();
                EditorSelfTestMarks::record( ( scope + ".floor" ).c_str() );
                EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "PreviewFloorTip" ) );
            }
            ImGui::SameLine();
            ImGui::PushStyleColor( ImGuiCol_Button, _bAxesShown ? toggleOn : toggleOff );
            if ( ImGui::SmallButton( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdCommon", "PreviewAxes" ), "PreviewAxes" ) ) )
                _bAxesShown = _bAxesShown == false;
            ImGui::PopStyleColor();
            EditorSelfTestMarks::record( ( scope + ".axes" ).c_str() );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "PreviewAxesTip" ) );
            const ImVec2 barSize = ImGui::GetWindowSize();
            _previewBarSize      = float2{ barSize.x, barSize.y };
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    bool EditorPreviewPanel::recordPreviewModify( GameObject& obj, const ObjectSnapshot& before, const ObjectSnapshot& after, string_view label )
    {
        _pCommandStack->push( ObjectSnapshot::makeModify( editor::getSubsystem<SceneManager>(), obj, before, after, label ) ); // 인스펙터 편집은 선택의 `recordModify` 로 같은 스택
        return true;
    }

    bool EditorPreviewPanel::readDocumentIsWatchAsset( float64& outValue ) const
    {
        const GlobalVariableInfo* pInfo = editor::getSubsystem<GlobalVariableManager>().findVariable( "gv_editorWatchAsset" );
        if ( pInfo == nullptr )
            return false;
        const string wanted = pInfo->getValueAsString();
        outValue            = wanted.empty() == false && hasDocument( wanted ) ? 1.0 : 0.0;
        return true;
    }

    void EditorPreviewPanel::applyWindowFocus( bool bFocused )
    {
        Scene* pScene = _viewport.getScene();
        if ( bFocused && pScene != nullptr )
        {
            if ( _bFocusApplied )
                return;
            (void)editor::getSubsystem<SceneManager>().focusScene( pScene ); // 이 창의 씬이라 늘 알려진 씬이다
            setFocusedCommandStack( _pCommandStack.get() );
            if ( _pSelection->isPreviewScope() )
                setFocusedSelection( _pSelection.get() ); // 포커스를 잃어도 남는다 — 인스펙터가 이 선택을 고친다(`getFocusedSelection`)
            _bFocusApplied = SW_TRUE;
            return;
        }
        if ( _bFocusApplied == false )
            return;
        // 이 창이 건 것만 푼다 — 그 사이 다른 창이 자기 것을 걸었으면 그대로 둔다.
        if ( getFocusedCommandStack() == _pCommandStack.get() )
            setFocusedCommandStack( nullptr );
        // 소멸자 · 종료에서도 오는 길이라 씬 관리자가 먼저 내려갔을 수 있다 — 초점 씬을 풀 대상이 없을 뿐 오류가 아니다.
        SceneManager* pSceneManager    = findSubsystem<SceneManager>(); // 종료 순서에 따라 이미 없을 수 있다
        const bool    bOwnSceneFocused = pSceneManager != nullptr && pScene != nullptr && pSceneManager->getFocusedScene() == pScene;
        if ( bOwnSceneFocused )
            pSceneManager->clearSceneFocus();
        _bFocusApplied = SW_FALSE;
    }
} // namespace sw::editor
