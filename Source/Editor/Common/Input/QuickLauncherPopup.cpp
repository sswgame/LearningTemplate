#include "pch.h"

#include "Editor/Common/Input/QuickLauncherPopup.h"

#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Commands/Asset/EditorAssetCommands.h"
#include "Editor/Common/Commands/Scene/EditorSceneCommands.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Input/EditorPopupManager.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"

#include <imgui.h>

// QuickLauncherLayout — 이 파일만 쓴다(한 파일 전용 도우미는 따로 헤더를 두지 않는다)
namespace sw::editor
{
    /** @brief 결과 줄 하나의 배치입니다(줄 왼쪽 위 기준 픽셀). */
    struct QuickLauncherRowLayout
    {
        float32 _badgeX{ 0.0f };         ///< 종류 표시(`[Material]`)의 x
        float32 _titleX{ 0.0f };         ///< 이름의 x
        float32 _titleClipRight{ 0.0f }; ///< 이름을 자르는 오른쪽 끝 — 경로 열 앞
        float32 _detailX{ 0.0f };        ///< 경로의 x
        float32 _rowHeight{ 0.0f };      ///< 줄 높이
        float32 _textOffsetY{ 0.0f };    ///< 글자를 줄 가운데에 놓는 y
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct QuickLauncherLayoutUtil
     * @brief 결과 줄의 열 위치를 글자 크기 · 배율에서 정합니다.
     * @details 열 위치를 고정하면(이름 x 95, 경로 x 300, 줄 높이 34) 글자 크기와 UI 배율을 따라가지 못해 `[GameObject]` 와 이름이 겹쳐
     *          `[GameObjectGameCamera` 처럼 보이고, 긴 에셋 이름이 경로 글과 겹친다.
     */
    struct QuickLauncherLayoutUtil
    {
        /**
         * @brief 줄 배치를 만듭니다.
         * @param availWidth 줄 폭
         * @param widestBadgeWidth 이번 목록의 종류 표시 가운데 가장 넓은 글의 폭
         * @param frameHeight `ImGui::GetFrameHeight()` — 줄 높이는 그 1.5 배
         * @param textHeight 글자 줄 높이
         * @param spacing 열 사이 여백(`ItemSpacing.x`)
         */
        static QuickLauncherRowLayout makeRowLayout( float32 availWidth, float32 widestBadgeWidth, float32 frameHeight, float32 textHeight, float32 spacing );
    };
} // namespace sw::editor

namespace sw::editor
{
    namespace
    {
        struct QuickLauncherLayoutInternal
        {
            static constexpr float32 kRowHeightScale  = 1.5f;  ///< 줄 높이 = 프레임 높이 × 이것
            static constexpr float32 kDetailColumnPos = 0.55f; ///< 경로 열 = 이름 열 뒤 남은 폭의 이 비율 지점
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    QuickLauncherRowLayout QuickLauncherLayoutUtil::makeRowLayout( float32 availWidth, float32 widestBadgeWidth, float32 frameHeight, float32 textHeight,
                                                                   float32 spacing )
    {
        QuickLauncherRowLayout layout{};
        layout._badgeX         = spacing;
        layout._titleX         = layout._badgeX + widestBadgeWidth + spacing * 2.0f;
        const float32 rest     = MathUtil::max( 0.0f, availWidth - layout._titleX );
        layout._detailX        = layout._titleX + rest * QuickLauncherLayoutInternal::kDetailColumnPos;
        layout._titleClipRight = layout._detailX - spacing;
        layout._rowHeight      = frameHeight * QuickLauncherLayoutInternal::kRowHeightScale;
        layout._textOffsetY    = MathUtil::max( 0.0f, ( layout._rowHeight - textHeight ) * 0.5f );
        return layout;
    }
} // namespace sw::editor

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

            /** @brief 배지 색 — 파일 항목은 콘텐츠 브라우저와 같은 종류 색(애셋 종류 등록)이다. */
            static Color getCategoryColor( const EditorSearchItem& item )
            {
                if ( item._source == EditorSearchSource::SceneObject )
                    return kGameObjectBadge;
                if ( item._source == EditorSearchSource::Asset )
                    return EditorThemeUtil::getAssetTypeColor( item._kind );
                return kDetailText; // 명령 · 창은 중립 회색(강조색은 데이터에만)
            }

            /** @brief 목록의 가장 넓은 종류 표시 폭을 @p inoutWidest 에 모읍니다. 같은 종류가 이어지면 한 번만 잰다. */
            static void measureBadge( const utf8* pCategory, float32& inoutWidest )
            {
                fixed_string<constant::buffer::kMax32> badge;
                formatstring( badge.data(), badge.capacity(), "[%s]", pCategory );
                inoutWidest = MathUtil::max( inoutWidest, ImGui::CalcTextSize( badge.c_str() ).x );
            }

            static void measureWidestBadge( const vector<EditorSearchItem>& listItem, float32& inoutWidest )
            {
                const string* pMeasuredCategory{ nullptr };
                for ( const EditorSearchItem& item : listItem )
                {
                    if ( pMeasuredCategory != nullptr && *pMeasuredCategory == item._category )
                        continue;
                    pMeasuredCategory = &item._category;
                    measureBadge( item._category.c_str(), inoutWidest );
                }
            }

            /** @brief 에셋 종류 전부의 배지를 잽니다 — 파일 색인은 일 스레드에서 늦게 와서, 온 것만 재면 첫 글자를 칠 즈음 이름 열이 한 번 밀린다. */
            static void measureAssetKindBadges( float32& inoutWidest )
            {
                for ( uint32 kindIndex = 0; kindIndex < EditorAssetTypeRegistry::getKindCount(); ++kindIndex )
                {
                    measureBadge( EditorAssetTypeRegistry::getKindAt( kindIndex )._pDisplayName, inoutWidest );
                }
            }

            /**
             * @brief 결과 줄 하나를 그립니다 — 선택 배경, 종류 배지, 제목, 설명. 긴 이름은 경로 열 앞에서, 긴 경로는 줄 끝에서 자른다.
             * @return 줄을 눌렀으면 true 입니다.
             */
            static bool drawRow( const EditorSearchItem& item, bool bSelected, const QuickLauncherRowLayout& layout )
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

                const Color                            categoryColor = getCategoryColor( item );
                fixed_string<constant::buffer::kMax32> badge;
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
        : _listItem{}
        , _listTailItem{}
        , _fileIndexJob{}
        , _searchBuffer{}
        , _selectedIndex{ 0 }
        , _widestBadgeWidth{ 0.0f }
        , _badgeFontSize{ 0.0f }
        , _bJustOpened{ false }
        , _bExecuteWhenIndexed{ SW_FALSE }
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
        _bExecuteWhenIndexed = SW_FALSE;
        _searchBuffer.clear();
        rebuildIndex();
    }

    void QuickLauncherPopup::rebuildIndex()
    {
        // Quick Open 의 제공자 순서: 씬 오브젝트 → 에셋(색인이 오면 붙는다) → 명령 → 창(팔레트와 같은 제공자, 순서만 다르다)
        _listItem.clear();
        _listTailItem.clear();
        EditorSearchItem::appendSceneObjects( _listItem );
        EditorSearchItem::appendCommands( _listTailItem );
        EditorSearchItem::appendPanels( _listTailItem );
        _badgeFontSize = 0.0f;
        _fileIndexJob.request();
    }

    void QuickLauncherPopup::pollFileIndex()
    {
        vector<EditorAssetIndexEntry> listFileEntry;
        if ( _fileIndexJob.take( listFileEntry ) != EditorTaskPoll::Ready )
            return;
        EditorSearchItem::appendAssets( listFileEntry, _listItem ); // 배지 폭은 그대로다 — 에셋 종류는 미리 다 쟀다(`getWidestBadgeWidth`)
    }

    float32 QuickLauncherPopup::getWidestBadgeWidth()
    {
        const float32 fontSize = ImGui::GetFontSize();
        if ( _badgeFontSize == fontSize )
            return _widestBadgeWidth;
        _badgeFontSize    = fontSize;
        _widestBadgeWidth = measureBadgeColumn( _listItem, _listTailItem );
        return _widestBadgeWidth;
    }

    float32 QuickLauncherPopup::measureBadgeColumn( const vector<EditorSearchItem>& listItem, const vector<EditorSearchItem>& listTailItem )
    {
        float32 widest = 0.0f;
        QuickLauncherPopupInternal::measureWidestBadge( listItem, widest );
        QuickLauncherPopupInternal::measureWidestBadge( listTailItem, widest );
        QuickLauncherPopupInternal::measureAssetKindBadges( widest );
        return widest;
    }

    bool QuickLauncherPopup::drawResults( const vector<const EditorSearchItem*>& listFiltered, bool bIndexOutstanding )
    {
        using Internal                          = QuickLauncherPopupInternal;
        const int32               filteredCount = static_cast<int32>( listFiltered.size() );
        bool                      bClicked      = false;
        editor::EditorSectionDesc resultsDesc{};
        resultsDesc._pID  = "##qlResults";
        resultsDesc._kind = editor::EditorSectionKind::Child;
        if ( EditorChrome::beginSection( resultsDesc ) )
        {
            // 열 위치는 후보 전체의 가장 넓은 종류 표시와 글자 크기에서 정한다 — 걸러 낸 결과로 재면 칠 때마다 이름 열이 좌우로 움직인다
            // (VS Code Quick Open · 언리얼 팔레트도 열을 입력과 무관하게 고정). 고정 x 는 배율 · 글꼴이 바뀌면 겹친다.
            const QuickLauncherRowLayout layout = QuickLauncherLayoutUtil::makeRowLayout( ImGui::GetContentRegionAvail().x, getWidestBadgeWidth(),
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
            _bExecuteWhenIndexed = SW_TRUE;
        if ( _bExecuteWhenIndexed && filteredCount > 0 )
        {
            _bExecuteWhenIndexed = SW_FALSE;
            bExecute             = true;
        }
        if ( _bExecuteWhenIndexed && bIndexOutstanding == false )
            _bExecuteWhenIndexed = SW_FALSE; // 목록이 왔는데도 맞는 것이 없다
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

        vector<const EditorSearchItem*> listFiltered;
        // 에셋 색인이 오기 전에는 명령 · 창을 섞지 않는다 — 파일 이름을 치고 바로 누른 Enter 가 명령을 실행하지 않고 색인을 기다린다(`resolvePendingExecute`)
        if ( bIndexOutstanding )
            EditorSearchItem::filter( { &_listItem }, _searchBuffer.view(), listFiltered );
        else
            EditorSearchItem::filter( { &_listItem, &_listTailItem }, _searchBuffer.view(), listFiltered );

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
            const EditorSearchItem* pTarget = listFiltered[static_cast<size_t>( _selectedIndex )];
            if ( pTarget != nullptr )
            {
                const EditorSearchItem item = *pTarget; // 실행이 목록을 다시 만들 수 있다 — 사본으로 부른다
                close();
                EditorSearchItem::execute( item );
                return;
            }
            close();
        }
    }
} // namespace sw::editor
