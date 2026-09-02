#include "pch.h"

#include "Editor/Panels/ContentBrowserPanel.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/mutex.h"
#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Commands/EditorAssetCommands.h"
#include "Editor/Common/Gui/EditorChrome.h"
#include "Editor/Common/Gui/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Common/EngineDefines.h"
#include "Engine/Resource/AssetDatabase.h"
#include "Engine/Resource/ResourceManager.h"
#include "Engine/Resource/ResourceUtil.h"

#include <IconsFontAwesome6.h>
#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct ContentBrowserPanelInternal
        {
            static ImVec4 colorForAsset( string_view path, bool bIsDirectory = false )
            {
                const Color4 c = EditorThemeUtil::getAssetColorForPath( path, bIsDirectory );
                return ImVec4( c._r, c._g, c._b, c._a );
            }

            static const utf8* typeLabel( string_view path, bool bIsDirectory )
            {
                if ( bIsDirectory )
                    return "Folder";
                uint32                          filterCount{ 0 };
                const EditorAssetBrowserFilter* pFilter = EditorAssetTypeRegistry::getBrowserFilters( filterCount );
                for ( uint32 index = 0; index < filterCount; ++index )
                {
                    if ( pFilter[index]._kind == EditorAssetKind::Unknown )
                        continue;
                    if ( EditorAssetTypeRegistry::matches( pFilter[index]._kind, path ) == false )
                        continue;
                    return pFilter[index]._label.data();
                }
                return "File";
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "ContentBrowserPanel" );

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

        // Card thumbnail background
        pDrawList->AddRectFilled( minVec, maxVec, IM_COL32( 22, 24, 30, 255 ), 4.0f );

        const utf8* pPath = entry._absolutePath.empty() ? entry._name.c_str() : entry._absolutePath.c_str();
        if ( entry._bIsDirectory )
        {
            const Color4 folderColor = EditorThemeUtil::getFolderColor();

            // Card subtle border
            pDrawList->AddRect( minVec, maxVec, IM_COL32( 40, 48, 62, 160 ), 4.0f );

            // UE5 Style Layered Folder:
            // 1) Back Tab & Back Plate (Deep Slate Accent)
            const uint32 colBack = IM_COL32(
                static_cast<int32>( folderColor._r * 110 + 15 ),
                static_cast<int32>( folderColor._g * 125 + 20 ),
                static_cast<int32>( folderColor._b * 165 + 30 ),
                255 );
            // 2) Front Pocket (Vibrant Theme Accent)
            const uint32 colFront = IM_COL32(
                static_cast<int32>( folderColor._r * 190 + 20 ),
                static_cast<int32>( folderColor._g * 205 + 25 ),
                static_cast<int32>( folderColor._b * 235 + 20 ),
                255 );
            // 3) Front Top Highlight Lip
            const uint32 colLip = IM_COL32(
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

            // Back Tab
            pDrawList->AddRectFilled( ImVec2( fLeft, fTabTop ), ImVec2( fTabR, fTop + 2.0f ), colBack, 3.0f );
            // Back Body
            pDrawList->AddRectFilled( ImVec2( fLeft, fTop ), ImVec2( fRight, fBottom ), colBack, 3.0f );

            // Front Pocket
            pDrawList->AddRectFilled( ImVec2( fLeft, fPktTop ), ImVec2( fRight, fBottom ), colFront, 3.0f );
            // Front Lip Highlight
            pDrawList->AddLine( ImVec2( fLeft + 2.0f, fPktTop + 1.0f ), ImVec2( fRight - 2.0f, fPktTop + 1.0f ), colLip, 1.5f );
            // Crisp Border
            pDrawList->AddRect( ImVec2( fLeft, fPktTop ), ImVec2( fRight, fBottom ), IM_COL32( 15, 25, 45, 120 ), 3.0f );
        }

        else if ( EditorAssetTypeRegistry::matches( EditorAssetKind::Prefab, pPath ) )
        {
            // 3D Isometric Blue Cube for Prefab
            const float32 sz        = w * 0.22f;
            ImVec2        topPts[4] = { ImVec2( cx, cy - sz * 1.1f ), ImVec2( cx + sz * 0.9f, cy - sz * 0.55f ),
                                        ImVec2( cx, cy ), ImVec2( cx - sz * 0.9f, cy - sz * 0.55f ) };
            pDrawList->AddConvexPolyFilled( topPts, 4, IM_COL32( 90, 160, 255, 255 ) );

            ImVec2 leftPts[4] = { ImVec2( cx - sz * 0.9f, cy - sz * 0.55f ), ImVec2( cx, cy ),
                                  ImVec2( cx, cy + sz * 0.9f ), ImVec2( cx - sz * 0.9f, cy + sz * 0.35f ) };
            pDrawList->AddConvexPolyFilled( leftPts, 4, IM_COL32( 50, 120, 230, 255 ) );

            ImVec2 rightPts[4] = { ImVec2( cx, cy ), ImVec2( cx + sz * 0.9f, cy - sz * 0.55f ),
                                   ImVec2( cx + sz * 0.9f, cy + sz * 0.35f ), ImVec2( cx, cy + sz * 0.9f ) };
            pDrawList->AddConvexPolyFilled( rightPts, 4, IM_COL32( 35, 95, 195, 255 ) );
        }
        else if ( EditorAssetTypeRegistry::matches( EditorAssetKind::Material, pPath ) )
        {
            // 3D Sphere preview with specular shading
            const float32 r = w * 0.26f;
            pDrawList->AddCircleFilled( ImVec2( cx, cy ), r, IM_COL32( 160, 60, 220, 255 ), 24 );
            pDrawList->AddCircleFilled( ImVec2( cx - r * 0.32f, cy - r * 0.32f ), r * 0.35f,
                                        IM_COL32( 230, 180, 255, 200 ), 16 );
            pDrawList->AddCircleFilled( ImVec2( cx - r * 0.38f, cy - r * 0.38f ), r * 0.15f,
                                        IM_COL32( 255, 255, 255, 240 ), 12 );
        }
        else if ( EditorAssetTypeRegistry::matches( EditorAssetKind::Texture, pPath ) )
        {
            // Checkerboard background
            constexpr float32 chk = 6.0f;
            for ( float32 y = minPos._y + 4.0f; y < maxPos._y - 4.0f; y += chk )
            {
                for ( float32 x = minPos._x + 4.0f; x < maxPos._x - 4.0f; x += chk )
                {
                    const bool bDark =
                        ( static_cast<int32>( ( x - minPos._x ) / chk ) +
                          static_cast<int32>( ( y - minPos._y ) / chk ) ) %
                            2 ==
                        0;
                    pDrawList->AddRectFilled( ImVec2( x, y ), ImVec2( x + chk, y + chk ),
                                              bDark ? IM_COL32( 38, 40, 46, 255 ) : IM_COL32( 58, 62, 70, 255 ) );
                }
            }
            // Picture frame
            pDrawList->AddRect( ImVec2( minPos._x + w * 0.16f, minPos._y + h * 0.16f ),
                                ImVec2( minPos._x + w * 0.84f, minPos._y + h * 0.84f ),
                                IM_COL32( 255, 255, 255, 200 ), 2.0f );
            pDrawList->AddCircleFilled( ImVec2( cx + w * 0.15f, cy - h * 0.12f ), w * 0.08f,
                                        IM_COL32( 240, 200, 80, 230 ) );
            ImVec2 triPts[3] = { ImVec2( cx - w * 0.22f, cy + h * 0.22f ), ImVec2( cx, cy - h * 0.05f ),
                                 ImVec2( cx + w * 0.22f, cy + h * 0.22f ) };
            pDrawList->AddConvexPolyFilled( triPts, 3, IM_COL32( 70, 180, 120, 230 ) );
        }
        else if ( EditorAssetTypeRegistry::matches( EditorAssetKind::Scene, pPath ) )
        {
            // 3D Scene Compass / Horizon
            pDrawList->AddCircle( ImVec2( cx, cy ), w * 0.26f, IM_COL32( 70, 200, 140, 200 ), 18, 1.5f );
            pDrawList->AddLine( ImVec2( cx, cy - w * 0.28f ), ImVec2( cx, cy + w * 0.28f ),
                                IM_COL32( 240, 80, 80, 220 ), 1.5f );
            pDrawList->AddLine( ImVec2( cx - w * 0.28f, cy ), ImVec2( cx + w * 0.28f, cy ),
                                IM_COL32( 80, 160, 240, 220 ), 1.5f );
        }
        else if ( EditorAssetTypeRegistry::matches( EditorAssetKind::Shader, pPath ) )
        {
            // Shader Diamond / Prism
            const float32 r         = w * 0.24f;
            ImVec2        diaPts[4] = { ImVec2( cx, cy - r ), ImVec2( cx + r * 0.85f, cy ), ImVec2( cx, cy + r ),
                                        ImVec2( cx - r * 0.85f, cy ) };
            pDrawList->AddConvexPolyFilled( diaPts, 4, IM_COL32( 240, 120, 50, 255 ) );
            pDrawList->AddPolyline( diaPts, 4, IM_COL32( 255, 210, 140, 255 ), ImDrawFlags_Closed, 1.5f );
        }
        else if ( EditorAssetTypeRegistry::matches( EditorAssetKind::Audio, pPath ) )
        {
            // Sound wave equalizer bars
            constexpr int32   numBars    = 5;
            constexpr float32 heights[5] = { 0.25f, 0.55f, 0.95f, 0.65f, 0.35f };
            constexpr float32 barW       = 3.0f;
            constexpr float32 barGap     = 3.0f;
            constexpr float32 totalW     = numBars * barW + ( numBars - 1 ) * barGap;
            const float32     startX     = cx - totalW * 0.5f;
            for ( int32 barIndex = 0; barIndex < numBars; ++barIndex )
            {
                const float32 bx = startX + static_cast<float32>( barIndex ) * ( barW + barGap );
                const float32 bh = h * 0.45f * heights[barIndex];
                pDrawList->AddRectFilled( ImVec2( bx, cy - bh * 0.5f ), ImVec2( bx + barW, cy + bh * 0.5f ),
                                          IM_COL32( 80, 210, 220, 240 ), 1.0f );
            }
        }
        else
        {
            // Generic document / data file
            pDrawList->AddRectFilled( ImVec2( minPos._x + w * 0.22f, minPos._y + h * 0.16f ),
                                      ImVec2( minPos._x + w * 0.78f, minPos._y + h * 0.84f ),
                                      IM_COL32( 65, 70, 82, 255 ), 3.0f );
            const utf8*  pLbl  = ContentBrowserPanelInternal::typeLabel( entry._extension, false );
            const ImVec2 txtSz = ImGui::CalcTextSize( pLbl );
            pDrawList->AddText( ImVec2( cx - txtSz.x * 0.5f, cy - txtSz.y * 0.5f ),
                                IM_COL32( 220, 225, 235, 230 ), pLbl );
        }

        // Sub-border
        pDrawList->AddRect( minVec, maxVec, IM_COL32( 50, 55, 65, 200 ), 4.0f );
    }

    void ContentBrowserPanel::drawAssetContextMenu( const AssetEntry& entry )
    {
        if ( ImGui::BeginPopupContextItem( "AssetCtx" ) )
        {
            if ( ImGui::MenuItem( "Show in Explorer" ) )
            {
                fixed_string<constant::kMaxBuffer512> arrCmd;
                formatstring( arrCmd.data(), arrCmd.capacity(), "explorer.exe /select,\"%s\"", entry._absolutePath.c_str() );
                system( arrCmd.c_str() );
            }

            if ( ImGui::MenuItem( "Copy Relative Path" ) )
            {
                ImGui::SetClipboardText( entry._relativePath.c_str() );
            }

            if ( ImGui::MenuItem( "Copy Absolute Path" ) )
            {
                ImGui::SetClipboardText( entry._absolutePath.c_str() );
            }

            ImGui::Separator();
            if ( ImGui::MenuItem( "Delete" ) )
                EditorAssetCommands::deleteAsset( entry._absolutePath );
            ImGui::EndPopup();
        }
    }

    ContentBrowserPanel::ContentBrowserPanel() noexcept
        : _listRoot{}
        , _listEntry{}
        , _listHistory{}
        , _selectedFolderAbs{}
        , _breadcrumb{}
        , _selectedAssetAbs{}
        , _searchBuffer{}
        , _tileSize{ 96.0f }
        , _filterIndex{ 0 }
        , _historyIndex{ -1 }
        , _viewMode{ ViewMode::Tiles }
        , _pendingImportMutex{}
        , _listPendingImportPath{}
        , _folderJob{}
        , _bRootsDirty{ SW_TRUE }
        , _bFolderDirty{ SW_TRUE }
        , _reservedFlags{ 0 }
    {
    }

    void ContentBrowserPanel::drawContent()
    {
        // 마우스 X1/X2 버튼(뒤로가기/앞으로가기) 및 단축키(Alt+Left/Right, Backspace) 폴더 이동 처리
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
    }

    void ContentBrowserPanel::refreshRoots()
    {
        _listRoot.clear();

        const auto addRoot = [this]( const utf8* pName, string_view path )
        {
            if ( path.empty() )
                return;
            if ( FileUtil::directoryExists( path ) == false )
                return;
            ContentRoot root;
            root._displayName  = pName;
            root._absolutePath = FileUtil::normalizeSeparators( path );
            _listRoot.push_back( std::move( root ) );
        };

        addRoot( "game", ResourceUtil::getGameFolderPath() );
        addRoot( "engine", ResourceUtil::getEngineFolderPath() );
        addRoot( "common", ResourceUtil::getCommonFolderPath() );
        addRoot( "editor", ResourceUtil::getEditorFolderPath() );

        if ( _selectedFolderAbs.empty() && _listRoot.empty() == false )
            selectFolder( _listRoot.front()._absolutePath, _listRoot.front()._displayName );

        _bRootsDirty = SW_FALSE;
    }

    void ContentBrowserPanel::refreshCurrentFolder()
    {
        _folderJob.request( _selectedFolderAbs );
        _bFolderDirty = SW_FALSE;
    }

    void ContentBrowserPanel::applyFolderListing( vector<EditorFolderListingEntry>& listEntry )
    {
        _listEntry = std::move( listEntry );
        for ( const AssetEntry& entry : _listEntry )
        {
            if ( entry._bIsDirectory == false && entry._relativePath.empty() == false )
                editor::getService<ResourceManager>()->getAssetDatabase().ensureMeta( entry._relativePath, false );
        }

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
        if ( filter._kind == EditorAssetKind::Unknown )
            return true;
        return EditorAssetTypeRegistry::matches( filter._kind, pPath );
    }

    bool ContentBrowserPanel::passesSearchFilter( const AssetEntry& entry ) const
    {
        if ( _searchBuffer.empty() )
            return true;

        return StringUtil::stristr( entry._name.c_str(), _searchBuffer.c_str() ) != nullptr;
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
            EditorWidgets::drawTooltip( "에셋 이름 또는 확장자로 필터링하여 검색합니다" );

            ImGui::SameLine();
            uint32                          filterCount{ 0 };
            const EditorAssetBrowserFilter* pFilter  = EditorAssetTypeRegistry::getBrowserFilters( filterCount );
            const utf8*                     pPreview = "All";
            if ( _filterIndex < filterCount )
                pPreview = pFilter[_filterIndex]._label.data();
            ImGui::SetNextItemWidth( 110.0f );
            if ( ImGui::BeginCombo( "##cb_type", pPreview ) )
            {
                for ( uint32 filterIdx = 0; filterIdx < filterCount; ++filterIdx )
                {
                    const bool bSelected = ( _filterIndex == filterIdx );
                    if ( ImGui::Selectable( pFilter[filterIdx]._label.data(), bSelected ) )
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
                ImGui::SetNextItemWidth( 80.0f );
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
                _listEntry.clear();
                _selectedAssetAbs.clear();
                refreshRoots();
                refreshCurrentFolder();
            }
            EditorWidgets::drawTooltip( "디스크 파일 및 리소스 루트 목록을 새로고침합니다" );
        }
        EditorChrome::endToolbar();
    }

    void ContentBrowserPanel::drawSourcesSection()
    {
        editor::EditorSectionDesc sourcesDesc{};
        sourcesDesc._pId       = "##cb_sources";
        sourcesDesc._kind      = editor::EditorSectionKind::Child;
        sourcesDesc._childSize = float2{ 220.0f, 0.0f };
        sourcesDesc._flags     = editor::EditorSectionFlags::Border | editor::EditorSectionFlags::ResizeX;
        EditorChrome::beginSection( sourcesDesc );

        EditorWidgets::drawSectionHeader( "Favorites" );
        struct FavFolder
        {
            const utf8* _label;
            const utf8* _relPath;
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
                                         ? FileUtil::normalizeSeparators(
                                               FileUtil::joinPath( ResourceUtil::getEngineFolderPath(), kArrFavorites[favIdx]._relPath ) )
                                         : FileUtil::normalizeSeparators(
                                               ResourceUtil::joinActivePackPath( kArrFavorites[favIdx]._relPath ) );
            const bool   bSelected   = FileUtil::pathsEqualNormalized( fullFavPath, _selectedFolderAbs );

            if ( ImGui::Selectable( kArrFavorites[favIdx]._label, bSelected ) )
            {
                selectFolder( fullFavPath, string{ "Favorites / " } + kArrFavorites[favIdx]._label );
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

        vector<string> listChild;
        EditorAssetCommands::collectChildFolders( absPath, listChild );
        const bool hasChildDirs = listChild.empty() == false;
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

        if ( ImGui::IsItemClicked() && ImGui::IsItemToggledOpen() == false )
        {
            // 루트 이름 + 그 아래 상대 경로로 브레드크럼을 만듭니다.
            string crumb( label );
            for ( const ContentRoot& root : _listRoot )
            {
                const string rootNorm = FileUtil::normalizePath( root._absolutePath );
                const string absNorm  = FileUtil::normalizePath( absPath );
                if ( FileUtil::startsWithPathComponent( absNorm, rootNorm ) )
                {
                    crumb            = root._displayName;
                    const string rel = FileUtil::suffixAfterPathComponent( absNorm, rootNorm );
                    string       pretty;
                    size_t       start{ 0 };
                    while ( start < rel.size() )
                    {
                        size_t slash = rel.find( '/', start );
                        if ( slash == string::npos )
                            slash = rel.size();
                        if ( pretty.empty() == false )
                            pretty += " / ";
                        pretty += rel.substr( start, slash - start );
                        start = slash + 1;
                    }
                    if ( pretty.empty() == false )
                        crumb += " / " + pretty;
                    break;
                }
            }
            selectFolder( absPath, crumb );
        }

        if ( opened && hasChildDirs )
        {
            std::sort( listChild.begin(), listChild.end() );
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
        vector<AssetEntry> listVisible;
        listVisible.reserve( _listEntry.size() );
        for ( const AssetEntry& entry : _listEntry )
        {
            if ( passesTypeFilter( entry ) == false || passesSearchFilter( entry ) == false )
                continue;
            listVisible.push_back( entry );
        }

        editor::EditorSectionDesc assetsDesc{};
        assetsDesc._pId   = "##cb_assets";
        assetsDesc._kind  = editor::EditorSectionKind::Child;
        assetsDesc._flags = editor::EditorSectionFlags::Border | editor::EditorSectionFlags::FillRemaining;
        EditorChrome::beginSection( assetsDesc );

        drawBreadcrumbs();
        ImGui::Separator();

        if ( _viewMode == ViewMode::Tiles )
            drawTilesView( listVisible );
        else
            drawListView( listVisible );

        EditorChrome::endSection();

        EditorWidgets::drawCountLabel( static_cast<uint32>( listVisible.size() ), 0, "items" );
        if ( _selectedAssetAbs.empty() == false )
        {
            ImGui::SameLine();
            ImGui::TextDisabled( "|  %s", _selectedAssetAbs.c_str() );
        }
    }

    void ContentBrowserPanel::drawBreadcrumbs()
    {
        if ( _breadcrumb.empty() )
        {
            ImGui::TextDisabled( "Select a folder" );
            return;
        }

        // "Game / Shaders"를 클릭 가능한 세그먼트로 파싱합니다.
        vector<string_view> listPart;
        {
            string_view remaining{ _breadcrumb };
            size_t      pos{ 0 };
            while ( pos < remaining.size() )
            {
                size_t sep = remaining.find( " / ", pos );
                if ( sep == string_view::npos )
                {
                    listPart.push_back( remaining.substr( pos ) );
                    break;
                }
                listPart.push_back( remaining.substr( pos, sep - pos ) );
                pos = sep + 3;
            }
        }

        string builtCrumb;
        string builtPath;
        for ( size_t partIndex = 0; partIndex < listPart.size(); ++partIndex )
        {
            const string_view part = listPart[partIndex];
            if ( partIndex > 0 )
            {
                ImGui::SameLine();
                ImGui::TextUnformatted( "/" );
                ImGui::SameLine();
            }

            if ( partIndex == 0 )
            {
                builtCrumb = string{ part };
                for ( const ContentRoot& root : _listRoot )
                {
                    if ( root._displayName == part )
                    {
                        builtPath = root._absolutePath;
                        break;
                    }
                }
            }
            else
            {
                builtCrumb += " / ";
                builtCrumb.append( part.data(), part.size() );
                const string lowerChild = FileUtil::normalizePath( part );
                string       next       = FileUtil::joinPath( builtPath, part );
                if ( FileUtil::directoryExists( next ) == false && builtPath.empty() == false )
                {
                    vector<string> listChild;
                    EditorAssetCommands::collectChildFolders( builtPath, listChild );
                    for ( const string& child : listChild )
                    {
                        if ( FileUtil::normalizePath( FileUtil::getFileNamePart( child ) ) == lowerChild )
                        {
                            next = FileUtil::normalizeSeparators( child );
                            break;
                        }
                    }
                }
                builtPath = std::move( next );
            }

            const bool bIsLast = ( partIndex + 1 == listPart.size() );
            if ( bIsLast )
            {
                const Color4 accentCol = EditorThemeUtil::getAccentColor();
                ImGui::TextColored( ImVec4( accentCol._r, accentCol._g, accentCol._b, 1.0f ), ICON_FA_FOLDER_OPEN );
                ImGui::SameLine();
                ImGui::TextUnformatted( string( part ).c_str() );
            }

            else
            {
                const string partStr{ part };
                if ( ImGui::SmallButton( partStr.c_str() ) )
                    selectFolder( builtPath, builtCrumb );
            }
        }
    }

    void ContentBrowserPanel::drawTilesView( const vector<AssetEntry>& listVisible )
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
        // 버튼 + 줄바꿈된 이름 라인 (셀 + 텍스트 라인)
        const float32 rowHeight = cell + ImGui::GetTextLineHeightWithSpacing() + paddingY;

        ImGuiListClipper clipper;
        clipper.Begin( rowCount, rowHeight );
        while ( clipper.Step() )
        {
            for ( int32 row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row )
            {
                for ( int32 col = 0; col < columns; ++col )
                {
                    const int32 index = row * columns + col;
                    if ( index >= itemCount )
                        break;

                    const AssetEntry& entry = listVisible[static_cast<size_t>( index )];
                    ImGui::PushID( entry._absolutePath.c_str() );

                    if ( col > 0 )
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
                    drawAssetContextMenu( entry );
                    if ( ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
                        openAsset( entry );
                    if ( entry._bIsDirectory == false )
                        EditorWidgets::drawAssetDragSource( entry._relativePath.c_str(), true );

                    ImDrawList*       pDrawList = ImGui::GetWindowDrawList();
                    constexpr float32 inset     = 6.0f;
                    drawAssetThumbnail( pDrawList, float2{ cursor.x + inset, cursor.y + inset },
                                        float2{ cursor.x + cell - inset, cursor.y + cell * 0.65f }, entry );

                    ImGui::PushTextWrapPos( ImGui::GetCursorPos().x + cell );
                    ImGui::TextUnformatted( entry._name.c_str() );
                    ImGui::PopTextWrapPos();
                    ImGui::EndGroup();

                    ImGui::PopStyleColor( 2 );
                    ImGui::PopID();
                }
            }
        }
    }

    void ContentBrowserPanel::drawListView( const vector<AssetEntry>& listVisible )
    {
        constexpr ImGuiTableFlags flags =
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;

        if ( ImGui::BeginTable( "##cb_list", 3, flags, ImGui::GetContentRegionAvail() ) )
        {
            ImGui::TableSetupColumn( "Name", ImGuiTableColumnFlags_WidthStretch );
            ImGui::TableSetupColumn( "Type", ImGuiTableColumnFlags_WidthFixed, 90.0f );
            ImGui::TableSetupColumn( "Path", ImGuiTableColumnFlags_WidthStretch );
            ImGui::TableHeadersRow();

            ImGuiListClipper clipper;
            clipper.Begin( static_cast<int32>( listVisible.size() ) );
            while ( clipper.Step() )
            {
                for ( int32 itemIndex = clipper.DisplayStart; itemIndex < clipper.DisplayEnd; ++itemIndex )
                {
                    const AssetEntry& entry = listVisible[static_cast<size_t>( itemIndex )];
                    ImGui::PushID( entry._absolutePath.c_str() );
                    ImGui::TableNextRow();

                    const bool selected = FileUtil::pathsEqualNormalized( entry._absolutePath, _selectedAssetAbs );
                    ImGui::TableSetColumnIndex( 0 );
                    const utf8*  pAssetIcon   = EditorThemeUtil::getAssetIconForPath( entry._name, entry._bIsDirectory );
                    const Color4 assetColor   = EditorThemeUtil::getAssetColorForPath( entry._name, entry._bIsDirectory );
                    const string nameWithIcon = string( pAssetIcon ) + "  " + entry._name;
                    ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( assetColor._r, assetColor._g, assetColor._b, assetColor._a ) );
                    const bool bSelected = ImGui::Selectable( nameWithIcon.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick );
                    ImGui::PopStyleColor();
                    if ( bSelected )
                    {
                        selectAsset( entry );
                        if ( ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
                            openAsset( entry );
                    }
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
            selectFolder( entry._folderPathAbs, entry._breadcrumb, false );
        }
    }

    void ContentBrowserPanel::navigateForward()
    {
        if ( canNavigateForward() )
        {
            ++_historyIndex;
            const HistoryEntry& entry = _listHistory[static_cast<size_t>( _historyIndex )];
            selectFolder( entry._folderPathAbs, entry._breadcrumb, false );
        }
    }

    void ContentBrowserPanel::selectFolder( string_view absolutePath, string_view breadcrumb, bool bRecordHistory )
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
                {
                    _listHistory.erase( _listHistory.begin() + ( _historyIndex + 1 ), _listHistory.end() );
                }
                HistoryEntry newEntry;
                newEntry._folderPathAbs = normalizedPath;
                newEntry._breadcrumb    = string{ breadcrumb };
                _listHistory.push_back( std::move( newEntry ) );
                _historyIndex = static_cast<int32>( _listHistory.size() ) - 1;
            }
        }

        // 탐색/I/O는 실제 FS 대소문자를 쓰고, 저장 경로는 ResourceUtil::makeSavePath로 상대 세그먼트를 소문자화합니다.
        _selectedFolderAbs = normalizedPath;
        _breadcrumb        = string{ breadcrumb };
        _selectedAssetAbs.clear();
        _bFolderDirty = SW_TRUE;
    }

    void ContentBrowserPanel::selectAsset( const AssetEntry& entry )
    {
        _selectedAssetAbs = entry._absolutePath;
        if ( entry._bIsDirectory == false )
            EditorContext::get()->getWorkspace().setFocusedAssetPath( entry._relativePath.c_str() );
    }

    void ContentBrowserPanel::openAsset( const AssetEntry& entry )
    {
        if ( entry._bIsDirectory )
        {
            string nextCrumb = _breadcrumb.empty() ? string( entry._name ) : string( _breadcrumb + " / " + entry._name );
            selectFolder( entry._absolutePath, nextCrumb );
            return;
        }

        selectAsset( entry );
        SW_LOG_TRACE( "Open: %#", entry._relativePath.c_str() );
        EditorAssetCommands::openPath( entry._relativePath );
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
