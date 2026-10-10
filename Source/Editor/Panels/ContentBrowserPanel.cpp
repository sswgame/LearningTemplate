#include "pch.h"

#include "Editor/Panels/ContentBrowserPanel.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "Editor/AssetActions/EditorAssetTypeActions.h"
#include "Editor/Common/Commands/EditorAssetCommands.h"
#include "Editor/Common/Config/EditorPreferences.h"
#include "Editor/Common/Config/EditorSettingsRegistry.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/SourceControl/EditorSourceControl.h"
#include "Editor/Common/Widgets/EditorListFilter.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/AssetHotReload.h"
#include "Editor/Common/Workspace/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/EditorPanelManager.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/AutomationProbe.h"
#include "Engine/Common/EngineDefines.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Resource/ResourceUtil.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct ContentBrowserPanelInternal
        {
            /** @brief 타일 이름 줄 수 — 넘치면 마지막 줄을 말줄임한다(`EditorWidgets::drawClampedLabel`). */
            static constexpr uint32 kTileLabelLineCount = 2;

            /** @brief 삭제 확인 모달의 팝업 이름입니다(OpenPopup 과 BeginPopupModal 이 같은 글을 써야 한다). */
            static constexpr const utf8* kDeleteConfirmPopupName = "Delete Asset##ConfirmDeleteAsset";

            static ImVec4 colorForAsset( string_view path, bool bIsDirectory = false )
            {
                const Color4 c = EditorThemeUtil::getAssetColorForPath( path, bIsDirectory );
                return ImVec4( c._r, c._g, c._b, c._a );
            }

            static const utf8* typeLabel( string_view path, bool bIsDirectory )
            {
                if ( bIsDirectory )
                    return "Folder";
                const EditorAssetTypeInfo* pInfo = EditorAssetTypeRegistry::findKindInfo( EditorAssetTypeRegistry::findKind( path ) );
                return pInfo != nullptr ? pInfo->_pBrowserLabel : "File";
            }

            /** @brief 참조 찾기 결과 창의 팝업 이름입니다. */
            static constexpr const utf8* kReferenceResultsPopupName = "References##ContentBrowserReferences";
            /** @brief 게임 팩 루트의 표시 이름 접두입니다(`game/<팩>`). */
            static constexpr const utf8* kGameDomainPrefix = "game/";
            /** @brief 참조 결과 표가 스크롤 없이 보이는 최대 줄 수입니다. */
            static constexpr size_t kReferenceVisibleRowCount = 12;

            /** @brief 시험이 켜졌을 때 바로 앞 위젯에 `<접두><이름>` 이름표를 남깁니다(꺼져 있으면 글을 만들지 않는다). */
            static void noteMark( const utf8* pPrefix, string_view name )
            {
                if ( EditorSelfTestMarks::isEnabled() == false )
                    return;
                const string key = string( pPrefix ) + string( name );
                EditorSelfTestMarks::note( key.c_str() );
            }

            /** @brief 콘텐츠 브라우저 패널입니다. 없으면 nullptr. */
            static const ContentBrowserPanel* findPanel()
            {
                EditorContext* pContext = EditorContext::get();
                if ( pContext == nullptr )
                    return nullptr;
                return static_cast<const ContentBrowserPanel*>( pContext->getPanelManager().findPanel( "content_browser" ) );
            }

            [[nodiscard]] static bool readGameRootCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const ContentBrowserPanel* pPanel = findPanel();
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getGameRootCount() );
                return true;
            }

            [[nodiscard]] static bool readReferenceIndexReady( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const ContentBrowserPanel* pPanel = findPanel();
                if ( pPanel == nullptr )
                    return false;
                outValue = pPanel->isReferenceIndexReady() ? 1.0 : 0.0;
                return true;
            }

            /** @brief `content.open <리소스 폴더>` — 콘텐츠 브라우저를 열고 그 폴더로 갑니다(언리얼 Sync to Content Browser 와 같다). */
            static bool runContentOpen( const vector<string>& listArgument, string& outReply )
            {
                EditorContext* pContext = EditorContext::get();
                if ( listArgument.size() != 1 || pContext == nullptr )
                    return false;
                const string folderAbs = FileUtil::joinPath( ResourceUtil::getRootFolderPath(), FileUtil::normalizePath( listArgument[0] ) );
                if ( FileUtil::isDirectory( folderAbs ) == false || pContext->getPanelManager().setPanelOpen( "content_browser", true ) == false )
                    return false;
                ContentBrowserPanel* pPanel = static_cast<ContentBrowserPanel*>( pContext->getPanelManager().findPanel( "content_browser" ) );
                if ( pPanel == nullptr )
                    return false;
                pPanel->openFolder( folderAbs );
                outReply = "content browser at " + listArgument[0];
                return true;
            }

            [[nodiscard]] static bool readReferenceResultCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const ContentBrowserPanel* pPanel = findPanel();
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getReferenceResultCount() );
                return true;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "ContentBrowserPanel" );
    SW_EDITOR_PANEL( ContentBrowserPanel, "content_browser", EditorPanelCategory::Core, 600 );
    SW_AUTOMATION_PROBE( editorContentGameRootCount, "Editor.ContentGameRootCount", "Game pack roots the Content Browser lists (1 = active pack only)",
                         &ContentBrowserPanelInternal::readGameRootCount );
    SW_AUTOMATION_PROBE( editorReferenceIndexReady, "Editor.ReferenceIndexReady", "1 once the Content Browser reference index has been built",
                         &ContentBrowserPanelInternal::readReferenceIndexReady );
    SW_AUTOMATION_PROBE( editorReferenceResultCount, "Editor.ReferenceResultCount", "Rows of the last Find References or Show Dependencies query",
                         &ContentBrowserPanelInternal::readReferenceResultCount );
    SW_DEV_COMMAND( ContentOpen, "content.open", "content.open <resource folder>", "Show a resource folder in the Content Browser (e.g. game/empty/models)",
                    &ContentBrowserPanelInternal::runContentOpen );

    void ContentBrowserPanel::drawAssetThumbnail( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos,
                                                  const AssetEntry& entry )
    {
        if ( pDrawList == nullptr )
            return;

        const ImVec2 minVec{ minPos._x, minPos._y };
        const ImVec2 maxVec{ maxPos._x, maxPos._y };

        const float32 w  = maxPos._x - minPos._x;
        const float32 h  = maxPos._y - minPos._y;
        const float32 cx = minPos._x + w * 0.5f;
        const float32 cy = minPos._y + h * 0.5f;

        // 카드 썸네일 배경
        pDrawList->AddRectFilled( minVec, maxVec, IM_COL32( 22, 24, 30, 255 ), 4.0f );

        const utf8* pPath = entry._absolutePath.empty() ? entry._name.c_str() : entry._absolutePath.c_str();
        if ( entry._bIsDirectory )
        {
            drawFolderThumbnail( pDrawList, minPos, maxPos );
        }
        else
        {
            // 종류별 그림은 그 종류의 동작이 그린다(`IEditorAssetTypeActions`). 없으면 종류 라벨을 얹은 일반 문서다.
            const IEditorAssetTypeActions* pActions = EditorAssetTypeActionsRegistry::findActionsForPath( pPath );
            const bool                     bDrawn   = pActions != nullptr && pActions->drawThumbnail( pDrawList, minPos, maxPos );
            if ( bDrawn == false )
            {
                pDrawList->AddRectFilled( ImVec2( minPos._x + w * 0.22f, minPos._y + h * 0.16f ),
                                          ImVec2( minPos._x + w * 0.78f, minPos._y + h * 0.84f ), IM_COL32( 65, 70, 82, 255 ), 3.0f );
                const utf8*  pLbl  = ContentBrowserPanelInternal::typeLabel( entry._extension, false );
                const ImVec2 txtSz = ImGui::CalcTextSize( pLbl );
                pDrawList->AddText( ImVec2( cx - txtSz.x * 0.5f, cy - txtSz.y * 0.5f ), IM_COL32( 220, 225, 235, 230 ), pLbl );
            }
        }

        // 안쪽 테두리
        pDrawList->AddRect( minVec, maxVec, IM_COL32( 50, 55, 65, 200 ), 4.0f );
    }

    void ContentBrowserPanel::drawFolderThumbnail( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos )
    {
        const ImVec2  minVec{ minPos._x, minPos._y };
        const ImVec2  maxVec{ maxPos._x, maxPos._y };
        const float32 w = maxPos._x - minPos._x;
        const float32 h = maxPos._y - minPos._y;

        const Color4 folderColor = EditorThemeUtil::getFolderColor();

        // 카드의 옅은 테두리
        pDrawList->AddRect( minVec, maxVec, IM_COL32( 40, 48, 62, 160 ), 4.0f );

        // UE5 스타일의 겹친 폴더 아이콘
        // 1) 뒤쪽 탭과 뒤판(짙은 슬레이트)
        const uint32 colorBack = IM_COL32(
            static_cast<int32>( folderColor._r * 110 + 15 ),
            static_cast<int32>( folderColor._g * 125 + 20 ),
            static_cast<int32>( folderColor._b * 165 + 30 ),
            255 );
        // 2) 앞주머니(테마 액센트)
        const uint32 colorFront = IM_COL32(
            static_cast<int32>( folderColor._r * 190 + 20 ),
            static_cast<int32>( folderColor._g * 205 + 25 ),
            static_cast<int32>( folderColor._b * 235 + 20 ),
            255 );
        // 3) 앞주머니 윗면 하이라이트
        const uint32 colorPocketHighlight = IM_COL32(
            static_cast<int32>( folderColor._r * 255 ),
            static_cast<int32>( folderColor._g * 255 ),
            static_cast<int32>( folderColor._b * 255 ),
            220 );

        const float32 fLeft   = minPos._x + w * 0.18f;
        const float32 fRight  = minPos._x + w * 0.82f;
        const float32 fTabR   = minPos._x + w * 0.48f;
        const float32 fTabTop = minPos._y + h * 0.20f;
        const float32 fTop    = minPos._y + h * 0.28f;
        const float32 fPktTop = minPos._y + h * 0.38f;
        const float32 fBottom = minPos._y + h * 0.78f;

        // 뒤쪽 탭
        pDrawList->AddRectFilled( ImVec2( fLeft, fTabTop ), ImVec2( fTabR, fTop + 2.0f ), colorBack, 3.0f );
        // 뒤판
        pDrawList->AddRectFilled( ImVec2( fLeft, fTop ), ImVec2( fRight, fBottom ), colorBack, 3.0f );

        // 앞주머니
        pDrawList->AddRectFilled( ImVec2( fLeft, fPktTop ), ImVec2( fRight, fBottom ), colorFront, 3.0f );
        // 앞주머니 윗면 하이라이트
        pDrawList->AddLine( ImVec2( fLeft + 2.0f, fPktTop + 1.0f ), ImVec2( fRight - 2.0f, fPktTop + 1.0f ), colorPocketHighlight, 1.5f );
        // 테두리
        pDrawList->AddRect( ImVec2( fLeft, fPktTop ), ImVec2( fRight, fBottom ), IM_COL32( 15, 25, 45, 120 ), 3.0f );
    }

    string ContentBrowserPanel::describeSourceControlStatus( const AssetEntry& entry )
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr || entry._bIsDirectory )
            return {};
        return pContext->getSourceControl().describeStatus( entry._absolutePath, entry._bReadOnly );
    }

    void ContentBrowserPanel::drawSourceControlBadge( ImDrawList* pDrawList, const float2& topRight, const AssetEntry& entry )
    {
        const string statusText = describeSourceControlStatus( entry );
        if ( statusText.empty() )
            return;
        // 잠금(남이든 나든)은 주황, 읽기 전용(잠그기 전)은 회색 자물쇠 — 바로 아래 버튼의 도구 설명이 이유를 말한다.
        EditorContext* pContext = EditorContext::get();
        const bool     bLocked  = pContext != nullptr && pContext->getSourceControl().findLock( entry._absolutePath ) != nullptr;
        const ImU32    color    = bLocked ? IM_COL32( 255, 160, 40, 255 ) : IM_COL32( 170, 170, 170, 255 );
        const ImVec2   textSize = ImGui::CalcTextSize( editoricon::kLock );
        pDrawList->AddText( ImVec2( topRight._x - textSize.x, topRight._y ), color, editoricon::kLock );
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "%s", statusText.c_str() );
    }

    void ContentBrowserPanel::drawAssetContextMenu( const AssetEntry& entry )
    {
        if ( ImGui::BeginPopupContextItem( "AssetCtx" ) )
        {
            if ( ImGui::MenuItem( "Show in Explorer" ) )
                EditorAssetCommands::showInFileExplorer( entry._absolutePath );

            if ( ImGui::MenuItem( "Copy Relative Path" ) )
                ImGui::SetClipboardText( entry._relativePath.c_str() );

            if ( ImGui::MenuItem( "Copy Absolute Path" ) )
                ImGui::SetClipboardText( entry._absolutePath.c_str() );

            // 참조 — 텍스트 에셋을 훑은 역색인으로 찾는다. 결과 창은 이 메뉴 밖, 창 단위에서 연다.
            if ( entry._bIsDirectory == false )
            {
                ImGui::Separator();
                if ( ImGui::MenuItem( "Find References" ) )
                    showReferences( entry._absolutePath, false );
                EditorSelfTestMarks::note( "contentBrowser.menu.findReferences" );
                if ( ImGui::MenuItem( "Show Dependencies", nullptr, false, EditorReferenceIndex::isTextAsset( entry._absolutePath ) ) )
                    showReferences( entry._absolutePath, true );
                EditorSelfTestMarks::note( "contentBrowser.menu.showDependencies" );
            }

            // 버전 관리 — 사용자가 고른 파일만 잠그고 푼다(공급자가 없으면 메뉴가 꺼져 있다).
            EditorContext* pContext = EditorContext::get();
            if ( pContext != nullptr && entry._bIsDirectory == false )
            {
                EditorSourceControl& sourceControl = pContext->getSourceControl();
                const bool           bCanLock      = sourceControl.getProvider().canLock();
                const bool           bLocked       = sourceControl.findLock( entry._absolutePath ) != nullptr;
                ImGui::Separator();
                if ( ImGui::MenuItem( EditorThemeUtil::makeIconLabel( editoricon::kLock, "Check Out (Lock)" ), nullptr, false, bCanLock && bLocked == false ) )
                    (void)sourceControl.requestLock( entry._absolutePath );
                if ( ImGui::MenuItem( EditorThemeUtil::makeIconLabel( editoricon::kUnlock, "Release Lock" ), nullptr, false, bCanLock && bLocked ) )
                    (void)sourceControl.requestUnlock( entry._absolutePath );
                if ( ImGui::MenuItem( "Refresh Source Control", nullptr, false, bCanLock ) )
                    sourceControl.requestRefresh();
            }

            ImGui::Separator();
            // 지우기 전에 확인한다(휴지통이 아니라 되돌릴 수 없다). 모달은 이 메뉴 밖, 창 단위에서 연다.
            if ( ImGui::MenuItem( "Delete..." ) )
                requestDeleteAsset( entry._absolutePath );
            ImGui::EndPopup();
        }
    }

    ContentBrowserPanel::ContentBrowserPanel() noexcept
        : _listRoot{}
        , _listEntry{}
        , _listHistory{}
        , _selectedFolderAbs{}
        , _listCrumb{}
        , _selectedAssetAbs{}
        , _pendingDeleteAbs{}
        , _referenceQueryID{}
        , _listReferenceResult{}
        , _referenceIndex{}
        , _referenceIndexJob{}
        , _referenceIndexSerial{ 0 }
        , _searchBuffer{}
        , _seenContentChangeSerial{ 0 }
        , _tileSize{ 96.0f }
        , _filterIndex{ 0 }
        , _historyIndex{ -1 }
        , _viewMode{ ViewMode::Tiles }
        , _pendingImportMutex{}
        , _listPendingImportPath{}
        , _folderJob{}
        , _folderCache{ &EditorAssetCommands::collectChildFolders }
        , _bRootsDirty{ SW_TRUE }
        , _bFolderDirty{ SW_TRUE }
        , _bOpenDeleteConfirm{ SW_FALSE }
        , _bShowAllPacks{ SW_FALSE }
        , _bShowAllPacksInitialized{ SW_FALSE }
        , _bReferenceIndexRequested{ SW_FALSE }
        , _bOpenReferenceResults{ SW_FALSE }
        , _bReferenceQueryDependencies{ SW_FALSE }
    {
    }

    void ContentBrowserPanel::requestDeleteAsset( string_view absolutePath )
    {
        _pendingDeleteAbs   = string{ absolutePath };
        _bOpenDeleteConfirm = SW_TRUE;
    }

    bool ContentBrowserPanel::confirmDeleteAsset()
    {
        if ( _pendingDeleteAbs.empty() )
            return false;
        const bool bDeleted = EditorAssetCommands::deleteAsset( _pendingDeleteAbs );
        if ( bDeleted )
        {
            // 지운 파일이 목록 · 선택에 남지 않게 지금 폴더를 다시 읽는다. 파일 감시도 같은 변경을 알리지만 한두 프레임 늦다.
            _selectedAssetAbs.clear();
            _bFolderDirty = SW_TRUE;
            _folderCache.clear();
        }
        _pendingDeleteAbs.clear();
        return bDeleted;
    }

    void ContentBrowserPanel::drawDeleteConfirmModal()
    {
        if ( _bOpenDeleteConfirm == SW_TRUE )
        {
            ImGui::OpenPopup( ContentBrowserPanelInternal::kDeleteConfirmPopupName );
            _bOpenDeleteConfirm = SW_FALSE;
        }
        if ( ImGui::BeginPopupModal( ContentBrowserPanelInternal::kDeleteConfirmPopupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize ) == false )
            return;

        ImGui::Text( "Delete '%s'? This cannot be undone.", FileUtil::getFileNamePart( _pendingDeleteAbs ).c_str() );
        // 지우기 전에 이것을 쓰는 곳을 보인다(언리얼 Delete Assets 대화상자의 참조 수와 같다).
        if ( _referenceIndex.isReady() )
        {
            const uint32 referrerCount = _referenceIndex.countReferrerFiles( ResourceUtil::toResourceID( _pendingDeleteAbs ) );
            if ( referrerCount > 0 )
                ImGui::TextColored( ImVec4( 1.0f, 0.7f, 0.25f, 1.0f ), "%s Used by %u file(s). They will name a missing asset.", editoricon::kWarning, referrerCount );
            else
                ImGui::TextDisabled( "No text asset names it." );
        }
        else
        {
            ImGui::TextDisabled( "The reference index is still building." );
        }
        ImGui::Separator();
        if ( ImGui::Button( "Delete" ) )
        {
            (void)confirmDeleteAsset(); // 실패는 deleteAsset 이 알린다(파일과 .meta 를 그대로 둔다)
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if ( ImGui::Button( "Cancel" ) || ImGui::IsKeyPressed( ImGuiKey_Escape ) )
        {
            _pendingDeleteAbs.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void ContentBrowserPanel::syncWithContentChanges()
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;
        const uint64 serial = pContext->getAssetHotReload().getContentChangeSerial();
        if ( serial == _seenContentChangeSerial )
            return;
        _seenContentChangeSerial = serial;
        _bFolderDirty            = SW_TRUE;
        _folderCache.clear();
    }

    void ContentBrowserPanel::syncReferenceIndex()
    {
        EditorReferenceIndexData data{};
        if ( _referenceIndexJob.take( data ) )
        {
            SW_LOG_INFO( "Reference index ready: %# text asset(s), %# reference(s) in %# ms", data._scannedFileCount, data._listReference.size(), data._scanMilliseconds );
            _referenceIndex.assign( std::move( data ) );
        }

        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr || _referenceIndexJob.isPending() )
            return;
        // 훑는 동안 바뀐 것은 끝난 뒤 번호가 달라 다시 훑는다. 저장이 몰려도 훑기는 하나씩 돈다.
        const uint64 serial = pContext->getAssetHotReload().getContentChangeSerial();
        if ( _bReferenceIndexRequested == SW_TRUE && serial == _referenceIndexSerial )
            return;
        _referenceIndexSerial     = serial;
        _bReferenceIndexRequested = SW_TRUE;
        _referenceIndexJob.request( ResourceUtil::getRootFolderPath() );
    }

    void ContentBrowserPanel::showReferences( string_view absolutePath, bool bDependencies )
    {
        _referenceQueryID            = ResourceUtil::toResourceID( absolutePath );
        _bReferenceQueryDependencies = bDependencies ? SW_TRUE : SW_FALSE;
        _bOpenReferenceResults       = SW_TRUE;
        if ( bDependencies )
            _referenceIndex.findDependencies( _referenceQueryID, _listReferenceResult );
        else
            _referenceIndex.findReferrers( _referenceQueryID, _listReferenceResult );
    }

    void ContentBrowserPanel::drawReferenceResults()
    {
        if ( _bOpenReferenceResults == SW_TRUE )
        {
            ImGui::OpenPopup( ContentBrowserPanelInternal::kReferenceResultsPopupName );
            _bOpenReferenceResults = SW_FALSE;
            // 마우스 위치에 열면 아래 도킹 칸에서 표가 화면 밖으로 나간다. 주 뷰포트 가운데에 연다.
            ImGui::SetNextWindowPos( ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2( 0.5f, 0.5f ) );
        }
        if ( ImGui::BeginPopup( ContentBrowserPanelInternal::kReferenceResultsPopupName ) == false )
            return;

        const bool bDependencies = _bReferenceQueryDependencies == SW_TRUE;
        ImGui::TextUnformatted( bDependencies ? "Dependencies of" : "References to" );
        ImGui::SameLine();
        ImGui::TextColored( ImVec4( 0.55f, 0.8f, 1.0f, 1.0f ), "%s", _referenceQueryID.c_str() );
        if ( _referenceIndex.isReady() == false )
        {
            ImGui::TextDisabled( "Indexing references. Try again in a moment." );
            ImGui::EndPopup();
            return;
        }
        if ( _listReferenceResult.empty() )
        {
            EditorWidgets::drawEmptyHint( bDependencies ? "This asset names no other asset." : "No text asset names this asset." );
            ImGui::EndPopup();
            return;
        }

        constexpr ImGuiTableFlags kTableFlags  = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
        const float32             dpiScale     = EditorThemeUtil::getDpiScale();
        const size_t              visibleCount = std::min( _listReferenceResult.size(), ContentBrowserPanelInternal::kReferenceVisibleRowCount );
        const float32             tableHeight  = ImGui::GetTextLineHeightWithSpacing() * static_cast<float32>( visibleCount + 2 );
        string                    openPath;
        if ( ImGui::BeginTable( "##cb_references", 2, kTableFlags, ImVec2( 560.0f * dpiScale, tableHeight ) ) )
        {
            ImGui::TableSetupColumn( "Asset", ImGuiTableColumnFlags_WidthStretch );
            ImGui::TableSetupColumn( "Line", ImGuiTableColumnFlags_WidthFixed, 48.0f * dpiScale );
            ImGui::TableHeadersRow();
            for ( size_t rowIndex = 0; rowIndex < _listReferenceResult.size(); ++rowIndex )
            {
                const EditorAssetReference& reference = _listReferenceResult[rowIndex];
                const string&               shownPath = bDependencies ? reference._targetPath : reference._referrerPath;
                ImGui::PushID( static_cast<int32>( rowIndex ) );
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex( 0 );
                ImGui::Selectable( shownPath.c_str(), false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick );
                if ( ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
                    openPath = shownPath;
                EditorWidgets::drawTooltip( "Double-click to open" );
                ImGui::TableSetColumnIndex( 1 );
                ImGui::Text( "%u", reference._line );
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::EndPopup();
        if ( openPath.empty() == false )
            (void)EditorAssetCommands::openPath( openPath ); // 실패는 openPath 가 알린다
    }

    void ContentBrowserPanel::openFolder( string_view absolutePath )
    {
        vector<ContentBrowserCrumb> listCrumb;
        makeTrailForFolder( absolutePath, listCrumb );
        selectFolder( absolutePath, listCrumb );
    }

    void ContentBrowserPanel::drawContent()
    {
        // 마우스 X1/X2 버튼(뒤로/앞으로)과 단축키(Alt+Left/Right, Backspace)로 폴더를 이동한다
        if ( ImGui::IsWindowHovered( ImGuiHoveredFlags_RootAndChildWindows ) )
        {
            if ( ImGui::IsKeyPressed( ImGuiKey_MouseX1 ) )
                navigateBack();
            else if ( ImGui::IsKeyPressed( ImGuiKey_MouseX2 ) )
                navigateForward();
        }

        if ( ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows ) && ImGui::IsAnyItemActive() == false )
        {
            const ImGuiIO& io = ImGui::GetIO();
            if ( ( io.KeyAlt && ImGui::IsKeyPressed( ImGuiKey_LeftArrow ) ) || ImGui::IsKeyPressed( ImGuiKey_Backspace ) )
                navigateBack();
            else if ( io.KeyAlt && ImGui::IsKeyPressed( ImGuiKey_RightArrow ) )
                navigateForward();
        }

        if ( _bRootsDirty == SW_TRUE )
            refreshRoots();
        processPendingImports();
        syncWithContentChanges();
        syncReferenceIndex();
        if ( _bFolderDirty == SW_TRUE )
            refreshCurrentFolder();
        vector<EditorFolderListingEntry> listNewEntry;
        if ( _folderJob.take( listNewEntry ) )
            applyFolderListing( listNewEntry );

        drawToolbar();
        ImGui::Separator();

        drawSourcesSection();
        ImGui::SameLine();
        drawAssetView();
        drawDeleteConfirmModal();
        drawReferenceResults();
    }

    void ContentBrowserPanel::refreshRoots()
    {
        _listRoot.clear();

        const auto addRoot = [this]( string_view name, string_view path )
        {
            if ( path.empty() )
                return;
            if ( FileUtil::isDirectory( path ) == false )
                return;
            ContentRoot root;
            root._displayName  = string{ name };
            root._absolutePath = FileUtil::normalizeSeparators( path );
            _listRoot.push_back( std::move( root ) );
        };

        // 기본은 활성 게임 팩 하나다(언리얼 Content Browser 가 프로젝트 콘텐츠만 보이는 것과 같다). All packs 를 켜면 game/ 의 팩마다 루트 하나.
        if ( _bShowAllPacksInitialized == SW_FALSE )
        {
            _bShowAllPacks            = getPreferences<EditorContentBrowserPreferences>()._bShowAllPacksByDefault ? SW_TRUE : SW_FALSE;
            _bShowAllPacksInitialized = SW_TRUE;
        }
        const string& packRoot = GameConfig::getActive()._packRoot;
        if ( packRoot.empty() == false )
            addRoot( packRoot, ResourceUtil::getDomainFolderPath( packRoot ) );
        if ( _bShowAllPacks == SW_TRUE || packRoot.empty() )
        {
            vector<string> listPackFolder;
            (void)FileUtil::collectFolders( ResourceUtil::getDomainFolderPath( "game" ), listPackFolder, false ); // game/ 이 없으면 팩 루트가 없다
            std::sort( listPackFolder.begin(), listPackFolder.end() );
            for ( const string& packFolder : listPackFolder )
            {
                const string packName = string( ContentBrowserPanelInternal::kGameDomainPrefix ) + FileUtil::normalizePath( FileUtil::getFileNamePart( packFolder ) );
                if ( packName != FileUtil::normalizePath( packRoot ) )
                    addRoot( packName, packFolder );
            }
        }
        addRoot( "engine", ResourceUtil::getDomainFolderPath( "engine" ) );
        addRoot( "common", ResourceUtil::getDomainFolderPath( "common" ) );
        addRoot( "editor", ResourceUtil::getDomainFolderPath( "editor" ) );

        // 지금 폴더가 숨긴 팩 안이면 첫 루트로 간다.
        bool bSelectedIsUnderRoot = false;
        for ( const ContentRoot& root : _listRoot )
        {
            if ( FileUtil::startsWithPathComponent( FileUtil::normalizePath( _selectedFolderAbs ), FileUtil::normalizePath( root._absolutePath ) ) )
            {
                bSelectedIsUnderRoot = true;
                break;
            }
        }
        if ( bSelectedIsUnderRoot == false && _listRoot.empty() == false )
        {
            vector<ContentBrowserCrumb> listCrumb;
            makeTrailForFolder( _listRoot.front()._absolutePath, listCrumb );
            selectFolder( _listRoot.front()._absolutePath, listCrumb );
        }

        _bRootsDirty = SW_FALSE;
    }

    void ContentBrowserPanel::setShowAllPacks( bool bShowAllPacks )
    {
        _bShowAllPacks            = bShowAllPacks ? SW_TRUE : SW_FALSE;
        _bShowAllPacksInitialized = SW_TRUE;
        _bRootsDirty              = SW_TRUE;
    }

    uint32 ContentBrowserPanel::getGameRootCount() const
    {
        uint32 gameRootCount{ 0 };
        for ( const ContentRoot& root : _listRoot )
        {
            if ( StringUtil::startsWith( root._displayName, ContentBrowserPanelInternal::kGameDomainPrefix ) )
                ++gameRootCount;
        }
        return gameRootCount;
    }

    bool ContentBrowserPanel::hasRoot( string_view displayName ) const
    {
        for ( const ContentRoot& root : _listRoot )
        {
            if ( FileUtil::normalizePath( root._displayName ) == FileUtil::normalizePath( displayName ) )
                return true;
        }
        return false;
    }

    void ContentBrowserPanel::refreshCurrentFolder()
    {
        _folderJob.request( _selectedFolderAbs );
        _bFolderDirty = SW_FALSE;
    }

    void ContentBrowserPanel::applyFolderListing( vector<EditorFolderListingEntry>& listEntry )
    {
        // 목록을 보기만 한다 — `.meta` 를 쓰지 않는다. GUID 는 그 에셋을 쓰는 시스템(머티리얼 캐시 · 프리팹 · 씬 저장 · 임포트)이 만든다.
        _listEntry = std::move( listEntry );

        std::sort( _listEntry.begin(), _listEntry.end(), []( const AssetEntry& entryA, const AssetEntry& entryB )
        {
            if ( entryA._bIsDirectory != entryB._bIsDirectory )
                return entryA._bIsDirectory > entryB._bIsDirectory;
            return entryA._name < entryB._name;
        } );
    }

    bool ContentBrowserPanel::passesTypeFilter( const AssetEntry& entry ) const
    {
        if ( entry._bIsDirectory )
            return true;

        const utf8* pPath = entry._name.c_str();

        uint32                          filterCount{ 0 };
        const EditorAssetBrowserFilter* pFilter = EditorAssetTypeRegistry::getBrowserFilters( filterCount );
        if ( _filterIndex >= filterCount )
            return true;

        const EditorAssetBrowserFilter& filter = pFilter[_filterIndex];
        if ( filter._bOther )
            return EditorAssetTypeRegistry::matchesOther( pPath );
        if ( filter._kind == EditorAssetType::Unknown )
            return true;
        return EditorAssetTypeRegistry::matches( filter._kind, pPath );
    }

    bool ContentBrowserPanel::passesSearchFilter( const AssetEntry& entry ) const
    {
        return EditorListFilter{ _searchBuffer.c_str() }.matches( entry._name );
    }

    void ContentBrowserPanel::drawToolbar()
    {
        if ( EditorChrome::beginToolbar( "##cb_toolbar" ) )
        {
            const bool bCanBack = canNavigateBack();
            if ( bCanBack == false )
                ImGui::BeginDisabled();
            if ( ImGui::Button( "<##cb_nav_back" ) )
                navigateBack();
            if ( bCanBack == false )
                ImGui::EndDisabled();
            EditorWidgets::drawTooltip( "이전 폴더로 이동 (마우스 뒤로가기 버튼 / Alt+Left / Backspace)" );

            ImGui::SameLine();
            const bool bCanForward = canNavigateForward();
            if ( bCanForward == false )
                ImGui::BeginDisabled();
            if ( ImGui::Button( ">##cb_nav_forward" ) )
                navigateForward();
            if ( bCanForward == false )
                ImGui::EndDisabled();
            EditorWidgets::drawTooltip( "다음 폴더로 이동 (마우스 앞으로가기 버튼 / Alt+Right)" );

            ImGui::SameLine();
            EditorWidgets::drawSearchField( "##cb_search", _searchBuffer, "Search Content", 160.0f, false );
            EditorSelfTestMarks::note( "contentBrowser.search" );
            EditorWidgets::drawTooltip( "에셋 이름 또는 확장자로 필터링하여 검색합니다" );

            ImGui::SameLine();
            uint32                          filterCount{ 0 };
            const EditorAssetBrowserFilter* pFilter  = EditorAssetTypeRegistry::getBrowserFilters( filterCount );
            const utf8*                     pPreview = "All";
            if ( _filterIndex < filterCount )
                pPreview = pFilter[_filterIndex]._pLabel;
            ImGui::SetNextItemWidth( 110.0f * EditorThemeUtil::getDpiScale() );
            if ( ImGui::BeginCombo( "##cb_type", pPreview ) )
            {
                for ( uint32 filterIdx = 0; filterIdx < filterCount; ++filterIdx )
                {
                    const bool bSelected = ( _filterIndex == filterIdx );
                    if ( ImGui::Selectable( pFilter[filterIdx]._pLabel, bSelected ) )
                        _filterIndex = filterIdx;
                    if ( bSelected )
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            EditorWidgets::drawTooltip( "특정 에셋 종류(텍스처, 씬, 프리팹, 사운드 등)별로 필터링합니다" );

            ImGui::SameLine();
            if ( ImGui::RadioButton( "Tiles", _viewMode == ViewMode::Tiles ) )
                _viewMode = ViewMode::Tiles;
            EditorWidgets::drawTooltip( "에셋을 사각형 썸네일 카드(타일) 형태로 표시합니다" );

            ImGui::SameLine();
            if ( ImGui::RadioButton( "List", _viewMode == ViewMode::List ) )
                _viewMode = ViewMode::List;
            EditorWidgets::drawTooltip( "에셋을 이름, 종류, 경로 세부 리스트 목록으로 표시합니다" );

            if ( _viewMode == ViewMode::Tiles )
            {
                ImGui::SameLine();
                ImGui::SetNextItemWidth( 80.0f * EditorThemeUtil::getDpiScale() );
                ImGui::SliderFloat( "##cb_tile", &_tileSize, 64.0f, 160.0f, "%.0f" );
                EditorWidgets::drawTooltip( "타일 썸네일의 크기를 조절합니다 (64px ~ 160px)" );
            }

            ImGui::SameLine();
            const bool canImport = _selectedFolderAbs.empty() == false;
            if ( canImport == false )
                ImGui::BeginDisabled();
            const bool importClicked = ImGui::Button( "Import..." );
            if ( canImport == false )
            {
                ImGui::EndDisabled();
                EditorWidgets::drawTooltip( "에셋을 가져올 대상 폴더를 먼저 선택하세요" );
            }
            else
            {
                EditorWidgets::drawTooltip( "외부 파일(텍스처, 셰이더, 사운드 등)을 현재 폴더로 가져옵니다" );
            }
            if ( importClicked )
                importFilesFromDialog();

            ImGui::SameLine();
            if ( ImGui::Button( "Refresh" ) )
            {
                _folderCache.clear();
                _listEntry.clear();
                _selectedAssetAbs.clear();
                refreshRoots();
                refreshCurrentFolder();
            }
            EditorWidgets::drawTooltip( "디스크 파일 및 리소스 루트 목록을 새로고침합니다" );

            ImGui::SameLine();
            bool bShowAllPacks = _bShowAllPacks == SW_TRUE;
            if ( ImGui::Checkbox( "All packs", &bShowAllPacks ) )
                setShowAllPacks( bShowAllPacks );
            EditorSelfTestMarks::note( "contentBrowser.allPacks" );
            EditorWidgets::drawTooltip( "끄면 활성 게임 팩만, 켜면 game/ 의 모든 팩을 보입니다(기본값은 환경설정 Content Browser)" );
        }
        EditorChrome::endToolbar();
    }

    void ContentBrowserPanel::drawSourcesSection()
    {
        editor::EditorSectionDesc sourcesDesc{};
        sourcesDesc._pID       = "##cb_sources";
        sourcesDesc._kind      = editor::EditorSectionKind::Child;
        sourcesDesc._childSize = float2{ 220.0f, 0.0f };
        // 오른쪽 Assets 칸과 같은 높이로 — 아래 개수 줄(drawCountLabel)이 창 안에 남는다. 높이 0(남은 전부)이면 그 줄이 창 밖으로 밀려 패널 전체가 스크롤된다.
        sourcesDesc._flags = editor::EditorSectionFlags::Border | editor::EditorSectionFlags::ResizeX | editor::EditorSectionFlags::FillRemaining;
        EditorChrome::beginSection( sourcesDesc );

        EditorWidgets::drawSectionHeader( "Favorites" );
        struct FavFolder
        {
            const utf8* _pLabel;
            const utf8* _pRelPath;
            bool        _bEngine;
        };
        static const FavFolder kArrFavorites[] = {
            {  "Scenes",    path::kMapsFolder, false},
            { "Prefabs", path::kPrefabsFolder, false},
            {"Textures", path::kTextureFolder, false},
            { "Shaders",  path::kShaderFolder,  true},
            {    "Data",    path::kDataFolder, false}
        };

        const uint32 favoriteCount = static_cast<uint32>( sizeof( kArrFavorites ) / sizeof( kArrFavorites[0] ) );
        for ( uint32 favIdx = 0; favIdx < favoriteCount; ++favIdx )
        {
            const string fullFavPath = kArrFavorites[favIdx]._bEngine
                                         ? ResourceUtil::getDomainFolderPath( "engine", kArrFavorites[favIdx]._pRelPath )
                                         : ResourceUtil::getDomainFolderPath( GameConfig::getActive()._packRoot, kArrFavorites[favIdx]._pRelPath );
            const bool   bSelected   = FileUtil::pathsEqualNormalized( fullFavPath, _selectedFolderAbs );

            if ( ImGui::Selectable( kArrFavorites[favIdx]._pLabel, bSelected ) )
            {
                vector<ContentBrowserCrumb> listCrumb;
                ContentBrowserLogic::makeFavoriteTrail( kArrFavorites[favIdx]._pLabel, fullFavPath, listCrumb );
                selectFolder( fullFavPath, listCrumb );
            }
        }

        ImGui::Separator();
        EditorWidgets::drawSectionHeader( "Sources" );

        for ( const ContentRoot& root : _listRoot )
        {
            drawFolderTreeNode( root._absolutePath.c_str(), root._displayName, 0 );
        }

        if ( _listRoot.empty() )
            EditorWidgets::drawEmptyHint( "No resource roots found." );

        EditorChrome::endSection();
    }

    void ContentBrowserPanel::drawFolderTreeNode( string_view folderPath, string_view label, int32 depth )
    {
        const string absPath  = FileUtil::normalizeSeparators( folderPath );
        const bool   selected = FileUtil::pathsEqualNormalized( absPath, _selectedFolderAbs );

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if ( selected )
            flags |= ImGuiTreeNodeFlags_Selected;
        if ( depth == 0 )
            flags |= ImGuiTreeNodeFlags_DefaultOpen;

        // 하위 폴더는 처음 그릴 때 한 번만 디스크에서 읽는다(Refresh · 파일 감시가 비운다). 참조는 재귀 중에도 산다.
        const vector<string>& listChild    = _folderCache.getOrScanChildFolders( absPath );
        const bool            hasChildDirs = listChild.empty() == false;
        if ( hasChildDirs == false )
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

        ImGui::PushID( absPath.c_str() );
        const bool   bDefaultOpen  = ( ( flags & ImGuiTreeNodeFlags_DefaultOpen ) != 0 );
        const utf8*  pFolderIcon   = EditorThemeUtil::getFolderIcon( bDefaultOpen );
        const string labelWithIcon = string( pFolderIcon ) + "  " + string( label );
        const Color4 folderColor   = EditorThemeUtil::getFolderColor();
        ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( folderColor._r, folderColor._g, folderColor._b, 1.0f ) );
        const bool opened = ImGui::TreeNodeEx( labelWithIcon.c_str(), flags );
        ImGui::PopStyleColor();
        if ( EditorSelfTestMarks::isEnabled() )
            ContentBrowserPanelInternal::noteMark( "contentBrowser.folder.", ResourceUtil::toResourceID( absPath ) );

        if ( ImGui::IsItemClicked() && ImGui::IsItemToggledOpen() == false )
        {
            vector<ContentBrowserCrumb> listCrumb;
            makeTrailForFolder( absPath, listCrumb );
            selectFolder( absPath, listCrumb );
        }

        if ( opened && hasChildDirs )
        {
            for ( const string& child : listChild )
            {
                drawFolderTreeNode( child, FileUtil::getFileNamePart( child ), depth + 1 );
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    void ContentBrowserPanel::drawAssetView()
    {
        // **엔트리를 복사하지 않는다.** `AssetEntry` 는 `string` 이 넷이라, 복사하면 애셋이
        // 수백 개일 때 프레임마다 문자열 수천 개를 복사한다. 보는 쪽 둘은 읽기만 하므로 포인터로
        // 충분하고, 버퍼도 멤버로 올려 두면 첫 프레임 뒤로는 할당이 없다.
        vector<const AssetEntry*>& listVisible = _listVisibleEntry;
        listVisible.clear();
        listVisible.reserve( _listEntry.size() );
        for ( const AssetEntry& entry : _listEntry )
        {
            if ( passesTypeFilter( entry ) == false || passesSearchFilter( entry ) == false )
                continue;
            listVisible.push_back( &entry );
        }

        editor::EditorSectionDesc assetsDesc{};
        assetsDesc._pID   = "##cb_assets";
        assetsDesc._kind  = editor::EditorSectionKind::Child;
        assetsDesc._flags = editor::EditorSectionFlags::Border | editor::EditorSectionFlags::FillRemaining;
        EditorChrome::beginSection( assetsDesc );

        drawBreadcrumbs();
        ImGui::Separator();

        // 걸러져서 0건인 것과 폴더가 정말 빈 것은 다르게 말해 준다.
        const EditorListFilter filter{ _searchBuffer.c_str() };
        if ( listVisible.empty() && filter.isActive() )
            EditorWidgets::drawNoSearchResultHint( filter.getText() );
        else if ( _viewMode == ViewMode::Tiles )
            drawTilesView( listVisible );
        else
            drawListView( listVisible );

        EditorChrome::endSection();

        EditorWidgets::drawCountLabel( static_cast<uint32>( listVisible.size() ), 0, "items" );
        if ( _referenceIndexJob.isPending() && _referenceIndex.isReady() == false )
        {
            ImGui::SameLine();
            ImGui::TextDisabled( "|  Indexing references..." );
        }
        if ( _selectedAssetAbs.empty() == false )
        {
            ImGui::SameLine();
            ImGui::TextDisabled( "|  %s", _selectedAssetAbs.c_str() );
        }
    }

    void ContentBrowserPanel::drawBreadcrumbs()
    {
        if ( _listCrumb.empty() )
        {
            EditorWidgets::drawEmptyHint( "Select a folder" );
            return;
        }

        // 조각마다 절대 경로를 든다 — 누르면 그 조각까지의 경로 줄과 함께 그 폴더로 간다. 경로가 빈 조각("Favorites")은 누를 수 없다.
        size_t clickedIndex = _listCrumb.size();
        for ( size_t crumbIndex = 0; crumbIndex < _listCrumb.size(); ++crumbIndex )
        {
            const ContentBrowserCrumb& crumb = _listCrumb[crumbIndex];
            if ( crumbIndex > 0 )
            {
                ImGui::SameLine();
                ImGui::TextUnformatted( "/" );
                ImGui::SameLine();
            }

            ImGui::PushID( static_cast<int32>( crumbIndex ) );
            const bool bIsLast = ( crumbIndex + 1 == _listCrumb.size() );
            if ( bIsLast )
            {
                const Color4 accentColor = EditorThemeUtil::getAccentColor();
                ImGui::TextColored( ImVec4( accentColor._r, accentColor._g, accentColor._b, 1.0f ), "%s", editoricon::kFolderOpen );
                ImGui::SameLine();
                ImGui::TextUnformatted( crumb._label.c_str() );
            }
            else if ( crumb._absolutePath.empty() )
            {
                ImGui::TextDisabled( "%s", crumb._label.c_str() );
            }
            else if ( ImGui::SmallButton( crumb._label.c_str() ) )
            {
                clickedIndex = crumbIndex;
            }
            ImGui::PopID();
        }

        if ( clickedIndex < _listCrumb.size() )
        {
            const vector<ContentBrowserCrumb> listCrumb( _listCrumb.begin(), _listCrumb.begin() + static_cast<std::ptrdiff_t>( clickedIndex + 1 ) );
            selectFolder( listCrumb.back()._absolutePath, listCrumb );
        }
    }

    void ContentBrowserPanel::drawTilesView( const vector<const AssetEntry*>& listVisible )
    {
        const float32 cell       = _tileSize;
        const float32 paddingX   = ImGui::GetStyle().ItemSpacing.x;
        const float32 paddingY   = ImGui::GetStyle().ItemSpacing.y;
        const float32 panelWidth = ImGui::GetContentRegionAvail().x;
        int32         columns    = static_cast<int32>( ( panelWidth + paddingX ) / ( cell + paddingX ) );
        if ( columns < 1 )
            columns = 1;

        const int32 itemCount = static_cast<int32>( listVisible.size() );
        const int32 rowCount  = ( itemCount + columns - 1 ) / columns;
        // 버튼과 이름 줄(셀 + 이름 줄 수 — drawClampedLabel 은 늘 그 줄 수만큼 자리를 잡는다)
        const float32 rowHeight = cell + ImGui::GetTextLineHeight() * static_cast<float32>( ContentBrowserPanelInternal::kTileLabelLineCount ) +
                                  ImGui::GetStyle().ItemSpacing.y + paddingY;

        ImGuiListClipper clipper;
        clipper.Begin( rowCount, rowHeight );
        while ( clipper.Step() )
        {
            for ( int32 row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row )
            {
                for ( int32 column = 0; column < columns; ++column )
                {
                    const int32 index = row * columns + column;
                    if ( index >= itemCount )
                        break;

                    const AssetEntry& entry = *listVisible[static_cast<size_t>( index )];
                    ImGui::PushID( entry._absolutePath.c_str() );

                    if ( column > 0 )
                        ImGui::SameLine();

                    const bool   selected = FileUtil::pathsEqualNormalized( entry._absolutePath, _selectedAssetAbs ) || ( entry._bIsDirectory && FileUtil::pathsEqualNormalized( entry._absolutePath, _selectedFolderAbs ) );
                    const Color4 accent   = EditorThemeUtil::getAccentColor();
                    const Color4 frameBg  = EditorThemeUtil::getFrameBgColor();
                    ImGui::PushStyleColor( ImGuiCol_Button, selected ? ImVec4( accent._r, accent._g, accent._b, 0.40f ) : ImVec4( frameBg._r, frameBg._g, frameBg._b, 0.60f ) );
                    ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( accent._r, accent._g, accent._b, 0.60f ) );

                    ImGui::BeginGroup();
                    const ImVec2 cursor = ImGui::GetCursorScreenPos();
                    if ( ImGui::Button( "##tile", ImVec2( cell, cell ) ) )
                        selectAsset( entry );
                    ContentBrowserPanelInternal::noteMark( "contentBrowser.asset.", entry._name );
                    drawAssetContextMenu( entry );
                    if ( ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
                        openAsset( entry );
                    if ( entry._bIsDirectory == false )
                        EditorWidgets::drawAssetDragSource( entry._relativePath.c_str(), true );

                    ImDrawList*       pDrawList = ImGui::GetWindowDrawList();
                    constexpr float32 inset     = 6.0f;
                    drawAssetThumbnail( pDrawList, float2{ cursor.x + inset, cursor.y + inset },
                                        float2{ cursor.x + cell - inset, cursor.y + cell * 0.65f }, entry );

                    drawSourceControlBadge( pDrawList, float2{ cursor.x + cell - inset, cursor.y + inset }, entry );

                    EditorWidgets::drawClampedLabel( entry._name, cell, ContentBrowserPanelInternal::kTileLabelLineCount );
                    ImGui::EndGroup();

                    ImGui::PopStyleColor( 2 );
                    ImGui::PopID();
                }
            }
        }
    }

    void ContentBrowserPanel::drawListView( const vector<const AssetEntry*>& listVisible )
    {
        constexpr ImGuiTableFlags flags =
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;

        if ( ImGui::BeginTable( "##cb_list", 3, flags, ImGui::GetContentRegionAvail() ) )
        {
            ImGui::TableSetupColumn( "Name", ImGuiTableColumnFlags_WidthStretch );
            ImGui::TableSetupColumn( "Type", ImGuiTableColumnFlags_WidthFixed, 90.0f * EditorThemeUtil::getDpiScale() );
            ImGui::TableSetupColumn( "Path", ImGuiTableColumnFlags_WidthStretch );
            ImGui::TableHeadersRow();

            ImGuiListClipper clipper;
            clipper.Begin( static_cast<int32>( listVisible.size() ) );
            while ( clipper.Step() )
            {
                for ( int32 itemIndex = clipper.DisplayStart; itemIndex < clipper.DisplayEnd; ++itemIndex )
                {
                    const AssetEntry& entry = *listVisible[static_cast<size_t>( itemIndex )];
                    ImGui::PushID( entry._absolutePath.c_str() );
                    ImGui::TableNextRow();

                    const bool selected = FileUtil::pathsEqualNormalized( entry._absolutePath, _selectedAssetAbs );
                    ImGui::TableSetColumnIndex( 0 );
                    const utf8*  pAssetIcon   = EditorThemeUtil::getAssetIconForPath( entry._name, entry._bIsDirectory );
                    const Color4 assetColor   = EditorThemeUtil::getAssetColorForPath( entry._name, entry._bIsDirectory );
                    const string statusText   = describeSourceControlStatus( entry );
                    const string nameWithIcon = string( pAssetIcon ) + "  " + entry._name + ( statusText.empty() ? string() : string( "  " ) + editoricon::kLock );
                    ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( assetColor._r, assetColor._g, assetColor._b, assetColor._a ) );
                    const bool bSelected = ImGui::Selectable( nameWithIcon.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick );
                    ImGui::PopStyleColor();
                    if ( bSelected )
                    {
                        selectAsset( entry );
                        if ( ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
                            openAsset( entry );
                    }
                    if ( statusText.empty() == false && ImGui::IsItemHovered() )
                        ImGui::SetTooltip( "%s", statusText.c_str() );
                    ContentBrowserPanelInternal::noteMark( "contentBrowser.asset.", entry._name );
                    drawAssetContextMenu( entry );
                    if ( entry._bIsDirectory == false )
                        EditorWidgets::drawAssetDragSource( entry._relativePath.c_str() );

                    ImGui::TableSetColumnIndex( 1 );
                    ImGui::TextColored( ContentBrowserPanelInternal::colorForAsset( entry._name, entry._bIsDirectory ),
                                        "%s", ContentBrowserPanelInternal::typeLabel( entry._name, entry._bIsDirectory ) );

                    ImGui::TableSetColumnIndex( 2 );
                    ImGui::TextUnformatted( entry._relativePath.c_str() );

                    ImGui::PopID();
                }
            }

            ImGui::EndTable();
        }
    }

    void ContentBrowserPanel::navigateBack()
    {
        if ( canNavigateBack() )
        {
            --_historyIndex;
            const HistoryEntry& entry = _listHistory[static_cast<size_t>( _historyIndex )];
            selectFolder( entry._folderPathAbs, entry._listCrumb, false );
        }
    }

    void ContentBrowserPanel::navigateForward()
    {
        if ( canNavigateForward() )
        {
            ++_historyIndex;
            const HistoryEntry& entry = _listHistory[static_cast<size_t>( _historyIndex )];
            selectFolder( entry._folderPathAbs, entry._listCrumb, false );
        }
    }

    void ContentBrowserPanel::makeTrailForFolder( string_view folderAbs, vector<ContentBrowserCrumb>& outListCrumb ) const
    {
        const string folderNorm = FileUtil::normalizePath( folderAbs );
        for ( const ContentRoot& root : _listRoot )
        {
            if ( FileUtil::startsWithPathComponent( folderNorm, FileUtil::normalizePath( root._absolutePath ) ) )
            {
                ContentBrowserLogic::makeFolderTrail( root._displayName, root._absolutePath, folderAbs, outListCrumb );
                return;
            }
        }
        ContentBrowserLogic::makeFolderTrail( {}, {}, folderAbs, outListCrumb );
    }

    void ContentBrowserPanel::selectFolder( string_view absolutePath, const vector<ContentBrowserCrumb>& listCrumb, bool bRecordHistory )
    {
        const string normalizedPath = FileUtil::normalizeSeparators( absolutePath );
        if ( bRecordHistory )
        {
            const bool bSameAsCurrent = ( _historyIndex >= 0 &&
                                          _historyIndex < static_cast<int32>( _listHistory.size() ) &&
                                          FileUtil::pathsEqualNormalized( _listHistory[static_cast<size_t>( _historyIndex )]._folderPathAbs, normalizedPath ) );
            if ( bSameAsCurrent == false )
            {
                if ( _historyIndex >= 0 && _historyIndex + 1 < static_cast<int32>( _listHistory.size() ) )
                    _listHistory.erase( _listHistory.begin() + ( _historyIndex + 1 ), _listHistory.end() );
                HistoryEntry newEntry;
                newEntry._folderPathAbs = normalizedPath;
                newEntry._listCrumb     = listCrumb;
                _listHistory.push_back( std::move( newEntry ) );

                // **가장 오래된 것부터 버린다.** 상한이 없으면 세션이 길수록 계속 쌓인다.
                if ( _listHistory.size() > kMaxHistoryCount )
                {
                    const size_t dropCount = _listHistory.size() - kMaxHistoryCount;
                    _listHistory.erase( _listHistory.begin(), _listHistory.begin() + static_cast<std::ptrdiff_t>( dropCount ) );
                }
                _historyIndex = static_cast<int32>( _listHistory.size() ) - 1;
            }
        }

        // 탐색 · I/O 는 실제 파일 시스템의 대소문자를 쓰고, 저장 경로는 ResourceUtil::makeSavePath 가 상대 경로 조각을 소문자로 바꾼다.
        _selectedFolderAbs = normalizedPath;
        _listCrumb         = listCrumb;
        _selectedAssetAbs.clear();
        _bFolderDirty = SW_TRUE;
    }

    void ContentBrowserPanel::selectAsset( const AssetEntry& entry )
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;

        _selectedAssetAbs = entry._absolutePath;
        if ( entry._bIsDirectory == false )
            pContext->getWorkspace().setFocusedAssetPath( entry._relativePath.c_str() );
    }

    void ContentBrowserPanel::openAsset( const AssetEntry& entry )
    {
        if ( entry._bIsDirectory )
        {
            vector<ContentBrowserCrumb> listCrumb = _listCrumb;
            ContentBrowserLogic::appendChildCrumb( entry._absolutePath, listCrumb );
            selectFolder( entry._absolutePath, listCrumb );
            return;
        }

        selectAsset( entry );
        SW_LOG_TRACE( "Open: %#", entry._relativePath.c_str() );
        (void)EditorAssetCommands::openPath( entry._relativePath ); // 실패는 openPath 가 알린다
    }

    void ContentBrowserPanel::importFilesFromDialog()
    {
        if ( _selectedFolderAbs.empty() )
        {
            SW_LOG_WARNING( "Select a destination folder before importing." );
            return;
        }

        FileDialogParams params{};
        params._type                = FileDialogParams::Type::Open;
        params._title               = "Import to Content Browser";
        params._description         = "Assets";
        params._bEnableMultiselect  = true;
        params._initialDirectory    = _selectedFolderAbs;
        params._listFilterExtension = {};
        EditorAssetTypeRegistry::appendImportExtensions( params._listFilterExtension );

        FileUtil::openFileDialog( params, SW_DELEGATE_METHOD( FileDialogDelegate, &ContentBrowserPanel::onImportDialogResult, this ) );
    }

    void ContentBrowserPanel::onImportDialogResult( const vector<string>& listPath )
    {
        std::scoped_lock<mutex> lock{ _pendingImportMutex };
        _listPendingImportPath.insert( _listPendingImportPath.end(), listPath.begin(), listPath.end() );
    }

    void ContentBrowserPanel::processPendingImports()
    {
        vector<string> listPath;
        {
            std::scoped_lock<mutex> lock{ _pendingImportMutex };
            if ( _listPendingImportPath.empty() )
                return;
            listPath.swap( _listPendingImportPath );
        }

        if ( _selectedFolderAbs.empty() )
        {
            SW_LOG_WARNING( "Import cancelled — no destination folder." );
            return;
        }

        if ( EditorAssetCommands::importFiles( _selectedFolderAbs, listPath ) > 0 )
            _bFolderDirty = SW_TRUE;
    }
} // namespace sw::editor
