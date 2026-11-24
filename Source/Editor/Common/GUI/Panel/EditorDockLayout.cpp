// 도크스페이스 · 기본 배치 · 앞 탭 고르기의 그리기입니다 — MVVM View. 배치 파일 · 패널 가시성의 저장과 복원은 `Model/EditorDockLayoutPersistence.cpp` 다.
#include "pch.h"

#include "Editor/Common/GUI/Panel/EditorDockLayout.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/GUI/Panel/EditorPanel.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Panel/EditorPanelText.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace sw::editor
{
    namespace
    {
        struct EditorDockLayoutInternal
        {
            /** @brief 이보다 작은 뷰포트(최소화한 창의 0×0)는 비율의 기준으로 삼지 않습니다. 비율이 0 이 되면 되돌릴 수 없기 때문입니다. */
            static constexpr float32 kMinScaledDockspaceSize = 64.0f;

            /** @brief 기본 배치 뒤에 앞 탭을 고르려고 포커스를 주는 최대 프레임 수입니다. 창이 도킹되고 탭 막대가 만들어지는 데 한두 프레임이 걸립니다. */
            static constexpr uint8 kMaxFrontTabFrames = 8;

            /** @brief 기본 배치에서 아래 도크의 높이(글자 높이 `GetTextLineHeightWithSpacing` 의 배수)입니다. 탭 줄 · 로그 툴바 · 명령 입력 줄을 빼고도 로그가 8줄 넘게 남는 값입니다. */
            static constexpr float32 kBottomDockTextLines = 16.0f;

            /** @brief 기본 배치의 왼쪽 · 오른쪽 도크 폭 비율입니다. 넓은 창에서 쓰고, 좁은 창은 아래 최소 글자 칸 수가 이깁니다. */
            static constexpr float32 kLeftDockRatio  = 0.22f;
            static constexpr float32 kRightDockRatio = 0.28f;

            /** @brief 좌우 도크 폭의 최소값입니다(숫자 "0" 의 너비 × 칸 수). 이름 열이 말줄임으로 뭉개지지 않는 폭입니다. */
            static constexpr float32 kLeftDockMinColumns  = 40.0f;
            static constexpr float32 kRightDockMinColumns = 48.0f;

            /** @brief 좌우 도크 폭의 합이 작업 영역 폭에서 차지할 수 있는 최대 비율입니다. 좁은 창에서 가운데 영역이 사라지지 않게 합니다. */
            static constexpr float32 kMaxSideDockRatio = 0.5f;

            /** @brief 아래 도크가 작업 영역 높이에서 차지할 수 있는 최대 비율입니다. 낮은 창에서 가운데 영역이 사라지지 않게 합니다. */
            static constexpr float32 kMaxBottomDockRatio = 0.5f;

            /** @brief @p pNode 아래 모든 노드의 기준 크기(SizeRef)에 @p scale 을 곱합니다. */
            static void scaleSizeRef( ImGuiDockNode* pNode, const ImVec2& scale )
            {
                if ( pNode == nullptr )
                    return;
                pNode->SizeRef.x *= scale.x;
                pNode->SizeRef.y *= scale.y;
                scaleSizeRef( pNode->ChildNodes[0], scale );
                scaleSizeRef( pNode->ChildNodes[1], scale );
            }

            /** @brief 기본 배치가 나눈 영역의 도크 노드입니다. */
            struct DefaultDockNodes
            {
                ImGuiID _left;
                ImGuiID _right;
                ImGuiID _center;
                ImGuiID _bottom;
            };

            /** @brief 패널이 밝힌 영역의 도크 노드입니다. 떠 있는 창이면 0 입니다. */
            static ImGuiID findSlotDockID( EditorDockSlot slot, const DefaultDockNodes& nodes )
            {
                switch ( slot )
                {
                    case EditorDockSlot::Floating:
                        return 0;
                    case EditorDockSlot::Left:
                        return nodes._left;
                    case EditorDockSlot::Right:
                        return nodes._right;
                    case EditorDockSlot::Center:
                    case EditorDockSlot::CenterFront:
                        return nodes._center;
                    case EditorDockSlot::Bottom:
                    case EditorDockSlot::BottomFront:
                        return nodes._bottom;
                }
                return 0;
            }

            /**
             * @brief 영역마다의 앞 탭(`EditorDockSlot::BottomFront` · `CenterFront`)을 밝힌 패널의 창 제목입니다. 가운데(Scene)가 마지막이다 - 같은 프레임에 포커스를
             *        준 마지막 창이 키보드 포커스를 가지므로, 기본 배치는 예전처럼 씬 뷰가 입력을 받는다.
             */
            static vector<const utf8*> collectFrontTabTitles()
            {
                vector<const utf8*> listTitle;
                for ( const EditorDockSlot frontSlot : { EditorDockSlot::BottomFront, EditorDockSlot::CenterFront } )
                {
                    for ( const EditorPanelEntry& entry : EditorPanelManager::getInEditorContext().getPanels() )
                    {
                        if ( entry._pInstance != nullptr && entry._pInstance->getDefaultDockSlot() == frontSlot )
                            listTitle.push_back( entry._title.c_str() );
                    }
                }
                return listTitle;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    EditorDockLayout::EditorDockLayout()
        : _imguiINIPath{}
        , _panelVisibilityPath{}
        , _pendingLayoutINI{}
        , _pendingLayoutVisibility{}
        , _lastDockspaceWidth{ 0.0f }
        , _lastDockspaceHeight{ 0.0f }
        , _frontTabFrameCount{ 0 }
        , _bLayoutPending{ SW_FALSE }
        , _bApplied{ SW_FALSE }
        , _bResetDefault{ SW_FALSE }
        , _bFrontTabPending{ SW_FALSE }
        , _bPanelOverrideRequested{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    bool EditorDockLayout::saveNamedLayout( string_view name, const AbsolutePath& folder )
    {
        if ( ImGui::GetCurrentContext() == nullptr )
            return false;
        size_t            iniSize  = 0;
        const utf8* const pINIText = ImGui::SaveIniSettingsToMemory( &iniSize );
        return storeNamedLayout( name, folder, string_view( pINIText, iniSize ) );
    }

    void EditorDockLayout::applyPendingNamedLayout()
    {
        if ( _bLayoutPending == SW_FALSE || ImGui::GetCurrentContext() == nullptr )
            return;
        _bLayoutPending = SW_FALSE;

        // 가시성을 먼저 적용한다. 다시 열린 패널의 창이 이번 프레임에 만들어지면서 읽은 도킹 설정을 받는다.
        if ( _pendingLayoutVisibility._mapOpen.empty() == false )
        {
            for ( const EditorPanelEntry& entry : EditorPanelManager::getInEditorContext().getPanels() )
            {
                // 적지 않은 패널은 지금 상태 그대로다.
                bool bOpen = entry._pInstance != nullptr && entry._pInstance->isOpen();
                if ( entry._pInstance != nullptr && _pendingLayoutVisibility.findOpen( entry._id, bOpen ) )
                    entry._pInstance->setOpen( bOpen );
            }
        }
        ImGui::LoadIniSettingsFromMemory( _pendingLayoutINI.c_str(), _pendingLayoutINI.size() );
        // 읽은 레이아웃은 저장할 때의 도크스페이스 크기와 그 크기의 SizeRef 를 가지고 있다. 다음 프레임이 그 크기를 기준으로 현재 창에 비율을 맞춘다.
        _lastDockspaceWidth  = 0.0f;
        _lastDockspaceHeight = 0.0f;
        // 기본 배치를 다시 덮어쓰지 않게 한다. 읽은 도킹 노드가 비어 보여도 그것이 사용자의 배치다. 배치가 맞지 않으면 메뉴의 레이아웃 초기화로 되돌린다.
        _bApplied      = SW_TRUE;
        _bResetDefault = SW_FALSE;
        // 다시 연 패널의 창은 처음 나타나면서 포커스를 받아 그 탭이 앞에 선다. 그대로 두면 씬 뷰 최대화를 풀 때 Prefab Overrides 가 앞에 선다. 기본 배치처럼 Scene 탭을 앞으로 가져온다.
        _bFrontTabPending   = SW_TRUE;
        _frontTabFrameCount = 0;
        _pendingLayoutINI.clear();
        _pendingLayoutVisibility._mapOpen.clear();
    }

    void EditorDockLayout::applyINIFilename() const
    {
        ImGuiIO& io = ImGui::GetIO();

        // 진단 스위치가 켜져 있으면 저장된 레이아웃을 읽지도 쓰지도 않는다. 도킹된 패널은 같은 노드에
        // 탭으로 쌓여 앞의 하나만 그려지므로, 모두 열어도 뒤의 것은 여전히 확인되지 않는다.
        // 레이아웃을 비우면 모두 떠 있는 창이 되어 한 프레임에 전부 그려진다.
        // 하나만 열 때도 같은 이유로 레이아웃을 비운다. 저장된 도킹으로 복원되면 그 패널이
        // 탭 뒤에 숨어서 결국 보이지 않는다.
        if ( _imguiINIPath.empty() == false && isPanelOverrideActive() == false )
            io.IniFilename = _imguiINIPath.c_str();
        else
            io.IniFilename = nullptr;
    }

    void EditorDockLayout::save()
    {
        savePanelVisibility();

        // 도킹 레이아웃도 같다. 스위치가 만든 배치(전부 떠 있는 창, 기본 배치)를 사용자의 imgui.ini 에 쓰지 않는다.
        const bool bCanSaveImguiLayout = _imguiINIPath.empty() == false && isPanelOverrideActive() == false && ImGui::GetCurrentContext() != nullptr;
        if ( bCanSaveImguiLayout )
        {
            ImGui::SaveIniSettingsToDisk( _imguiINIPath.c_str() );
            SW_LOG_TRACE_ID( "EditorDockLayout.SavedImGuiLayout", "Saved ImGui layout to %#", _imguiINIPath.c_str() );
        }
    }

    void EditorDockLayout::beginDockspace()
    {
        ImGuiIO& io = ImGui::GetIO();
        if ( ( io.ConfigFlags & ImGuiConfigFlags_DockingEnable ) == 0 )
            return;

        const ImGuiViewport* pViewport = ImGui::GetMainViewport();
        // 이름의 버전 번호는 기본 배치의 창이 바뀔 때 올린다. 저장된 imgui.ini 의 이전 도크 트리를 버리고 기본 배치를 다시 만든다.
        const ImGuiID dockspaceID = ImGui::GetID( "EditorMainDockSpace_v8" );
        // 도크스페이스가 이번 프레임 크기를 나누기 전에 기준 크기를 새 뷰포트 비율로 맞춘다.
        scaleDockSizeToViewport( dockspaceID );
        // 도크 노드마다 붙던 창 목록 단추(▼)와 노드 닫기(✕)를 뺀다 — 탭의 ✕ 가 같은 일을 해 닫기가 두 개였다(언리얼 · 유니티 탭도 탭 닫기 하나다).
        constexpr ImGuiDockNodeFlags kDockFlags = ImGuiDockNodeFlags_PassthruCentralNode | static_cast<ImGuiDockNodeFlags>( ImGuiDockNodeFlags_NoWindowMenuButton ) |
                                                  static_cast<ImGuiDockNodeFlags>( ImGuiDockNodeFlags_NoCloseButton );
        (void)ImGui::DockSpaceOverViewport( dockspaceID, pViewport, kDockFlags );

        if ( _bApplied == SW_FALSE && isOpeningAllPanels() )
        {
            // 기본 도킹 배치를 적용하지 않는다(위 applyINIFilename 참고). 모두 떠 있는 창으로 둔다.
            _bApplied = SW_TRUE;
        }
        else if ( _bApplied == SW_FALSE )
        {
            const ImGuiDockNode* const pNode = ImGui::DockBuilderGetNode( dockspaceID );
            const bool                 bEmpty =
                ( pNode == nullptr ) || ( pNode->IsSplitNode() == false && pNode->Windows.Size == 0 );
            if ( bEmpty && _bResetDefault == SW_FALSE )
                SW_LOG_INFO_ID( "EditorDockLayout.DockTreeEditorMainDockSpaceV8", "Dock tree for 'EditorMainDockSpace_v8' is missing or from an older layout version - building the default layout." );
            if ( bEmpty || _bResetDefault == SW_TRUE )
                applyDefaultDockLayout( dockspaceID );
            _bApplied      = SW_TRUE;
            _bResetDefault = SW_FALSE;
        }
    }

    void EditorDockLayout::requestResetDefault()
    {
        // 저장된 배치가 있으면 도크 트리가 비어 있지 않다. 빈 트리일 때만 만드는 첫 적용과 달리, 덮어쓰라고 따로 지정한다.
        // 핵심 패널도 기본 가시성으로 — 닫힌 패널은 도크 트리를 되돌려도 그려지지 않는다.
        EditorPanelManager::getInEditorContext().restoreCorePanelVisibility();
        _bApplied      = SW_FALSE;
        _bResetDefault = SW_TRUE;
    }

    void EditorDockLayout::updateDefaultTabSelection()
    {
        if ( _bFrontTabPending == SW_FALSE )
            return;

        if ( _frontTabFrameCount >= EditorDockLayoutInternal::kMaxFrontTabFrames )
        {
            _bFrontTabPending = SW_FALSE;
            return;
        }
        // 영역마다 앞 탭을 고른다. 숨은 탭의 창도 Active 다(Begin 은 호출되고 내용만 건너뛴다). 닫힌 패널의 창은 Active 가 아니므로 고를 탭이 없어 건너뛴다.
        // DockTabIsVisible 는 이번 프레임의 Begin 이 정한 값이다. 같은 프레임에 뒤에서 처음 나타난 창(다시 연 패널)이 포커스를 가져가면 다음 프레임에 탭이 바뀌므로,
        // 두 프레임 동안 포커스를 주고 나서 확인한다.
        constexpr uint32 kMinFocusFrames = 2;
        bool             bAllStable      = true;
        for ( const utf8* pTitle : EditorDockLayoutInternal::collectFrontTabTitles() )
        {
            ImGuiWindow* pWindow = ImGui::FindWindowByName( EditorPanelText::makeWindowID( pTitle ).c_str() );
            if ( pWindow == nullptr || pWindow->Active == false )
                continue;
            if ( pWindow->DockNode != nullptr && pWindow->DockTabIsVisible && _frontTabFrameCount >= kMinFocusFrames )
                continue;
            bAllStable = false;
            ImGui::FocusWindow( pWindow ); // 도킹된 창이면 그 노드의 탭 막대에서 이 탭을 선택한다
        }
        if ( bAllStable && _frontTabFrameCount >= kMinFocusFrames )
        {
            _bFrontTabPending = SW_FALSE;
            return;
        }
        ++_frontTabFrameCount;
    }

    void EditorDockLayout::scaleDockSizeToViewport( uint32 dockspaceID )
    {
        const ImVec2 workSize = ImGui::GetMainViewport()->WorkSize;
        if ( workSize.x < EditorDockLayoutInternal::kMinScaledDockspaceSize || workSize.y < EditorDockLayoutInternal::kMinScaledDockspaceSize )
            return; // 최소화 상태이므로 기준을 바꾸지 않고 다음 실제 크기에서 비교한다

        ImGuiDockNode* pRoot = ImGui::DockBuilderGetNode( dockspaceID );
        if ( _lastDockspaceWidth <= 0.0f || _lastDockspaceHeight <= 0.0f )
        {
            // 첫 프레임이거나 이름 붙인 레이아웃을 읽은 뒤에는 저장된 도크스페이스 크기를 기준으로 한다. 다른 창 크기로 저장한 imgui.ini 도 비율로 맞는다.
            const bool bHasSavedSize = pRoot != nullptr && pRoot->Size.x >= EditorDockLayoutInternal::kMinScaledDockspaceSize &&
                                       pRoot->Size.y >= EditorDockLayoutInternal::kMinScaledDockspaceSize;
            _lastDockspaceWidth  = bHasSavedSize ? pRoot->Size.x : workSize.x;
            _lastDockspaceHeight = bHasSavedSize ? pRoot->Size.y : workSize.y;
        }

        const bool bResized           = workSize.x != _lastDockspaceWidth || workSize.y != _lastDockspaceHeight;
        const bool bRescaleSplitNodes = bResized && pRoot != nullptr && pRoot->IsSplitNode();
        if ( bRescaleSplitNodes )
        {
            const ImVec2 scale{ workSize.x / _lastDockspaceWidth, workSize.y / _lastDockspaceHeight };
            EditorDockLayoutInternal::scaleSizeRef( pRoot->ChildNodes[0], scale );
            EditorDockLayoutInternal::scaleSizeRef( pRoot->ChildNodes[1], scale );
        }
        _lastDockspaceWidth  = workSize.x;
        _lastDockspaceHeight = workSize.y;
    }

    void EditorDockLayout::applyDefaultDockLayout( uint32 dockspaceID )
    {
        const ImGuiID id = dockspaceID;
        // 작업 영역(`WorkSize`)은 앞 프레임의 사이드 바로 정해진다 — 첫 프레임은 메뉴 · 툴바가 없는 창 전체다. 그 크기로 나눈 뒤 다음 프레임에 비율로 늘리면
        // 칸이 앞 프레임에 무엇이 그려졌느냐에 따라 화소 하나씩 달랐다(체인에서 넷 플레이 단계 뒤 screen 골든). 이번 프레임에 이미 그린 메뉴 · 툴바까지 뺀
        // 크기(`GetBuildWorkRect` — 둘은 도크스페이스보다 먼저 그린다)로 한 번에 나누고, 크기 맞춤의 기준도 그 크기로 둔다.
        const ImVec2 workSize = static_cast<ImGuiViewportP*>( ImGui::GetMainViewport() )->GetBuildWorkRect().GetSize();
        _lastDockspaceWidth   = workSize.x;
        _lastDockspaceHeight  = workSize.y;

        ImGui::DockBuilderRemoveNode( id );
        ImGui::DockBuilderAddNode( id, ImGuiDockNodeFlags_DockSpace );
        ImGui::DockBuilderSetNodeSize( id, workSize );

        ImGuiID dockMain = id;
        ImGuiID dockLeft{ 0 };
        ImGuiID dockRight{ 0 };
        ImGuiID dockBottom{ 0 };
        ImGuiID dockTop{ 0 };

        // 좌우 도크 폭은 넓은 창에서는 비율을 따르고, 좁은 창에서는 글자 너비의 배수 아래로 내려가지 않는다(Hierarchy · Inspector 가 말줄임으로 뭉개진다).
        // 두 도크의 합이 작업 영역 폭의 절반을 넘으면 같은 비율로 줄여 가운데 영역을 남긴다. `Editor.PanelContentColumns`(defaultlayout 시나리오)가 지킨다.
        const float32 columnWidth = ImGui::CalcTextSize( "0" ).x;
        const float32 workWidth   = workSize.x;
        float32       leftWidth   = MathUtil::max( EditorDockLayoutInternal::kLeftDockMinColumns * columnWidth, workWidth * EditorDockLayoutInternal::kLeftDockRatio );
        float32       rightWidth  = MathUtil::max( EditorDockLayoutInternal::kRightDockMinColumns * columnWidth, workWidth * EditorDockLayoutInternal::kRightDockRatio );
        const float32 sideLimit   = workWidth * EditorDockLayoutInternal::kMaxSideDockRatio;
        if ( leftWidth + rightWidth > sideLimit )
        {
            const float32 shrink = sideLimit / ( leftWidth + rightWidth );
            leftWidth *= shrink;
            rightWidth *= shrink;
        }
        ImGui::DockBuilderSplitNode( dockMain, ImGuiDir_Left, leftWidth / workWidth, &dockLeft, &dockMain );
        // 오른쪽 비율은 왼쪽을 뺀 나머지에 대한 값이다.
        ImGui::DockBuilderSplitNode( dockMain, ImGuiDir_Right, rightWidth / ( workWidth - leftWidth ), &dockRight, &dockMain );
        // 아래 도크 높이는 비율이 아니라 글자 높이의 배수다. 비율이면 창 높이에 따라 Output Log 가 몇 줄 보일지가 달라져 낮은 창에서 한두 줄만 남는다.
        // 배수에는 탭 줄 · 로그 툴바 · 명령 입력 줄이 든다. 남는 로그 줄은 `Editor.PanelContentLines`(defaultlayout 시나리오)가 지킨다.
        // 창이 너무 낮으면 가운데 영역이 사라지지 않게 절반에서 멈춘다.
        const float32 bottomHeight = EditorDockLayoutInternal::kBottomDockTextLines * ImGui::GetTextLineHeightWithSpacing();
        const float32 bottomRatio  = MathUtil::min( bottomHeight / workSize.y, EditorDockLayoutInternal::kMaxBottomDockRatio );
        ImGui::DockBuilderSplitNode( dockMain, ImGuiDir_Down, bottomRatio, &dockBottom, &dockMain );
        (void)dockTop;

        // 패널마다 제가 밝힌 영역(`EditorPanel::getDefaultDockSlot`)에 등록된 제목으로 붙인다. 제목을 여기 따로 적으면 패널 쪽에서 제목이 바뀔 때
        // `DockBuilderDockWindow` 가 모르는 이름을 조용히 받아 그 패널이 떠 있는 창이 된다(프리팹 오버라이드 도구가 씬 뷰를 덮었다).
        // 씬 뷰와 게임 뷰는 같은 영역의 탭이다(유니티 기본). 앞 탭은 Scene(`CenterFront`)이다. 붙인 순서로는 정해지지 않는다. ImGui 는 새 탭 가운데
        // 마지막 것을 고르기 때문이다. 그래서 패널을 그린 뒤 `updateDefaultTabSelection` 이 포커스로 고른다.
        const EditorDockLayoutInternal::DefaultDockNodes nodes{ dockLeft, dockRight, dockMain, dockBottom };
        for ( const EditorPanelEntry& entry : EditorPanelManager::getInEditorContext().getPanels() )
        {
            const ImGuiID slotDockID = entry._pInstance != nullptr ? EditorDockLayoutInternal::findSlotDockID( entry._pInstance->getDefaultDockSlot(), nodes ) : 0;
            if ( slotDockID != 0 )
                ImGui::DockBuilderDockWindow( EditorPanelText::makeWindowID( entry._title.c_str() ).c_str(), slotDockID );
        }

        ImGui::DockBuilderFinish( id );
        _bFrontTabPending   = SW_TRUE;
        _frontTabFrameCount = 0;
    }

    const ImGuiDockNode* EditorDockLayout::findMainDockspace()
    {
        const ImGuiViewport* pMain       = ImGui::GetMainViewport();
        ImGuiDockContext&    dockContext = ImGui::GetCurrentContext()->DockContext;
        for ( int32 nodeIndex = 0; nodeIndex < dockContext.Nodes.Data.Size; ++nodeIndex )
        {
            const ImGuiDockNode* pNode     = static_cast<const ImGuiDockNode*>( dockContext.Nodes.Data[nodeIndex].val_p );
            const bool           bMainRoot = pNode != nullptr && pNode->ParentNode == nullptr && pNode->IsDockSpace() && pNode->HostWindow != nullptr &&
                                   pNode->HostWindow->Viewport == pMain;
            if ( bMainRoot )
                return pNode;
        }
        return nullptr;
    }

    void EditorDockLayout::collectVisibleLeaves( const ImGuiDockNode* pNode, vector<const ImGuiDockNode*>& outListLeaf )
    {
        if ( pNode == nullptr || pNode->IsVisible == false )
            return;
        if ( pNode->IsLeafNode() )
        {
            outListLeaf.push_back( pNode );
            return;
        }
        collectVisibleLeaves( pNode->ChildNodes[0], outListLeaf );
        collectVisibleLeaves( pNode->ChildNodes[1], outListLeaf );
    }
} // namespace sw::editor
