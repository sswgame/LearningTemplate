#include "pch.h"

#include "Editor/Popups/QuickLauncherPopup.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include "Editor/Common/Commands/Asset/EditorAssetCommands.h"
#include "Editor/Common/Commands/Scene/EditorSceneCommands.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/EditorColorUtil.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Input/EditorPopupManager.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Popups/QuickLauncherLayout.h"

#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct QuickLauncherPopupInternal
        {
            static constexpr Color kSelectedRowBg{ Color::fromBytes( 40, 75, 130, 220 ) };
            static constexpr Color kSelectedRowBorder{ Color::fromBytes( 80, 140, 240, 255 ) };
            static constexpr Color kTitleText{ Color::fromBytes( 240, 240, 245, 255 ) };
            static constexpr Color kDetailText{ Color::fromBytes( 150, 155, 170, 200 ) };
            static constexpr Color kGameObjectBadge{ 0.95f, 0.85f, 0.30f, 1.0f };

            /** @brief 배지 색 — 파일 항목은 콘텐츠 브라우저와 같은 종류 색(애셋 종류 테이블)이다. */
            static Color getCategoryColor( const QuickLauncherItem& item )
            {
                if ( item._category == "GameObject" )
                    return kGameObjectBadge;
                return EditorThemeUtil::getAssetTypeColor( item._kind );
            }

            /** @brief 검색어가 비면 모두, 아니면 제목 · 설명 · 종류 중 하나에 검색어가 든 항목만 @p outListFiltered 에 담습니다(대소문자 무시). */
            static void filterItems( const vector<QuickLauncherItem>& listAllItem, const fixed_string<constant::kMaxBuffer128>& search, vector<const QuickLauncherItem*>& outListFiltered )
            {
                outListFiltered.reserve( listAllItem.size() );
                for ( const QuickLauncherItem& item : listAllItem )
                {
                    if ( search.empty() )
                    {
                        outListFiltered.push_back( &item );
                        continue;
                    }

                    const bool bMatchesSearchText = StringUtil::stristr( item._title.c_str(), search.c_str() ) != nullptr || StringUtil::stristr( item._detail.c_str(), search.c_str() ) != nullptr ||
                                                    StringUtil::stristr( item._category.c_str(), search.c_str() ) != nullptr;
                    if ( bMatchesSearchText )
                        outListFiltered.push_back( &item );
                }
            }

            /** @brief 이번 목록의 가장 넓은 종류 표시의 너비입니다. 종류는 몇 가지뿐이라 바뀔 때만 측정한다. */
            static float32 measureWidestBadge( const vector<const QuickLauncherItem*>& listFiltered )
            {
                float32       widestBadgeWidth{ 0.0f };
                const string* pMeasuredCategory{ nullptr };
                for ( const QuickLauncherItem* pItem : listFiltered )
                {
                    if ( pMeasuredCategory != nullptr && *pMeasuredCategory == pItem->_category )
                        continue;
                    pMeasuredCategory = &pItem->_category;
                    fixed_string<constant::kMaxBuffer32> badge;
                    formatstring( badge.data(), badge.capacity(), "[%s]", pItem->_category.c_str() );
                    widestBadgeWidth = MathUtil::max( widestBadgeWidth, ImGui::CalcTextSize( badge.c_str() ).x );
                }
                return widestBadgeWidth;
            }

            /**
             * @brief 결과 줄 하나를 그립니다 — 선택 배경, 종류 배지, 제목, 설명. 긴 이름은 경로 열 앞에서, 긴 경로는 줄 끝에서 자른다.
             * @return 줄을 눌렀으면 true 입니다.
             */
            static bool drawRow( const QuickLauncherItem& item, bool bSelected, const QuickLauncherRowLayout& layout )
            {
                ImVec2        cursor    = ImGui::GetCursorScreenPos();
                ImDrawList*   pDrawList = ImGui::GetWindowDrawList();
                const float32 availW    = ImGui::GetContentRegionAvail().x;
                const float32 itemH     = layout._rowHeight;
                const float32 textY     = cursor.y + layout._textOffsetY;

                if ( bSelected )
                {
                    pDrawList->AddRectFilled( cursor, ImVec2( cursor.x + availW, cursor.y + itemH ), EditorColorUtil::toImU32( kSelectedRowBg ), 4.0f );
                    pDrawList->AddRect( cursor, ImVec2( cursor.x + availW, cursor.y + itemH ), EditorColorUtil::toImU32( kSelectedRowBorder ), 4.0f );
                }

                const Color                          categoryColor = getCategoryColor( item );
                fixed_string<constant::kMaxBuffer32> badge;
                formatstring( badge.data(), badge.capacity(), "[%s]", item._category.c_str() );

                pDrawList->AddText( ImVec2( cursor.x + layout._badgeX, textY ), EditorColorUtil::toImU32( categoryColor ), badge.c_str() );

                pDrawList->PushClipRect( ImVec2( cursor.x + layout._titleX, cursor.y ), ImVec2( cursor.x + layout._titleClipRight, cursor.y + itemH ), true );
                pDrawList->AddText( ImVec2( cursor.x + layout._titleX, textY ), EditorColorUtil::toImU32( kTitleText ), item._title.c_str() );
                pDrawList->PopClipRect();

                pDrawList->PushClipRect( ImVec2( cursor.x + layout._detailX, cursor.y ), ImVec2( cursor.x + availW, cursor.y + itemH ), true );
                pDrawList->AddText( ImVec2( cursor.x + layout._detailX, textY ), EditorColorUtil::toImU32( kDetailText ), item._detail.c_str() );
                pDrawList->PopClipRect();

                return ImGui::InvisibleButton( "##itemBtn", ImVec2( availW, itemH ) );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_POPUP( QuickLauncherPopup, 100 );
    // 파일 목록은 벽시계로 도는 일 스레드가 모은다 — 시나리오가 프레임 수 예산으로 기다리면 프레임이 빠른 백엔드(DX11)가 목록이 오기 전에 예산을 다 쓴다.
    // 목록이 올 때까지 시나리오 시계를 잡아, Quick Open 을 쓰는 모든 시나리오가 프레임 속도와 상관없이 같은 프레임에 문서를 연다.
    SW_AUTOMATION_CLOCK_HOLDER( quickLauncherIndexing, "QuickLauncherIndexing", &QuickLauncherPopup::isIndexing );

    QuickLauncherPopup::QuickLauncherPopup()
        : _listAllItem{}
        , _fileIndexJob{}
        , _searchBuffer{}
        , _selectedIndex{ 0 }
        , _bJustOpened{ false }
        , _bExecuteWhenIndexed{ false }
    {
    }

    void QuickLauncherPopup::open()
    {
        EditorContext& editorContext = editor::getSubsystem<EditorContext>();
        editorContext.getSubsystem<EditorPopupManager>().openPopup( kPopupID );
    }

    void QuickLauncherPopup::close()
    {
        EditorContext& editorContext = editor::getSubsystem<EditorContext>();
        editorContext.getSubsystem<EditorPopupManager>().closePopup( kPopupID );
    }

    void QuickLauncherPopup::toggle()
    {
        EditorContext& editorContext = editor::getSubsystem<EditorContext>();
        editorContext.getSubsystem<EditorPopupManager>().togglePopup( kPopupID );
    }

    bool QuickLauncherPopup::isOpen()
    {
        EditorContext& editorContext = editor::getSubsystem<EditorContext>();
        return editorContext.getSubsystem<EditorPopupManager>().isPopupOpen( kPopupID );
    }

    bool QuickLauncherPopup::isIndexing()
    {
        EditorContext&            editorContext = editor::getSubsystem<EditorContext>();
        EditorPopupManager&       popups        = editorContext.getSubsystem<EditorPopupManager>();
        const QuickLauncherPopup* pPopup        = popups.findPopup<QuickLauncherPopup>( kPopupID );
        return pPopup != nullptr && popups.isPopupOpen( kPopupID ) && pPopup->_fileIndexJob.isPending();
    }

    void QuickLauncherPopup::onOpen()
    {
        _bJustOpened         = true;
        _selectedIndex       = 0;
        _bExecuteWhenIndexed = false;
        _searchBuffer.clear();
        rebuildIndex();
    }

    void QuickLauncherPopup::rebuildIndex()
    {
        _listAllItem.clear();

        GameObjectManager* pObjects = editor::getActiveObjectManager();
        if ( pObjects != nullptr )
        {
            // 값으로 반환하는 `getAllGameObjects()` 는 씬 전체를 복사한다. 순회만 하므로 복사하지 않는 쪽을 쓴다.
            pObjects->forEachGameObject( [this]( GameObject* pObj )
            {
                const uint64      objID = pObj->getObjectID();
                QuickLauncherItem item{};
                item._category = "GameObject";
                item._title    = string{ pObj->getName().c_str() };

                StringBuilder<constant::kMaxBuffer64> detailSb;
                detailSb.appendFormat( "Scene GameObject (ID: %#)", objID );
                item._detail         = string{ detailSb.view() };
                item._targetObjectID = objID;
                _listAllItem.push_back( std::move( item ) );
            } );
        }

        _fileIndexJob.request();
    }

    void QuickLauncherPopup::pollFileIndex()
    {
        vector<EditorResourceIndexEntry> listFileEntry;
        if ( _fileIndexJob.take( listFileEntry ) != EditorTaskPoll::Ready )
            return;

        for ( const EditorResourceIndexEntry& entry : listFileEntry )
        {
            QuickLauncherItem item{};
            item._kind     = entry._kind;
            item._category = entry._category;
            item._title    = entry._title;
            item._detail   = entry._detail;
            item._path     = entry._path;
            _listAllItem.push_back( std::move( item ) );
        }
    }

    void QuickLauncherPopup::executeItem( const QuickLauncherItem& item )
    {
        if ( item._category == "GameObject" )
        {
            GameObjectManager* pObjects = editor::getActiveObjectManager();
            if ( pObjects == nullptr )
                return;

            GameObject* pFound = pObjects->findGameObjectByID( item._targetObjectID );
            EditorSceneCommands::select( pFound, SelectionMode::Replace );
            return;
        }

        // 콘텐츠 브라우저 더블클릭과 같이 연다 — 도구 패널이 있는 종류는 그 패널을, 여는 동작이 있는 종류(씬)는 그 동작을.
        // 둘 다 없는 종류는 포커스만 옮긴다(인스펙터 · 콘텐츠 브라우저가 따라간다).
        if ( EditorAssetCommands::openPath( item._path ) )
            return;
        EditorAssetCommands::focusPath( item._path );
    }

    bool QuickLauncherPopup::drawResults( const vector<const QuickLauncherItem*>& listFiltered, bool bIndexOutstanding )
    {
        using Internal                          = QuickLauncherPopupInternal;
        const int32               filteredCount = static_cast<int32>( listFiltered.size() );
        bool                      bClicked      = false;
        editor::EditorSectionDesc resultsDesc{};
        resultsDesc._pID  = "##qlResults";
        resultsDesc._kind = editor::EditorSectionKind::Child;
        if ( EditorChrome::beginSection( resultsDesc ) )
        {
            // 열 위치는 이번 목록의 가장 넓은 종류 표시와 글자 크기에서 정한다(고정 x 는 배율 · 글꼴이 바뀌면 겹친다).
            const QuickLauncherRowLayout layout = QuickLauncherLayoutUtil::makeRowLayout( ImGui::GetContentRegionAvail().x, Internal::measureWidestBadge( listFiltered ),
                                                                                          ImGui::GetFrameHeight(), ImGui::GetTextLineHeight(), ImGui::GetStyle().ItemSpacing.x );

            for ( int32 itemIndex = 0; itemIndex < filteredCount; ++itemIndex )
            {
                const bool bSelected = ( itemIndex == _selectedIndex );

                ImGui::PushID( itemIndex );
                if ( Internal::drawRow( *listFiltered[static_cast<size_t>( itemIndex )], bSelected, layout ) )
                {
                    _selectedIndex = itemIndex;
                    bClicked       = true;
                }

                if ( bSelected )
                    ImGui::SetScrollHereY( 0.5f );

                ImGui::PopID();
            }

            if ( filteredCount == 0 )
            {
                if ( bIndexOutstanding )
                    EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdCommon", "QuickOpenIndexing" ) );
                else
                    EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdCommon", "QuickOpenNoMatches" ) );
            }
        }
        EditorChrome::endSection();
        return bClicked;
    }

    bool QuickLauncherPopup::resolvePendingExecute( int32 filteredCount, bool bIndexOutstanding, bool bExecute )
    {
        // 이름을 치고 바로 Enter 를 누르면 파일 목록(일 스레드)이 아직 안 왔을 수 있다 — 그때는 목록이 오는 대로 연다.
        // 목록이 비면 `updateListSelection` 이 Enter 를 보지 않으므로 키를 직접 본다.
        const bool bEnterPressed         = EditorInputGUI::wasEditKeyPressed( EditorEditKey::Enter ); // 글을 치는 동안 듣는 편집 키(입력 맵 밖)
        const bool bExecuteAfterIndexing = bEnterPressed && filteredCount == 0 && bIndexOutstanding;
        if ( bExecuteAfterIndexing )
            _bExecuteWhenIndexed = true;
        if ( _bExecuteWhenIndexed && filteredCount > 0 )
        {
            _bExecuteWhenIndexed = false;
            bExecute             = true;
        }
        if ( _bExecuteWhenIndexed && bIndexOutstanding == false )
            _bExecuteWhenIndexed = false; // 목록이 왔는데도 맞는 것이 없다
        return bExecute;
    }

    void QuickLauncherPopup::drawContent()
    {
        // 파일 목록이 아직 안 왔는지는 꺼내기 **전에** 한 번 읽고, 이 프레임은 그 값만 쓴다. 꺼낸 뒤에 다시 물으면 그 사이 워커가 끝낸 결과가
        // "대기 아님 · 목록 비어 있음" 으로 보여 미뤄 둔 Enter 를 버린다(결과는 다음 프레임에야 꺼낸다).
        const bool bIndexOutstanding = _fileIndexJob.isPending();
        pollFileIndex();

        editor::EditorSearchOverlayDesc overlayDesc{};
        overlayDesc._pID          = "##QuickLauncherOverlay";
        overlayDesc._pOpen        = &_bOpen;
        overlayDesc._size         = EditorTextExtent::ofText( 83.0f, 21.0f );
        overlayDesc._pFocusOnOpen = &_bJustOpened;

        if ( EditorChrome::beginSearchOverlay( overlayDesc ) == false )
        {
            EditorChrome::endSearchOverlay();
            close();
            return;
        }

        EditorWidgets::drawSearchField( "##qlSearch", _searchBuffer,
                                        SW_LOCKEY( "EdCommon", "QuickOpenHint" ), -1.0f, true );

        vector<const QuickLauncherItem*> listFiltered;
        QuickLauncherPopupInternal::filterItems( _listAllItem, _searchBuffer, listFiltered );

        const int32 filteredCount = static_cast<int32>( listFiltered.size() );
        bool        bExecute      = EditorWidgets::updateListSelection( _selectedIndex, filteredCount, false );

        ImGui::Separator();

        if ( drawResults( listFiltered, bIndexOutstanding ) )
            bExecute = true;

        EditorChrome::endSearchOverlay();

        bExecute                        = resolvePendingExecute( filteredCount, bIndexOutstanding, bExecute );
        const bool bExecuteSelectedItem = bExecute && filteredCount > 0 && 0 <= _selectedIndex && _selectedIndex < filteredCount;
        if ( bExecuteSelectedItem )
        {
            const QuickLauncherItem* pTarget = listFiltered[static_cast<size_t>( _selectedIndex )];
            if ( pTarget != nullptr )
                executeItem( *pTarget );
            close();
        }
    }
} // namespace sw::editor
