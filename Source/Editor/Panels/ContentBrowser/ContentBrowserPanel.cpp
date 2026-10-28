#include "pch.h"

#include "Editor/Panels/ContentBrowser/ContentBrowserPanel.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include "Editor/Common/Commands/Asset/EditorAssetCommands.h"
#include "Editor/Common/Commands/Asset/EditorAssetFileCommands.h"
#include "Editor/Common/Commands/Asset/EditorFolderListing.h"
#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Config/EditorPreferences.h"
#include "Editor/Common/Config/EditorSettingsRegistry.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorIconGlyphs.h"
#include "Editor/Common/GUI/Theme/EditorTextExtentImGui.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/SourceControl/EditorSourceControl.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Widgets/Model/EditorListFilter.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/Asset/EditorAssetTypeActions.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/HotReload/AssetHotReload.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/FileSystem/AssetPaths.h"
#include "Engine/Foundation/EngineDefines.h"
#include "Engine/Localization/LocText.h"

#include <imgui.h>

namespace sw::editor
{
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_editorWatchAsset, "", "감시 값 Editor.ReferenceCount 가 볼 에셋의 리소스 id (시나리오용)" );

    namespace
    {
        struct ContentBrowserPanelInternal
        {
            static constexpr Color kThumbnailBg{ Color::fromBytes( 22, 24, 30, 255 ) };
            static constexpr Color kThumbnailBorder{ Color::fromBytes( 50, 55, 65, 200 ) };
            static constexpr Color kFolderCardBorder{ Color::fromBytes( 40, 48, 62, 160 ) };
            static constexpr Color kFolderPocketBorder{ Color::fromBytes( 15, 25, 45, 120 ) };
            static constexpr Color kLockBadgeLocked{ Color::fromBytes( 255, 160, 40, 255 ) };
            static constexpr Color kLockBadgeReadOnly{ Color::fromBytes( 170, 170, 170, 255 ) };

            /** @brief 타일 이름의 줄 수입니다. 넘치면 마지막 줄을 말줄임표로 줄입니다(`EditorWidgets::drawClampedLabel`). */
            static constexpr uint32 kTileLabelLineCount = 2;

            /** @brief 삭제 확인 모달의 팝업 이름입니다. OpenPopup 과 BeginPopupModal 이 같은 문자열을 써야 합니다. */
            static constexpr const utf8* kDeleteConfirmPopupName = "Delete Asset##ConfirmDeleteAsset";

            /** @brief 참조 삭제 경고 문구와 참조 조회 ID 문구의 색입니다. */
            static constexpr Color kReferrerWarningText{ 1.0f, 0.7f, 0.25f, 1.0f };
            static constexpr Color kReferenceQueryText{ 0.55f, 0.8f, 1.0f, 1.0f };

            static ImVec4 colorForAsset( string_view path, bool bIsDirectory = false )
            {
                return EditorColorUtil::toImVec4( EditorThemeUtil::getAssetColorForPath( path, bIsDirectory ) );
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
            /** @brief 폴더 이동 액션입니다. 키는 `editor.input.xml` 의 `Editor.Panel.content_browser` 레이어가 정한다. */
            static constexpr string_view kActionNavigateBack    = "contentBrowser.navigateBack";
            static constexpr string_view kActionNavigateForward = "contentBrowser.navigateForward";
            /** @brief 이미지 썸네일이 없는 종류의 아이콘 크기입니다. 썸네일 짧은 변에 대한 비율입니다. */
            static constexpr float32 kFallbackGlyphSizeRatio = 0.5f;
            /** @brief 참조 결과 테이블이 스크롤 없이 보이는 최대 행 수입니다. */
            static constexpr size_t kReferenceVisibleRowCount = 12;

            /** @brief 테스트가 켜져 있으면 바로 앞 위젯에 `<접두><이름>` 이름을 남깁니다. 꺼져 있으면 문자열을 만들지 않습니다. */
            static void noteMark( const utf8* pPrefix, string_view name )
            {
                if ( EditorSelfTestMarks::isEnabled() == false )
                    return;
                const string key = string( pPrefix ) + string( name );
                EditorSelfTestMarks::note( key.c_str() );
            }

            /** @brief 콘텐츠 브라우저 패널을 반환합니다. 없으면 nullptr 입니다. */
            static const ContentBrowserPanel* findPanel()
            {
                EditorContext* pContext = editor::findSubsystem<EditorContext>();
                if ( pContext == nullptr )
                    return nullptr;
                return pContext->getSubsystem<EditorPanelManager>().findPanel<ContentBrowserPanel>();
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
                outValue = pPanel->isReferenceIndexCurrent() ? 1.0 : 0.0;
                return true;
            }

            /**
             * @brief 시계 잡기 조건 — 콘텐츠 브라우저의 내용이 보이는데 참조 역색인이 마지막 `Content/` 변경을 아직 반영하지 않았으면 참입니다.
             * @details 스캔은 수 초(Debug 는 수십 초)가 걸린다. 시계가 그동안 가면 고정 프레임의 단언과 이름 바꾸기가 스캔 전의 색인(시나리오의 `<Scratch>` 복사 전 트리)을 쓴다.
             *          언리얼 자동화가 에셋 레지스트리의 스캔이 끝나기를 기다리는 것과 같다.
             */
            static bool isWaitingForReferenceIndex()
            {
                const ContentBrowserPanel* pPanel = findPanel();
                return pPanel != nullptr && pPanel->isWaitingForReferenceIndex();
            }

            /** @brief 에디터가 본 `Content/` 변경 번호입니다. 패널은 에디터 컨텍스트가 있어야 도므로 컨텍스트와 핫 리로드는 필수입니다. */
            [[nodiscard]] static uint64 getContentChangeSerial()
            {
                return editor::getSubsystem<EditorContext>().getSubsystem<AssetHotReload>().getContentChangeSerial();
            }

            /** @brief `content.open <리소스 폴더>` 명령입니다. 콘텐츠 브라우저를 열고 그 폴더로 이동합니다. 언리얼의 Sync to Content Browser 와 같습니다. */
            static bool runContentOpen( const vector<string>& listArgument, string& outReply )
            {
                EditorContext* pContext = editor::findSubsystem<EditorContext>();
                if ( listArgument.size() != 1 || pContext == nullptr )
                    return false;
                const string folderAbs = AssetPaths::findLooseFile( FileUtil::normalizePath( listArgument[0] ) ).getDiskPath();
                if ( FileUtil::isDirectory( folderAbs ) == false || pContext->getSubsystem<EditorPanelManager>().setPanelOpen<ContentBrowserPanel>( true ) == false )
                    return false;
                ContentBrowserPanel* pPanel = pContext->getSubsystem<EditorPanelManager>().findPanel<ContentBrowserPanel>();
                if ( pPanel == nullptr )
                    return false;
                pPanel->openFolder( folderAbs );
                outReply = "content browser at " + listArgument[0];
                return true;
            }

            [[nodiscard]] static bool readThumbnailCacheCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const ContentBrowserPanel* pPanel = findPanel();
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getThumbnailCacheCount() );
                return true;
            }

            [[nodiscard]] static bool readFallbackGlyphCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const ContentBrowserPanel* pPanel = findPanel();
                if ( pPanel == nullptr )
                    return false;
                outValue = static_cast<float64>( pPanel->getFallbackGlyphCount() );
                return true;
            }

            /** @brief `-gv_editorWatchAsset` 을 참조하는 텍스트 에셋 파일 수입니다. 역색인이 마지막 변경을 반영하지 않았으면(스캔 중 · 아직 없음 · 패널이 가려짐) 낡은 값 대신 값을 내지 않습니다. */
            [[nodiscard]] static bool readReferenceCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const ContentBrowserPanel* pPanel = findPanel();
                if ( pPanel == nullptr || pPanel->isReferenceIndexCurrent() == false )
                    return false;
                outValue = static_cast<float64>( pPanel->getReferenceIndex().countReferrerFiles( gv_editorWatchAsset ) );
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

            // ------------------------------------------------------------------------------
            // 에셋 명령 — 맥락 메뉴 · 단축키(`editor.input.xml` 의 `Editor.Panel.content_browser`)가 같은 명령을 부른다.
            // 대상 id 는 절대 경로를 페이로드 이름 표(`EditorInputGUI::registerPayloadName`)에 올린 값이다. 대상이 없으면(단축키) 이 창의 선택이다.
            // ------------------------------------------------------------------------------
            static ContentBrowserPanel* findWritablePanel()
            {
                EditorContext* pContext = editor::findSubsystem<EditorContext>();
                return pContext != nullptr ? pContext->getSubsystem<EditorPanelManager>().findPanel<ContentBrowserPanel>() : nullptr;
            }

            /** @brief 대상 에셋의 절대 경로입니다. 누른 항목 → 선택 스냅숏 → 창의 지금 선택 순서로 고릅니다. 없으면 빈 문자열입니다. */
            static string findTargetPath( const EditorCommandTarget& target )
            {
                string       path;
                const uint64 targetID = target._hoveredID != 0 ? target._hoveredID : ( target._listSelectedID.empty() ? 0 : target._listSelectedID.front() );
                if ( targetID != 0 )
                {
                    (void)EditorInputGUI::findPayloadName( EditorPayloadKind::Asset, targetID, path ); // 모르는 id 면 빈 경로 — 명령이 꺼진다
                    return path;
                }
                const ContentBrowserPanel* pPanel = findPanel();
                if ( pPanel != nullptr )
                    path = pPanel->getSelectedAssetPath();
                return path;
            }

            static bool hasTarget( const EditorCommandTarget& target ) { return findTargetPath( target ).empty() == false; }

            /** @brief 대상이 폴더가 아닌 파일이면 true 입니다(이름 바꾸기 · 복제 · 지우기 · 참조 · 잠금). */
            static bool hasFileTarget( const EditorCommandTarget& target )
            {
                const string path = findTargetPath( target );
                return path.empty() == false && FileUtil::isDirectory( path ) == false;
            }

            static bool hasTextAssetTarget( const EditorCommandTarget& target )
            {
                const string path = findTargetPath( target );
                return path.empty() == false && EditorReferenceIndex::isTextAsset( path );
            }

            static void showInExplorer( const EditorCommandTarget& target ) { EditorAssetFileCommands::showInFileExplorer( findTargetPath( target ) ); }

            static void copyRelativePath( const EditorCommandTarget& target ) { ImGui::SetClipboardText( AssetPaths::toAssetID( findTargetPath( target ) ).c_str() ); }

            static void copyAbsolutePath( const EditorCommandTarget& target ) { ImGui::SetClipboardText( findTargetPath( target ).c_str() ); }

            static void findReferences( const EditorCommandTarget& target )
            {
                ContentBrowserPanel* pPanel = findWritablePanel();
                if ( pPanel != nullptr )
                    pPanel->showReferences( findTargetPath( target ), false );
            }

            static void showDependencies( const EditorCommandTarget& target )
            {
                ContentBrowserPanel* pPanel = findWritablePanel();
                if ( pPanel != nullptr )
                    pPanel->showReferences( findTargetPath( target ), true );
            }

            static EditorSourceControl& getSourceControl() { return editor::getSubsystem<EditorContext>().getSubsystem<EditorSourceControl>(); }

            static bool canLock( const EditorCommandTarget& target )
            {
                return hasFileTarget( target ) && getSourceControl().getProvider().canLock() && getSourceControl().findLock( findTargetPath( target ) ) == nullptr;
            }

            static bool canUnlock( const EditorCommandTarget& target )
            {
                return hasFileTarget( target ) && getSourceControl().getProvider().canLock() && getSourceControl().findLock( findTargetPath( target ) ) != nullptr;
            }

            static bool canRefreshSourceControl( const EditorCommandTarget& target ) { return hasFileTarget( target ) && getSourceControl().getProvider().canLock(); }

            static void lockAsset( const EditorCommandTarget& target ) { (void)getSourceControl().requestLock( findTargetPath( target ) ); } // 결과는 공급자가 알린다

            static void unlockAsset( const EditorCommandTarget& target ) { (void)getSourceControl().requestUnlock( findTargetPath( target ) ); } // 결과는 공급자가 알린다

            static void refreshSourceControl( const EditorCommandTarget& /*target*/ ) { getSourceControl().requestRefresh(); }

            static void renameAsset( const EditorCommandTarget& target )
            {
                ContentBrowserPanel* pPanel = findWritablePanel();
                if ( pPanel != nullptr )
                    pPanel->beginRename( findTargetPath( target ) );
            }

            static void duplicateAsset( const EditorCommandTarget& target )
            {
                ContentBrowserPanel* pPanel = findWritablePanel();
                if ( pPanel != nullptr )
                    (void)pPanel->duplicateAsset( findTargetPath( target ) ); // 실패는 copyFile 이 알린다
            }

            static void deleteAsset( const EditorCommandTarget& target )
            {
                // 지우기 전에 확인한다. 파일은 휴지통으로 보낸다. 모달은 메뉴 밖의 창 단위에서 연다.
                ContentBrowserPanel* pPanel = findWritablePanel();
                if ( pPanel != nullptr )
                    pPanel->requestDeleteAsset( findTargetPath( target ) );
            }

            /** @brief 선택 밖 항목에서 메뉴를 열면 그 항목만 고릅니다(R2). */
            static void selectOnlyAsset( uint64 assetID )
            {
                ContentBrowserPanel* pPanel = findWritablePanel();
                string               path;
                if ( pPanel != nullptr && EditorInputGUI::findPayloadName( EditorPayloadKind::Asset, assetID, path ) )
                    pPanel->selectAssetPath( path );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "ContentBrowserPanel" );
    SW_EDITOR_PANEL( ContentBrowserPanel, ContentBrowserPanel::kPanelID, EditorPanelCategory::Core, 600 );
    SW_AUTOMATION_WATCH( editorContentGameRootCount, "Editor.ContentGameRootCount", "Game pack roots the Content Browser lists (1 = active pack only)",
                         &ContentBrowserPanelInternal::readGameRootCount );
    SW_AUTOMATION_WATCH( editorReferenceIndexReady, "Editor.ReferenceIndexReady",
                         "1 when the Content Browser reference index reflects the last Content/ change the editor saw (built, no scan in flight)",
                         &ContentBrowserPanelInternal::readReferenceIndexReady );
    SW_AUTOMATION_CLOCK_HOLDER( contentBrowserReferenceIndex, "ContentBrowserReferenceIndex", &ContentBrowserPanelInternal::isWaitingForReferenceIndex );
    SW_AUTOMATION_WATCH( editorReferenceResultCount, "Editor.ReferenceResultCount", "Rows of the last Find References or Show Dependencies query",
                         &ContentBrowserPanelInternal::readReferenceResultCount );
    SW_AUTOMATION_WATCH( editorThumbnailCacheCount, "Editor.ThumbnailCacheCount", "Texture thumbnails the Content Browser holds as ImGui textures",
                         &ContentBrowserPanelInternal::readThumbnailCacheCount );
    SW_AUTOMATION_WATCH( editorThumbnailFallbackGlyphCount, "Editor.ThumbnailFallbackGlyphCount",
                         "Content Browser tiles drawn last frame with the kind icon (kinds without a drawn thumbnail)", &ContentBrowserPanelInternal::readFallbackGlyphCount );
    SW_AUTOMATION_WATCH( editorReferenceCount, "Editor.ReferenceCount", "Text assets that name gv_editorWatchAsset (the reference index)",
                         &ContentBrowserPanelInternal::readReferenceCount );
    SW_DEV_COMMAND( ContentOpen, "content.open", "content.open <resource folder>", "Show a resource folder in the Content Browser (e.g. game/empty/models)",
                    &ContentBrowserPanelInternal::runContentOpen );
    // 에셋 맥락 메뉴 · 단축키(R4 — 메뉴 · 키 · 팔레트가 같은 명령). 순서 칸: 100 탐색기 · 경로, 200 참조, 300 버전 관리, 900 파일 편집.
    SW_EDITOR_CONTEXT_COMMAND( AssetShowInExplorer, "asset.showInExplorer", "Show in Explorer", editoricon::kFolderOpen, "Asset", Asset, 100, nullptr,
                               &ContentBrowserPanelInternal::showInExplorer, &ContentBrowserPanelInternal::hasTarget, NoScene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( AssetCopyRelativePath, "asset.copyRelativePath", "Copy Relative Path", nullptr, "Asset", Asset, 110, nullptr,
                               &ContentBrowserPanelInternal::copyRelativePath, &ContentBrowserPanelInternal::hasTarget, NoScene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( AssetCopyAbsolutePath, "asset.copyAbsolutePath", "Copy Absolute Path", nullptr, "Asset", Asset, 120, nullptr,
                               &ContentBrowserPanelInternal::copyAbsolutePath, &ContentBrowserPanelInternal::hasTarget, NoScene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( AssetFindReferences, "asset.findReferences", "Find References", nullptr, "Asset", Asset, 200, nullptr,
                               &ContentBrowserPanelInternal::findReferences, &ContentBrowserPanelInternal::hasFileTarget, NoScene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( AssetShowDependencies, "asset.showDependencies", "Show Dependencies", nullptr, "Asset", Asset, 210, nullptr,
                               &ContentBrowserPanelInternal::showDependencies, &ContentBrowserPanelInternal::hasTextAssetTarget, NoScene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( AssetLock, "asset.lock", "Check Out (Lock)", editoricon::kLock, "Asset", Asset, 300, nullptr, &ContentBrowserPanelInternal::lockAsset,
                               &ContentBrowserPanelInternal::canLock, NoScene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( AssetUnlock, "asset.unlock", "Release Lock", editoricon::kUnlock, "Asset", Asset, 310, nullptr, &ContentBrowserPanelInternal::unlockAsset,
                               &ContentBrowserPanelInternal::canUnlock, NoScene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( AssetRefreshSourceControl, "asset.refreshSourceControl", "Refresh Source Control", nullptr, "Asset", Asset, 320, nullptr,
                               &ContentBrowserPanelInternal::refreshSourceControl, &ContentBrowserPanelInternal::canRefreshSourceControl, NoScene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( AssetRename, "asset.rename", "Rename", nullptr, "Asset", Asset, 900, "asset.rename", &ContentBrowserPanelInternal::renameAsset,
                               &ContentBrowserPanelInternal::hasFileTarget, NoScene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( AssetDuplicate, "asset.duplicate", "Duplicate", nullptr, "Asset", Asset, 910, "asset.duplicate", &ContentBrowserPanelInternal::duplicateAsset,
                               &ContentBrowserPanelInternal::hasFileTarget, NoScene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( AssetDelete, "asset.delete", "Delete...", editoricon::kTrash, "Asset", Asset, 920, "asset.delete", &ContentBrowserPanelInternal::deleteAsset,
                               &ContentBrowserPanelInternal::hasFileTarget, NoScene, nullptr );

    void ContentBrowserPanel::drawAssetThumbnail( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos,
                                                  const AssetEntry& entry )
    {
        if ( pDrawList == nullptr )
            return;

        const ImVec2 minVec{ minPos._x, minPos._y };
        const ImVec2 maxVec{ maxPos._x, maxPos._y };

        // 카드 썸네일 배경
        pDrawList->AddRectFilled( minVec, maxVec, EditorColorUtil::toImU32( ContentBrowserPanelInternal::kThumbnailBg ), 4.0f );

        const utf8* pPath = entry._absolutePath.empty() ? entry._name.c_str() : entry._absolutePath.c_str();
        if ( entry._bIsDirectory )
        {
            drawFolderThumbnail( pDrawList, minPos, maxPos );
        }
        else
        {
            // 종류별 그림은 그 종류의 동작(`IEditorAssetTypeActions`)이 그린다. 이미지 미리보기가 있는 종류(텍스처)는 썸네일 캐시의 이미지가 우선한다.
            // 둘 다 없으면 종류 아이콘을 종류 색으로 가운데에 그린다.
            const IEditorAssetTypeActions* pActions = EditorAssetTypeActionsRegistry::findActionsForPath( pPath );
            bool                           bDrawn   = false;
            if ( pActions != nullptr && pActions->hasImagePreview() )
            {
                void* pTextureID = _thumbnailCache.findTexture( entry._relativePath );
                if ( pTextureID != nullptr )
                {
                    // 정사각형으로 가운데에 그린다. 썸네일 영역은 가로가 길다. 텍스처 비율은 보지 않으며, 정사각형이 아니면 늘어난 채 보인다.
                    const float32 side    = MathUtil::min( maxPos._x - minPos._x, maxPos._y - minPos._y );
                    const float32 centerX = ( minPos._x + maxPos._x ) * 0.5f;
                    const float32 centerY = ( minPos._y + maxPos._y ) * 0.5f;
                    pDrawList->AddImage( reinterpret_cast<ImTextureID>( pTextureID ), ImVec2( centerX - side * 0.5f, centerY - side * 0.5f ),
                                         ImVec2( centerX + side * 0.5f, centerY + side * 0.5f ) );
                    bDrawn = true;
                }
            }
            if ( bDrawn == false && pActions != nullptr )
                bDrawn = pActions->drawThumbnail( pDrawList, minPos, maxPos );
            if ( bDrawn == false )
            {
                EditorThemeUtil::drawCenteredGlyph( pDrawList, minPos, maxPos, EditorThemeUtil::getAssetIconForPath( entry._name ),
                                                    EditorThemeUtil::getAssetColorForPath( entry._name ), ContentBrowserPanelInternal::kFallbackGlyphSizeRatio );
                ++_fallbackGlyphCount;
            }
        }

        // 안쪽 테두리
        pDrawList->AddRect( minVec, maxVec, EditorColorUtil::toImU32( ContentBrowserPanelInternal::kThumbnailBorder ), 4.0f );
    }

    void ContentBrowserPanel::drawFolderThumbnail( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos )
    {
        const ImVec2  minVec{ minPos._x, minPos._y };
        const ImVec2  maxVec{ maxPos._x, maxPos._y };
        const float32 w = maxPos._x - minPos._x;
        const float32 h = maxPos._y - minPos._y;

        const Color folderColor = EditorThemeUtil::getFolderColor();

        // 카드의 옅은 테두리
        pDrawList->AddRect( minVec, maxVec, EditorColorUtil::toImU32( ContentBrowserPanelInternal::kFolderCardBorder ), 4.0f );

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
        pDrawList->AddRect( ImVec2( fLeft, fPktTop ), ImVec2( fRight, fBottom ), EditorColorUtil::toImU32( ContentBrowserPanelInternal::kFolderPocketBorder ), 3.0f );
    }

    string ContentBrowserPanel::describeSourceControlStatus( const AssetEntry& entry )
    {
        EditorContext& editorContext = editor::getSubsystem<EditorContext>();
        if ( entry._bIsDirectory )
            return {};
        return editorContext.getSubsystem<EditorSourceControl>().describeStatus( entry._absolutePath, entry._bReadOnly );
    }

    void ContentBrowserPanel::drawSourceControlBadge( ImDrawList* pDrawList, const float2& topRight, const AssetEntry& entry )
    {
        const string statusText = describeSourceControlStatus( entry );
        if ( statusText.empty() )
            return;
        // 잠금은 누가 걸었든 주황 자물쇠로, 읽기 전용(잠그기 전)은 회색 자물쇠로 그린다. 바로 아래 버튼의 툴팁이 이유를 설명한다.
        EditorContext& editorContext = editor::getSubsystem<EditorContext>();
        const bool     bLocked       = editorContext.getSubsystem<EditorSourceControl>().findLock( entry._absolutePath ) != nullptr;
        const ImU32    color         = bLocked ? EditorColorUtil::toImU32( ContentBrowserPanelInternal::kLockBadgeLocked ) : EditorColorUtil::toImU32( ContentBrowserPanelInternal::kLockBadgeReadOnly );
        const ImVec2   textSize      = ImGui::CalcTextSize( editoricon::kLock );
        pDrawList->AddText( ImVec2( topRight._x - textSize.x, topRight._y ), color, editoricon::kLock );
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "%s", statusText.c_str() );
    }

    void ContentBrowserPanel::drawAssetContextMenu( const AssetEntry& entry )
    {
        // 항목은 `asset.*` 명령이다(이 파일의 등록 줄). 대상 id 는 절대 경로를 페이로드 이름 표에 올린 값이다.
        EditorContextMenuSubject subject{};
        subject._pPopupID  = "AssetCtx";
        subject._kind      = EditorCommandTargetKind::Asset;
        subject._hoveredID = EditorInputGUI::registerPayloadName( EditorPayloadKind::Asset, entry._absolutePath );
        if ( _selectedAssetAbs.empty() == false )
            subject._listSelectedID.push_back( EditorInputGUI::registerPayloadName( EditorPayloadKind::Asset, _selectedAssetAbs ) );
        subject._selectOnly = &ContentBrowserPanelInternal::selectOnlyAsset;
        (void)EditorInputGUI::drawContextMenuFor( subject ); // 열렸는지는 쓰지 않는다
    }

    void ContentBrowserPanel::selectAssetPath( string_view absolutePath )
    {
        for ( const AssetEntry& entry : _listEntry )
        {
            if ( FileUtil::pathsEqualNormalized( entry._absolutePath, absolutePath ) )
            {
                selectAsset( entry );
                return;
            }
        }
    }

    ContentBrowserPanel::ContentBrowserPanel() noexcept
        : _listRoot{}
        , _listEntry{}
        , _listVisibleEntry{}
        , _selectedFolderAbs{}
        , _listCrumb{}
        , _selectedAssetAbs{}
        , _history{}
        , _fileActions{}
        , _referenceSearch{}
        , _importQueue{}
        , _thumbnailCache{}
        , _searchBuffer{}
        , _renameBuffer{}
        , _seenContentChangeSerial{ 0 }
        , _tileSize{ 96.0f }
        , _filterIndex{ 0 }
        , _fallbackGlyphCount{ 0 }
        , _lastFallbackGlyphCount{ 0 }
        , _viewMode{ ViewMode::Tiles }
        , _folderJob{}
        , _folderCache{ &EditorFolderListing::collectChildFolders }
        , _bRootsDirty{ SW_TRUE }
        , _bFolderDirty{ SW_TRUE }
        , _bOpenDeleteConfirm{ SW_FALSE }
        , _bShowAllPacks{ SW_FALSE }
        , _bShowAllPacksInitialized{ SW_FALSE }
        , _bOpenReferenceResults{ SW_FALSE }
        , _bFocusRenameInput{ SW_FALSE }
        , _bListingRecursive{ SW_FALSE }
        , _bContentVisible{ SW_FALSE }
        , _reservedFlags{ 0 }
    {
    }

    void ContentBrowserPanel::requestDeleteAsset( string_view absolutePath )
    {
        _fileActions.requestDelete( absolutePath );
        _bOpenDeleteConfirm = SW_TRUE;
    }

    bool ContentBrowserPanel::confirmDeleteAsset()
    {
        if ( _fileActions.confirmDelete() == false )
            return false;
        // 지운 파일이 목록과 선택에 남지 않도록 현재 폴더를 다시 읽는다. 파일 감시도 같은 변경을 알리지만 한두 프레임 늦다.
        _selectedAssetAbs.clear();
        _bFolderDirty = SW_TRUE;
        _folderCache.clear();
        return true;
    }

    void ContentBrowserPanel::drawDeleteConfirmModal()
    {
        if ( _bOpenDeleteConfirm == SW_TRUE )
        {
            EditorInputGUI::openPopup( ContentBrowserPanelInternal::kDeleteConfirmPopupName );
            _bOpenDeleteConfirm = SW_FALSE;
        }
        if ( EditorWidgets::beginCenteredModal( ContentBrowserPanelInternal::kDeleteConfirmPopupName ) == false )
            return;

        const string&               pendingDeleteAbs = _fileActions.getPendingDeletePath();
        const EditorReferenceIndex& referenceIndex   = _referenceSearch.getIndex();
        ImGui::Text( "Move '%s' to the Recycle Bin?", FileUtil::getFileNamePart( pendingDeleteAbs ).c_str() );
        // 지우기 전에 이 에셋을 쓰는 곳을 보여 준다. 언리얼의 Delete Assets 대화상자가 보여 주는 참조 수와 같다.
        if ( referenceIndex.isReady() )
        {
            const uint32 referrerCount = referenceIndex.countReferrerFiles( AssetPaths::toAssetID( pendingDeleteAbs ) );
            if ( referrerCount > 0 )
                ImGui::TextColored( EditorColorUtil::toImVec4( ContentBrowserPanelInternal::kReferrerWarningText ), "%s Used by %u file(s). They will name a missing asset.", editoricon::kWarning, referrerCount );
            else
                ImGui::TextDisabled( "No text asset names it." );
        }
        else
        {
            ImGui::TextDisabled( "The reference index is still building." );
        }
        ImGui::Separator();
        const bool bConfirmed = ImGui::Button( "Delete" );
        EditorSelfTestMarks::note( "contentBrowser.delete.confirm" );
        if ( bConfirmed )
        {
            (void)confirmDeleteAsset(); // 실패는 moveToTrash 가 알린다(파일과 .meta 를 그대로 둔다)
            ImGui::CloseCurrentPopup();
        }
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Cancel" ) );
        if ( ImGui::Button( "Cancel" ) || EditorInputGUI::wasCancelPressed() )
        {
            _fileActions.cancelDelete();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void ContentBrowserPanel::syncWithContentChanges()
    {
        EditorContext& editorContext = editor::getSubsystem<EditorContext>();
        const uint64   serial        = editorContext.getSubsystem<AssetHotReload>().getContentChangeSerial();
        if ( serial == _seenContentChangeSerial )
            return;
        _seenContentChangeSerial = serial;
        _bFolderDirty            = SW_TRUE;
        _folderCache.clear();
    }

    void ContentBrowserPanel::shutdown( IRHIDevice* /*pDevice*/ )
    {
        _thumbnailCache.clear();
    }

    void ContentBrowserPanel::syncReferenceIndex()
    {
        AssetHotReload& hotReload = editor::getSubsystem<EditorContext>().getSubsystem<AssetHotReload>(); // 번호의 출처이기도 하다
        _referenceSearch.update( hotReload.getContentChangeSerial(), &hotReload );
    }

    bool ContentBrowserPanel::isReferenceIndexCurrent() const
    {
        return _referenceSearch.isCurrent( ContentBrowserPanelInternal::getContentChangeSerial() );
    }

    bool ContentBrowserPanel::isWaitingForReferenceIndex() const
    {
        const bool bShowingContent = isOpen() && _bContentVisible == SW_TRUE;
        return bShowingContent && isReferenceIndexCurrent() == false;
    }

    void ContentBrowserPanel::onPanelCollapsed()
    {
        _bContentVisible = SW_FALSE;
    }

    void ContentBrowserPanel::onPanelClosed()
    {
        _bContentVisible = SW_FALSE;
    }

    void ContentBrowserPanel::selectAfterFileChange( string_view absolutePath )
    {
        // 감시도 같은 변경을 알리지만 한두 프레임 늦다. 결과를 아는 위치에서 바로 다시 읽는다.
        _selectedAssetAbs = FileUtil::normalizeSeparators( absolutePath );
        _bFolderDirty     = SW_TRUE;
        _folderCache.clear();
        // 이 변경 전에 시작한 스캔은 이동이 고친 색인을 오래된 결과로 덮어쓴다. 그 결과를 버리고 새로 스캔한다.
        _referenceSearch.restartPendingScan( ContentBrowserPanelInternal::getContentChangeSerial() );
    }

    bool ContentBrowserPanel::createNewFolder()
    {
        string folderAbs;
        if ( ContentBrowserFileActions::createFolder( _selectedFolderAbs, folderAbs ) == false )
            return false;
        selectAfterFileChange( folderAbs );
        return true;
    }

    bool ContentBrowserPanel::createNewAsset( const EditorNewAssetTemplate& assetTemplate )
    {
        string assetAbs;
        if ( ContentBrowserFileActions::createAsset( _selectedFolderAbs, assetTemplate, assetAbs ) == false )
            return false;
        selectAfterFileChange( assetAbs );
        return true;
    }

    bool ContentBrowserPanel::duplicateAsset( string_view absolutePath )
    {
        string assetAbs;
        if ( ContentBrowserFileActions::duplicateAsset( absolutePath, assetAbs ) == false )
            return false;
        selectAfterFileChange( assetAbs );
        return true;
    }

    void ContentBrowserPanel::beginRename( string_view absolutePath )
    {
        string stem;
        if ( _fileActions.beginRename( absolutePath, stem ) == false )
            return;
        _renameBuffer.assign( stem.c_str() );
        _bFocusRenameInput = SW_TRUE;
    }

    EditorReferenceIndex* ContentBrowserPanel::findCurrentReferenceIndex()
    {
        EditorReferenceIndex* pIndex = _referenceSearch.findCurrentIndex( ContentBrowserPanelInternal::getContentChangeSerial() );
        if ( pIndex == nullptr )
            SW_LOG_WARNING( "The reference index has not caught up with the last file change yet - nothing was moved, try again in a moment" );
        return pIndex;
    }

    bool ContentBrowserPanel::commitRename( string_view newName )
    {
        // 색인이 따라오기 전이면 입력란을 열어 둔 채 돌아온다 — Enter 를 다시 누르면 된다.
        EditorReferenceIndex* pIndex = findCurrentReferenceIndex();
        if ( pIndex == nullptr )
            return false;
        string destinationAbs;
        if ( _fileActions.commitRename( newName, *pIndex, destinationAbs ) == false )
            return false;
        selectAfterFileChange( destinationAbs );
        return true;
    }

    bool ContentBrowserPanel::moveAssetToFolder( string_view sourceAbs, string_view folderAbs )
    {
        EditorReferenceIndex* pIndex = findCurrentReferenceIndex();
        if ( pIndex == nullptr )
            return false;
        string destinationAbs;
        if ( ContentBrowserFileActions::moveAssetToFolder( sourceAbs, folderAbs, *pIndex, destinationAbs ) == false )
            return false;
        selectAfterFileChange( destinationAbs );
        return true;
    }

    bool ContentBrowserPanel::isRenaming( const AssetEntry& entry ) const
    {
        const string& renamingAbs = _fileActions.getRenamingPath();
        return renamingAbs.empty() == false && FileUtil::pathsEqualNormalized( entry._absolutePath, renamingAbs );
    }

    void ContentBrowserPanel::acceptAssetDrop( string_view folderAbs )
    {
        string assetID;
        if ( EditorWidgets::acceptAssetDrop( assetID ) )
            (void)moveAssetToFolder( AssetPaths::findLooseFile( assetID ).getDiskPath(), folderAbs ); // 실패는 moveAsset 이 알린다
    }

    void ContentBrowserPanel::drawRenameField( float32 width )
    {
        if ( width > 0.0f )
            ImGui::SetNextItemWidth( width );
        if ( _bFocusRenameInput == SW_TRUE )
        {
            ImGui::SetKeyboardFocusHere();
            _bFocusRenameInput = SW_FALSE;
        }
        const bool bEntered = ImGui::InputText( "##cb_rename", _renameBuffer.data(), _renameBuffer.capacity(),
                                                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll );
        EditorSelfTestMarks::note( "contentBrowser.rename" );
        const bool bEscaped = EditorInputGUI::wasCancelPressed();
        if ( bEscaped )
        {
            _fileActions.cancelRename();
            return;
        }
        if ( bEntered || ImGui::IsItemDeactivated() )
            (void)commitRename( _renameBuffer.c_str() ); // 실패(이름 규칙 · 이미 있음)는 moveAsset 이 알린다
    }

    void ContentBrowserPanel::drawAddMenu()
    {
        if ( ImGui::BeginPopup( "##cb_add" ) == false )
            return;
        if ( ImGui::MenuItem( EditorThemeUtil::makeIconLabel( editoricon::kFolder, "New Folder" ) ) )
            (void)createNewFolder(); // 실패는 createFolder 가 알린다
        EditorSelfTestMarks::note( "contentBrowser.add.folder" );
        ImGui::Separator();
        StringBuilder<constant::kMaxBuffer64> mark;
        for ( const EditorNewAssetTemplate& assetTemplate : EditorAssetFileCommands::getNewAssetTemplateList() )
        {
            if ( ImGui::MenuItem( assetTemplate._pLabel ) )
                (void)createNewAsset( assetTemplate ); // 실패는 createAsset 이 알린다
            mark.clear();
            mark.append( "contentBrowser.add." ).append( assetTemplate._pID );
            EditorSelfTestMarks::note( mark.c_str() );
        }
        ImGui::EndPopup();
    }

    void ContentBrowserPanel::showReferences( string_view absolutePath, bool bDependencies )
    {
        _referenceSearch.query( absolutePath, bDependencies );
        _bOpenReferenceResults = SW_TRUE;
    }

    void ContentBrowserPanel::drawReferenceResults()
    {
        if ( _bOpenReferenceResults == SW_TRUE )
        {
            EditorInputGUI::openPopup( ContentBrowserPanelInternal::kReferenceResultsPopupName );
            _bOpenReferenceResults = SW_FALSE;
            // 마우스 위치에 열면 아래쪽 도킹 노드에서 테이블이 화면 밖으로 나간다. 주 뷰포트 가운데에 연다.
            ImGui::SetNextWindowPos( ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2( 0.5f, 0.5f ) );
        }
        if ( ImGui::BeginPopup( ContentBrowserPanelInternal::kReferenceResultsPopupName ) == false )
            return;

        const bool                          bDependencies       = _referenceSearch.isDependencyQuery();
        const vector<EditorAssetReference>& listReferenceResult = _referenceSearch.getResultList();
        ImGui::TextUnformatted( bDependencies ? "Dependencies of" : "References to" );
        ImGui::SameLine();
        ImGui::TextColored( EditorColorUtil::toImVec4( ContentBrowserPanelInternal::kReferenceQueryText ), "%s", _referenceSearch.getQueryID().c_str() );
        if ( _referenceSearch.getIndex().isReady() == false )
        {
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorContentBrowser", "IndexingReferences" ) );
            ImGui::EndPopup();
            return;
        }
        if ( listReferenceResult.empty() )
        {
            EditorWidgets::drawEmptyHint( bDependencies ? SW_LOCKEY( "EditorContentBrowser", "NoDependencies" ) : SW_LOCKEY( "EditorContentBrowser", "NoReferencers" ) );
            ImGui::EndPopup();
            return;
        }

        constexpr ImGuiTableFlags kTableFlags  = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
        const float32             dpiScale     = EditorThemeUtil::getDPIScale();
        const size_t              visibleCount = std::min( listReferenceResult.size(), ContentBrowserPanelInternal::kReferenceVisibleRowCount );
        const float32             tableHeight  = ImGui::GetTextLineHeightWithSpacing() * static_cast<float32>( visibleCount + 2 );
        string                    openPath;
        if ( ImGui::BeginTable( "##cb_references", 2, kTableFlags, ImVec2( measureExtent( EditorTextExtent::ofText( 75.0f, 0.0f ) ).x, tableHeight ) ) )
        {
            ImGui::TableSetupColumn( "Asset", ImGuiTableColumnFlags_WidthStretch );
            ImGui::TableSetupColumn( "Line", ImGuiTableColumnFlags_WidthFixed, 48.0f * dpiScale );
            ImGui::TableHeadersRow();
            for ( size_t rowIndex = 0; rowIndex < listReferenceResult.size(); ++rowIndex )
            {
                const EditorAssetReference& reference = listReferenceResult[rowIndex];
                const string&               shownPath = bDependencies ? reference._targetPath : reference._referrerPath;
                ImGui::PushID( static_cast<int32>( rowIndex ) );
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex( 0 );
                ImGui::Selectable( shownPath.c_str(), false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick );
                if ( EditorInputGUI::wasItemDoubleClicked() )
                    openPath = shownPath;
                EditorWidgets::drawTooltip( SW_LOCKEY( "EditorContentBrowser", "OpenOnDoubleClick" ) );
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
        // 폴더 이동: 마우스 뒤로 · 앞으로 단추(창 위에서)와 창 액션(`Editor.Panel.content_browser` 의 Alt+Left · Backspace · Alt+Right).
        // 이름 바꾸기 · 복제 · 지우기는 `asset.*` 명령이라 입력 계층이 실행한다.
        const EditorPointerState pointer  = EditorInputGUI::readPointer();
        const bool               bHovered = ImGui::IsWindowHovered( ImGuiHoveredFlags_RootAndChildWindows );
        const bool               bBack    = ( bHovered && pointer.wasPressed( EditorMouseButton::Back ) ) || EditorInputGUI::wasActionTriggered( ContentBrowserPanelInternal::kActionNavigateBack );
        const bool               bForward = ( bHovered && pointer.wasPressed( EditorMouseButton::Forward ) ) || EditorInputGUI::wasActionTriggered( ContentBrowserPanelInternal::kActionNavigateForward );
        if ( bBack )
            navigateBack();
        else if ( bForward )
            navigateForward();

        _lastFallbackGlyphCount = _fallbackGlyphCount;
        _fallbackGlyphCount     = 0;
        _thumbnailCache.update();
        // 썸네일은 워커가 읽고 프레임마다 몇 개씩 올린다 — 끝날 때까지 에디터 유휴(창 메시지까지 잠)로 들지 않게 연속 프레임을 청한다.
        if ( _thumbnailCache.hasPendingLoads() )
            editor::getSubsystem<EditorContext>().getSubsystem<EditorRenderViews>().requestAnimationFrame();

        // 검색어가 있으면 하위 폴더의 파일까지 본다. 언리얼과 유니티의 프로젝트 검색과 같다.
        const bool bWantRecursive = EditorListFilter{ _searchBuffer.c_str() }.isActive();
        if ( bWantRecursive != ( _bListingRecursive == SW_TRUE ) )
        {
            _bListingRecursive = bWantRecursive ? SW_TRUE : SW_FALSE;
            _bFolderDirty      = SW_TRUE;
        }

        if ( _bRootsDirty == SW_TRUE )
            refreshRoots();
        processPendingImports();
        _bContentVisible = SW_TRUE;
        syncWithContentChanges();
        syncReferenceIndex();
        if ( _bFolderDirty == SW_TRUE )
            refreshCurrentFolder();
        vector<EditorFolderListingEntry> listNewEntry;
        if ( _folderJob.take( listNewEntry ) == EditorTaskPoll::Ready )
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

        // 기본은 활성 게임 팩 하나다. 언리얼의 Content Browser 가 프로젝트 콘텐츠만 보이는 것과 같다. All packs 를 켜면 game/ 의 팩마다 루트가 하나씩 생긴다.
        if ( _bShowAllPacksInitialized == SW_FALSE )
        {
            _bShowAllPacks            = getPreferences<EditorContentBrowserPreferences>()._bShowAllPacksByDefault ? SW_TRUE : SW_FALSE;
            _bShowAllPacksInitialized = SW_TRUE;
        }
        const string& packRoot = editor::getSubsystem<const GameConfig>()._packRoot.get();
        if ( packRoot.empty() == false )
            addRoot( packRoot, AssetPaths::findLooseDomainFolder( packRoot ).getDiskPath() );
        if ( _bShowAllPacks == SW_TRUE || packRoot.empty() )
        {
            vector<string> listPackFolder;
            (void)FileUtil::collectFolders( AssetPaths::findLooseDomainFolder( "game" ).getDiskPath(), listPackFolder, false ); // game/ 이 없으면 팩 루트가 없다
            std::sort( listPackFolder.begin(), listPackFolder.end() );
            for ( const string& packFolder : listPackFolder )
            {
                const string packName = string( ContentBrowserPanelInternal::kGameDomainPrefix ) + FileUtil::normalizePath( FileUtil::getFileNamePart( packFolder ) );
                if ( packName != FileUtil::normalizePath( packRoot ) )
                    addRoot( packName, packFolder );
            }
        }
        addRoot( "engine", AssetPaths::findLooseDomainFolder( "engine" ).getDiskPath() );
        addRoot( "common", AssetPaths::findLooseDomainFolder( "common" ).getDiskPath() );
        addRoot( "editor", AssetPaths::findLooseDomainFolder( "editor" ).getDiskPath() );

        // 현재 폴더가 숨긴 팩 안에 있으면 첫 루트로 이동한다.
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
        return VectorUtil::containsIf( _listRoot, [&]( const ContentRoot& root )
        { return FileUtil::normalizePath( root._displayName ) == FileUtil::normalizePath( displayName ); } );
    }

    void ContentBrowserPanel::refreshCurrentFolder()
    {
        _folderJob.request( _selectedFolderAbs, _bListingRecursive == SW_TRUE );
        _bFolderDirty = SW_FALSE;
    }

    void ContentBrowserPanel::applyFolderListing( vector<EditorFolderListingEntry>& listEntry )
    {
        // 목록을 보기만 하고 `.meta` 를 쓰지 않는다. GUID 는 그 에셋을 쓰는 시스템(머티리얼 캐시, 프리팹, 씬 저장, 임포트)이 만든다.
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
            drawNavigationControls();
            drawViewControls();
            drawFileControls();
        }
        EditorChrome::endToolbar();
    }

    void ContentBrowserPanel::drawNavigationControls()
    {
        const bool bCanBack = _history.canNavigateBack();
        if ( bCanBack == false )
            ImGui::BeginDisabled();
        if ( ImGui::Button( "<##cb_nav_back" ) )
            navigateBack();
        if ( bCanBack == false )
            ImGui::EndDisabled();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorContentBrowser", "NavigateBack" ) );

        ImGui::SameLine();
        const bool bCanForward = _history.canNavigateForward();
        if ( bCanForward == false )
            ImGui::BeginDisabled();
        if ( ImGui::Button( ">##cb_nav_forward" ) )
            navigateForward();
        if ( bCanForward == false )
            ImGui::EndDisabled();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorContentBrowser", "NavigateForward" ) );

        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( EditorThemeUtil::makeIconLabel( editoricon::kPlus, "Add" ) ) );
        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kPlus, "Add" ) ) )
            EditorInputGUI::openPopup( "##cb_add" );
        EditorSelfTestMarks::note( "contentBrowser.add" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorContentBrowser", "AddAsset" ) );
        drawAddMenu();

        ImGui::SameLine();
        EditorWidgets::drawSearchField( "##cb_search", _searchBuffer, SW_LOCKEY( "EditorContentBrowser", "SearchHint" ), 160.0f, false );
        EditorSelfTestMarks::note( "contentBrowser.search" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorContentBrowser", "SearchTooltip" ) );
    }

    void ContentBrowserPanel::drawViewControls()
    {
        ImGui::SameLine();
        uint32                          filterCount{ 0 };
        const EditorAssetBrowserFilter* pFilter  = EditorAssetTypeRegistry::getBrowserFilters( filterCount );
        const utf8*                     pPreview = "All";
        if ( _filterIndex < filterCount )
            pPreview = pFilter[_filterIndex]._pLabel;
        EditorChrome::setNextItemWidthColumns( 15.0f );
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
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorContentBrowser", "TypeFilter" ) );

        EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( "Tiles" ) );
        if ( ImGui::RadioButton( "Tiles", _viewMode == ViewMode::Tiles ) )
            _viewMode = ViewMode::Tiles;
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorContentBrowser", "TilesView" ) );

        EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( "List" ) );
        if ( ImGui::RadioButton( "List", _viewMode == ViewMode::List ) )
            _viewMode = ViewMode::List;
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorContentBrowser", "ListView" ) );

        if ( _viewMode == ViewMode::Tiles )
        {
            ImGui::SameLine();
            EditorChrome::setNextItemWidthColumns( 11.0f );
            ImGui::SliderFloat( "##cb_tile", &_tileSize, 64.0f, 160.0f, "%.0f" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorContentBrowser", "TileSize" ) );
        }
    }

    void ContentBrowserPanel::drawFileControls()
    {
        ImGui::SameLine();
        const bool canImport = _selectedFolderAbs.empty() == false;
        if ( canImport == false )
            ImGui::BeginDisabled();
        const bool importClicked = ImGui::Button( "Import..." );
        if ( canImport == false )
        {
            ImGui::EndDisabled();
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorContentBrowser", "ImportNeedsFolder" ) );
        }
        else
        {
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorContentBrowser", "Import" ) );
        }
        if ( importClicked )
            importFilesFromDialog();

        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Refresh" ) );
        if ( ImGui::Button( "Refresh" ) )
        {
            _folderCache.clear();
            _listEntry.clear();
            _selectedAssetAbs.clear();
            refreshRoots();
            refreshCurrentFolder();
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorContentBrowser", "Refresh" ) );

        bool bShowAllPacks = _bShowAllPacks == SW_TRUE;
        EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( "All packs" ) );
        if ( ImGui::Checkbox( "All packs", &bShowAllPacks ) )
            setShowAllPacks( bShowAllPacks );
        EditorSelfTestMarks::note( "contentBrowser.allPacks" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorContentBrowser", "AllPacks" ) );
    }

    void ContentBrowserPanel::drawSourcesSection()
    {
        editor::EditorSectionDesc sourcesDesc{};
        sourcesDesc._pID       = "##cb_sources";
        sourcesDesc._kind      = editor::EditorSectionKind::Child;
        sourcesDesc._childSize = float2{ 220.0f, 0.0f };
        // 오른쪽 Assets 영역과 같은 높이로 맞춰서 아래의 개수 행(drawCountLabel)이 창 안에 남게 한다. 높이가 0(남은 전부)이면 그 행이 창 밖으로 밀려 패널 전체가 스크롤된다.
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
                                         ? AssetPaths::findLooseDomainFolder( "engine", kArrFavorites[favIdx]._pRelPath ).getDiskPath()
                                         : AssetPaths::findLooseDomainFolder( editor::getSubsystem<const GameConfig>()._packRoot.get(), kArrFavorites[favIdx]._pRelPath ).getDiskPath();
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
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorContentBrowser", "NoResourceRoots" ) );

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

        // 하위 폴더는 처음 그릴 때 한 번만 디스크에서 읽는다. Refresh 와 파일 감시가 캐시를 비운다. 참조는 재귀 중에도 유효하다.
        const vector<string>& listChild    = _folderCache.getOrScanChildFolders( absPath );
        const bool            hasChildDirs = listChild.empty() == false;
        if ( hasChildDirs == false )
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

        ImGui::PushID( absPath.c_str() );
        const bool   bDefaultOpen  = ( ( flags & ImGuiTreeNodeFlags_DefaultOpen ) != 0 );
        const utf8*  pFolderIcon   = EditorThemeUtil::getFolderIcon( bDefaultOpen );
        const string labelWithIcon = string( pFolderIcon ) + "  " + string( label );
        const Color  folderColor   = EditorThemeUtil::getFolderColor();
        ImGui::PushStyleColor( ImGuiCol_Text, EditorColorUtil::toImVec4( folderColor.withAlpha( 1.0f ) ) );
        const bool opened = ImGui::TreeNodeEx( labelWithIcon.c_str(), flags );
        ImGui::PopStyleColor();
        if ( EditorSelfTestMarks::isEnabled() )
            ContentBrowserPanelInternal::noteMark( "contentBrowser.folder.", AssetPaths::toAssetID( absPath ) );
        acceptAssetDrop( absPath );

        if ( EditorInputGUI::wasItemClicked() && ImGui::IsItemToggledOpen() == false )
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
        // 엔트리를 복사하지 않는다. `AssetEntry` 는 `string` 이 네 개라서 복사하면 애셋이
        // 수백 개일 때 프레임마다 문자열 수천 개를 복사한다. 이 목록을 쓰는 두 곳은 읽기만 하므로 포인터로
        // 충분하고, 버퍼도 멤버로 두면 첫 프레임 이후로는 할당이 없다.
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

        // 필터에 걸러져서 0건인 것과 폴더가 정말 비어 있는 것은 다르게 안내한다.
        const EditorListFilter filter{ _searchBuffer.c_str() };
        if ( listVisible.empty() && filter.isActive() )
            EditorWidgets::drawNoSearchResultHint( filter.getText() );
        else if ( _viewMode == ViewMode::Tiles )
            drawTilesView( listVisible );
        else
            drawListView( listVisible );

        EditorChrome::endSection();

        EditorWidgets::drawCountLabel( static_cast<uint32>( listVisible.size() ), 0, "items" );
        if ( _referenceSearch.isScanPending() && _referenceSearch.getIndex().isReady() == false )
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
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorContentBrowser", "SelectFolder" ) );
            return;
        }

        // 조각마다 절대 경로를 가진다. 누르면 그 조각까지의 브레드크럼 경로와 함께 그 폴더로 이동한다. 경로가 비어 있는 조각("Favorites")은 누를 수 없다.
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
                const Color accentColor = EditorThemeUtil::getAccentColor();
                ImGui::TextColored( EditorColorUtil::toImVec4( accentColor.withAlpha( 1.0f ) ), "%s", editoricon::kFolderOpen );
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
        // 버튼과 이름 행이다. 셀 높이에 이름 줄 수를 더한 크기이며, drawClampedLabel 은 항상 그 줄 수만큼 공간을 잡는다
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

                    const bool  selected = FileUtil::pathsEqualNormalized( entry._absolutePath, _selectedAssetAbs ) || ( entry._bIsDirectory && FileUtil::pathsEqualNormalized( entry._absolutePath, _selectedFolderAbs ) );
                    const Color accent   = EditorThemeUtil::getAccentColor();
                    const Color frameBg  = EditorThemeUtil::getFrameBgColor();
                    ImGui::PushStyleColor( ImGuiCol_Button, EditorColorUtil::toImVec4( selected ? accent.withAlpha( 0.40f ) : frameBg.withAlpha( 0.60f ) ) );
                    ImGui::PushStyleColor( ImGuiCol_ButtonHovered, EditorColorUtil::toImVec4( accent.withAlpha( 0.60f ) ) );

                    ImGui::BeginGroup();
                    const ImVec2 cursor = ImGui::GetCursorScreenPos();
                    if ( ImGui::Button( "##tile", ImVec2( cell, cell ) ) )
                        selectAsset( entry );
                    ContentBrowserPanelInternal::noteMark( "contentBrowser.asset.", entry._name );
                    drawAssetContextMenu( entry );
                    if ( EditorInputGUI::wasItemDoubleClicked() )
                        openAsset( entry );
                    if ( entry._bIsDirectory == false )
                        EditorWidgets::drawAssetDragSource( entry._relativePath.c_str(), true );
                    else
                        acceptAssetDrop( entry._absolutePath );

                    ImDrawList*       pDrawList = ImGui::GetWindowDrawList();
                    constexpr float32 inset     = 6.0f;
                    drawAssetThumbnail( pDrawList, float2{ cursor.x + inset, cursor.y + inset },
                                        float2{ cursor.x + cell - inset, cursor.y + cell * 0.65f }, entry );

                    drawSourceControlBadge( pDrawList, float2{ cursor.x + cell - inset, cursor.y + inset }, entry );

                    if ( isRenaming( entry ) )
                        drawRenameField( cell );
                    else
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
            ImGui::TableSetupColumn( "Type", ImGuiTableColumnFlags_WidthFixed, 90.0f * EditorThemeUtil::getDPIScale() );
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
                    const Color  assetColor   = EditorThemeUtil::getAssetColorForPath( entry._name, entry._bIsDirectory );
                    const string statusText   = describeSourceControlStatus( entry );
                    const string nameWithIcon = string( pAssetIcon ) + "  " + entry._name + ( statusText.empty() ? string() : string( "  " ) + editoricon::kLock );
                    ImGui::PushStyleColor( ImGuiCol_Text, EditorColorUtil::toImVec4( assetColor ) );
                    const bool bSelected = ImGui::Selectable( nameWithIcon.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick );
                    ImGui::PopStyleColor();
                    if ( bSelected )
                    {
                        selectAsset( entry );
                        if ( EditorInputGUI::wasItemDoubleClicked() )
                            openAsset( entry );
                    }
                    if ( statusText.empty() == false && ImGui::IsItemHovered() )
                        ImGui::SetTooltip( "%s", statusText.c_str() );
                    ContentBrowserPanelInternal::noteMark( "contentBrowser.asset.", entry._name );
                    drawAssetContextMenu( entry );
                    if ( entry._bIsDirectory == false )
                        EditorWidgets::drawAssetDragSource( entry._relativePath.c_str() );
                    else
                        acceptAssetDrop( entry._absolutePath );
                    if ( isRenaming( entry ) )
                    {
                        ImGui::SameLine();
                        drawRenameField( 0.0f );
                    }

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
        // 이력을 적지 않고 고르므로 항목 포인터는 selectFolder 안에서도 유효하다.
        const ContentBrowserHistoryEntry* pEntry = _history.stepBack();
        if ( pEntry != nullptr )
            selectFolder( pEntry->_folderPathAbs, pEntry->_listCrumb, false );
    }

    void ContentBrowserPanel::navigateForward()
    {
        const ContentBrowserHistoryEntry* pEntry = _history.stepForward();
        if ( pEntry != nullptr )
            selectFolder( pEntry->_folderPathAbs, pEntry->_listCrumb, false );
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
            _history.record( normalizedPath, listCrumb );

        // 탐색과 I/O 는 실제 파일 시스템의 대소문자를 쓰고, 저장 경로는 AssetPaths::makeSavePath 가 상대 경로 조각을 소문자로 바꾼다.
        _selectedFolderAbs = normalizedPath;
        _listCrumb         = listCrumb;
        _selectedAssetAbs.clear();
        _bFolderDirty = SW_TRUE;
    }

    void ContentBrowserPanel::selectAsset( const AssetEntry& entry )
    {
        EditorContext& editorContext = editor::getSubsystem<EditorContext>();

        _selectedAssetAbs = entry._absolutePath;
        if ( entry._bIsDirectory == false )
            editorContext.getSubsystem<EditorWorkspace>().setFocusedAssetPath( entry._relativePath.c_str() );
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

        _importQueue.openDialog( _selectedFolderAbs );
    }

    void ContentBrowserPanel::processPendingImports()
    {
        vector<string> listPath;
        if ( _importQueue.takePending( listPath ) == false )
            return;

        if ( _selectedFolderAbs.empty() )
        {
            SW_LOG_WARNING( "Import cancelled — no destination folder." );
            return;
        }

        if ( EditorAssetFileCommands::importFiles( _selectedFolderAbs, listPath ) > 0 )
            _bFolderDirty = SW_TRUE;
    }
} // namespace sw::editor
