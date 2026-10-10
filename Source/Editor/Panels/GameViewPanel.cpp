#include "pch.h"

#include "Editor/Panels/GameViewPanel.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Common/Commands/EditorScreenshotCommands.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Widgets/ViewportInputOverlay.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Panels/EditorPanelManager.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Object/GameObject/CameraRegistry.h"
#include "Engine/Scene/Scene.h"

#include <imgui.h>

namespace sw::editor
{
    SW_EDITOR_PANEL( GameViewPanel, "game_view", EditorPanelCategory::Core, 350 );

    GameViewPanel::GameViewPanel()
        : _listOverlayRow{}
        , _lastOverlayRowCount{ 0 }
        , _aspect{ EditorGameViewAspect::Free }
        , _bShowOverlay{ true }
        , _bNoCameraHintShown{ false }
    {
    }

    void GameViewPanel::onPanelCollapsed()
    {
        EditorContext* pClosedContext = EditorContext::get();
        if ( pClosedContext != nullptr )
        {
            pClosedContext->setGameViewFocused( false );
            pClosedContext->setGameViewHovered( false );
        }
    }

    void GameViewPanel::drawContent()
    {
        EditorContext* pEditorContext = EditorContext::get();
        if ( pEditorContext == nullptr )
            return;

        // Play 중 게임 입력은 이 패널 위에서만 게임으로 간다(`ImGuiEditor::processEvent`). 씬 뷰 위의 입력은 에디터 카메라 몫이다.
        pEditorContext->setGameViewFocused( ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows ) );
        if ( pEditorContext->isGameViewFocused() )
            EditorScreenshotCommands::noteFocusedView( EditorViewKind::Game );
        pEditorContext->setGameViewHovered( ImGui::IsWindowHovered( ImGuiHoveredFlags_RootAndChildWindows ) );

        drawToolbar();

        const ImVec2         available = ImGui::GetContentRegionAvail();
        const EditorViewRect rect      = EditorViewTargetUtil::fitViewImage( float2{ available.x, available.y }, _aspect );
        const uint32         width     = static_cast<uint32>( rect._size._x );
        const uint32         height    = static_cast<uint32>( rect._size._y );
        if ( width > 1 && height > 1 )
        {
            const EditorViewTarget& view = pEditorContext->getViewTarget( EditorViewKind::Game );
            if ( EditorViewTargetUtil::needsResize( view._width, view._height, width, height ) )
                pEditorContext->ensureViewTargetSize( EditorViewKind::Game, width, height );
        }
        // 이 패널이 보이는 프레임만 게임 뷰를 그린다(접힘 · 닫힘 · 다른 탭이면 drawContent 가 불리지 않는다 — 게임 뷰 RT 요청 0).
        pEditorContext->markViewDrawn( EditorViewKind::Game );

        // 레터박스 — 남는 쪽은 비워 둔다. 이미지는 RT 크기 그대로(늘이지 않는다).
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 imagePos{ origin.x + rect._offset._x, origin.y + rect._offset._y };
        const ImVec2 imageSize{ rect._size._x, rect._size._y };
        ImGui::SetCursorScreenPos( imagePos );
        const void* pTextureId = pEditorContext->getViewTarget( EditorViewKind::Game )._pTextureId;
        if ( pTextureId != nullptr && imageSize.x > 1.0f && imageSize.y > 1.0f )
            ImGui::Image( reinterpret_cast<ImTextureID>( pTextureId ), imageSize );
        else
            ImGui::Dummy( ImVec2{ MathUtil::max( imageSize.x, 1.0f ), MathUtil::max( imageSize.y, 1.0f ) } );
        EditorSelfTestMarks::note( "gameView.canvas" );

        // 게임 카메라가 없으면(꺼짐 · 지움) 그릴 눈이 없다 — 지난 그림 위에 안내를 띄운다(유니티 "No cameras rendering").
        Scene*                 pScene      = editor::getActiveScene();
        const CameraComponent* pGameCamera = pScene != nullptr ? pScene->getActiveGameCamera() : nullptr;
        _bNoCameraHintShown                = CameraRegistry::isUsableCamera( pGameCamera ) == false;
        ImDrawList* pDrawList              = ImGui::GetWindowDrawList();
        if ( _bNoCameraHintShown && imageSize.x > 1.0f && imageSize.y > 1.0f )
        {
            constexpr const utf8* kHint    = "No camera rendering";
            const ImVec2          hintSize = ImGui::CalcTextSize( kHint );
            pDrawList->AddRectFilled( imagePos, ImVec2{ imagePos.x + imageSize.x, imagePos.y + imageSize.y }, IM_COL32( 0, 0, 0, 255 ) );
            pDrawList->AddText( ImVec2{ imagePos.x + ( imageSize.x - hintSize.x ) * 0.5f, imagePos.y + ( imageSize.y - hintSize.y ) * 0.5f },
                                IM_COL32( 220, 220, 220, 255 ), kHint );
        }

        if ( imageSize.x > 1.0f && imageSize.y > 1.0f )
        {
            drawDebugOverlay( float2{ imagePos.x, imagePos.y }, float2{ imageSize.x, imageSize.y } );
            InputManager* pInput = getService<InputManager>();
            if ( pInput != nullptr && ViewportInputOverlay::getConfig()._bEnabled == SW_TRUE )
                ViewportInputOverlay::draw( pDrawList, imagePos, imageSize, pInput, &pInput->getInputMap() );
        }
    }

    void GameViewPanel::drawToolbar()
    {
        if ( EditorChrome::beginToolbar( "##GameViewToolbar" ) )
        {
            const utf8* pCurrentLabel = EditorViewTargetUtil::getAspectLabel( _aspect );
            ImGui::SetNextItemWidth( ImGui::GetFontSize() * 8.0f );
            if ( ImGui::BeginCombo( "##GameViewAspect", pCurrentLabel ) )
            {
                for ( uint32 aspectIndex = 0; aspectIndex < static_cast<uint32>( EditorGameViewAspect::Count ); ++aspectIndex )
                {
                    const EditorGameViewAspect aspect = static_cast<EditorGameViewAspect>( aspectIndex );
                    if ( ImGui::Selectable( EditorViewTargetUtil::getAspectLabel( aspect ), aspect == _aspect ) )
                        _aspect = aspect;
                }
                ImGui::EndCombo();
            }
            EditorSelfTestMarks::note( "gameView.aspect" );
            EditorWidgets::drawTooltip( "게임 뷰 화면 비율 — 자유(패널 전체) · 16:9(남는 쪽은 레터박스)" );

            ImGui::SameLine();
            ImGui::Checkbox( "HUD", &_bShowOverlay );
            EditorWidgets::drawTooltip( "게임이 DebugOverlayState 에 쓴 값을 게임 화면 왼쪽 아래에 표시합니다" );

            ImGui::SameLine();
            if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kCamera, "##GameViewScreenshot" ) ) )
                EditorScreenshotCommands::captureGameView();
            EditorSelfTestMarks::note( "gameView.screenshot" );
            EditorWidgets::drawTooltip( "게임 뷰 그림을 Saved/Screenshots/<시각>.png 로 저장합니다 (F9 — 포커스가 있는 뷰)" );
        }
        EditorChrome::endToolbar();
    }

    void GameViewPanel::drawDebugOverlay( const float2& canvasPos, const float2& canvasSize )
    {
        _lastOverlayRowCount              = 0;
        const DebugOverlayState* pOverlay = getService<DebugOverlayState>();
        if ( _bShowOverlay == false || pOverlay == nullptr || pOverlay->_bVisible == SW_FALSE )
            return;
        pOverlay->collectRows( _listOverlayRow );
        if ( _listOverlayRow.empty() )
            return;

        // 키 열 너비를 맞춰 값이 한 줄로 서게 한다.
        float32 keyWidth   = 0.0f;
        float32 valueWidth = 0.0f;
        for ( const DebugOverlayRow& row : _listOverlayRow )
        {
            keyWidth   = MathUtil::max( keyWidth, ImGui::CalcTextSize( row._key.c_str() ).x );
            valueWidth = MathUtil::max( valueWidth, ImGui::CalcTextSize( row._value.c_str() ).x );
        }

        // 왼쪽 아래 — 게임 UI 가 주로 쓰는 위쪽 가장자리를 비운다.
        constexpr float32 kPadding   = 6.0f;
        constexpr float32 kColumnGap = 12.0f;
        constexpr float32 kMargin    = 8.0f;
        const float32     lineHeight = ImGui::GetTextLineHeight();
        const float32     boxHeight  = kPadding * 2.0f + lineHeight * static_cast<float32>( _listOverlayRow.size() );
        const ImVec2      boxMin{ canvasPos._x + kMargin, canvasPos._y + canvasSize._y - kMargin - boxHeight };
        const ImVec2      boxMax{ boxMin.x + kPadding * 2.0f + keyWidth + kColumnGap + valueWidth, boxMin.y + boxHeight };
        ImDrawList*       pDrawList = ImGui::GetWindowDrawList();
        pDrawList->AddRectFilled( boxMin, boxMax, IM_COL32( 0, 0, 0, 150 ), 4.0f );

        float32 lineY = boxMin.y + kPadding;
        for ( const DebugOverlayRow& row : _listOverlayRow )
        {
            pDrawList->AddText( ImVec2( boxMin.x + kPadding, lineY ), IM_COL32( 160, 200, 255, 255 ), row._key.c_str() );
            pDrawList->AddText( ImVec2( boxMin.x + kPadding + keyWidth + kColumnGap, lineY ), IM_COL32( 255, 255, 255, 255 ), row._value.c_str() );
            lineY += lineHeight;
        }
        _lastOverlayRowCount = static_cast<uint32>( _listOverlayRow.size() );
    }
} // namespace sw::editor
