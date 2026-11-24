/**
 * @file ContentBrowserCommands.cpp
 * @brief 에셋 맥락 메뉴 · 단축키 명령(`asset.*`)과 `content.open` 입니다. 메뉴 · 키 · 팔레트가 같은 명령을 부른다(R4).
 * @details 명령은 모델을 바꾸므로 View 가 아니라 여기(Model)에 있다. 버전 관리 · 탐색기 · 클립보드는 바로 부르고, 창이 열어야 하는 것(이름 칸 · 지우기 확인 ·
 *          참조 결과 창 · 폴더 이동)은 ViewModel 요청(`ContentBrowserViewModel::postRequest`)으로 넣는다 — 창이 닫혀 있으면 열고 꺼낸다(언리얼 Sync to Content Browser).
 *          대상 id 는 리소스 id 를 올린 에셋 페이로드 id 다(`EditorWidgets::registerAssetPayload` — 끌기와 같은 한 형식). 대상이 없으면(단축키) 창이 내건 지금 선택이다.
 */
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"

#include "Editor/Common/Asset/EditorAssetIndex.h"
#include "Editor/Common/Commands/EditorCommandIDs.h"
#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/GUI/EditorClipboard.h"
#include "Editor/Common/SourceControl/EditorSourceControl.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Panels/ContentBrowser/Model/ContentBrowserViewModel.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Content/AssetPaths.h"

namespace sw::editor
{
    namespace
    {
        struct ContentBrowserCommandsInternal
        {
            /** @brief 에셋 페이로드 id 의 디스크 자리(절대 경로)입니다. 모르는 id 이거나 디스크에 없으면 빈 문자열입니다. */
            static AbsolutePath findAssetDiskPath( uint64 assetID )
            {
                string resourceID;
                if ( EditorWidgets::findAssetPayload( assetID, resourceID ) == false )
                    return {};
                const optional<AbsolutePath> diskPath = EditorPaths::getAssetPaths().findLooseFile( resourceID );
                return diskPath.has_value() ? *diskPath : AbsolutePath{};
            }

            /** @brief 대상 에셋의 절대 경로입니다. 누른 항목 → 선택 스냅숏 → 창의 지금 선택 순서로 고릅니다. 없으면 빈 문자열입니다. */
            static AbsolutePath findTargetPath( const EditorCommandTarget& target )
            {
                const uint64 targetID = target._hoveredID != 0 ? target._hoveredID : ( target._listSelectedID.empty() ? 0 : target._listSelectedID.front() );
                if ( targetID != 0 )
                    return findAssetDiskPath( targetID ); // 모르는 id · 디스크에 없는 에셋이면 빈 경로 — 명령이 꺼진다
                return ContentBrowserViewModel::findPublishedSelection();
            }

            static bool hasTarget( const EditorCommandTarget& target ) { return findTargetPath( target ).empty() == false; }

            /** @brief 대상이 폴더가 아닌 파일이면 true 입니다(이름 바꾸기 · 복제 · 지우기 · 참조 · 잠금). */
            static bool hasFileTarget( const EditorCommandTarget& target )
            {
                const AbsolutePath path = findTargetPath( target );
                return path.empty() == false && FileUtil::isDirectory( path ) == false;
            }

            static bool hasTextAssetTarget( const EditorCommandTarget& target )
            {
                const string path = findTargetPath( target ).str();
                return path.empty() == false && EditorAssetIndex::isTextAsset( path );
            }

            static EditorSourceControl& getSourceControl() { return ContentBrowserViewModel::getSourceControl(); }

            static bool canLock( const EditorCommandTarget& target )
            {
                return hasFileTarget( target ) && getSourceControl().getProvider().canLock() && getSourceControl().findLock( findTargetPath( target ).str() ) == nullptr;
            }

            static bool canUnlock( const EditorCommandTarget& target )
            {
                return hasFileTarget( target ) && getSourceControl().getProvider().canLock() && getSourceControl().findLock( findTargetPath( target ).str() ) != nullptr;
            }

            static bool canRefreshSourceControl( const EditorCommandTarget& target ) { return hasFileTarget( target ) && getSourceControl().getProvider().canLock(); }

            static void lockAsset( const EditorCommandTarget& target ) { (void)getSourceControl().requestLock( findTargetPath( target ).str() ); } // 결과는 공급자가 알린다

            static void unlockAsset( const EditorCommandTarget& target ) { (void)getSourceControl().requestUnlock( findTargetPath( target ).str() ); } // 결과는 공급자가 알린다

            static void refreshSourceControl( const EditorCommandTarget& /*target*/ ) { getSourceControl().requestRefresh(); }

            static void showInExplorer( const EditorCommandTarget& target ) { ContentBrowserViewModel::showInFileExplorer( findTargetPath( target ).str() ); }

            static void copyRelativePath( const EditorCommandTarget& target ) { EditorClipboard::setText( EditorPaths::getAssetPaths().toAssetID( findTargetPath( target ) ) ); }

            static void copyAbsolutePath( const EditorCommandTarget& target ) { EditorClipboard::setText( findTargetPath( target ).str() ); }

            /** @brief 창 요청 @p kind 를 대상 에셋으로 넣습니다. 대상이 없으면 아무것도 하지 않습니다. */
            static void postForTarget( ContentBrowserRequestKind kind, const EditorCommandTarget& target )
            {
                const AbsolutePath path = findTargetPath( target );
                if ( path.empty() == false )
                    ContentBrowserViewModel::postRequest( kind, path );
            }

            /** @brief 대상 에셋의 폴더를 열고 그 에셋을 고릅니다(언리얼 Browse to Asset — 인스펙터 에셋 칸의 행 메뉴가 부른다). */
            static void revealAsset( const EditorCommandTarget& target ) { postForTarget( ContentBrowserRequestKind::Reveal, target ); }

            static void findReferences( const EditorCommandTarget& target ) { postForTarget( ContentBrowserRequestKind::ShowReferences, target ); }

            static void showDependencies( const EditorCommandTarget& target ) { postForTarget( ContentBrowserRequestKind::ShowDependencies, target ); }

            static void renameAsset( const EditorCommandTarget& target ) { postForTarget( ContentBrowserRequestKind::Rename, target ); }

            static void duplicateAsset( const EditorCommandTarget& target ) { postForTarget( ContentBrowserRequestKind::Duplicate, target ); }

            /** @brief 지우기 전에 확인한다(파일은 휴지통으로). 모달은 창이 메뉴 밖의 창 단위에서 연다. */
            static void deleteAsset( const EditorCommandTarget& target ) { postForTarget( ContentBrowserRequestKind::Delete, target ); }

            /** @brief `content.open <리소스 폴더>` 명령입니다. 콘텐츠 브라우저를 열고 그 폴더로 이동합니다. 언리얼의 Sync to Content Browser 와 같습니다. */
            static bool runContentOpen( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 )
                    return false;
                const optional<AbsolutePath> folderPath = EditorPaths::getAssetPaths().findLooseFile( FileUtil::normalizePath( listArgument[0] ).str() );
                const AbsolutePath           folderAbs  = folderPath.has_value() ? *folderPath : AbsolutePath{};
                if ( FileUtil::isDirectory( folderAbs ) == false )
                    return false;
                ContentBrowserViewModel::postRequest( ContentBrowserRequestKind::OpenFolder, folderAbs );
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "content browser at %#", listArgument[0] );
                outReply = string( replyBuilder.view() );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( ContentOpen, "content.open", "content.open <resource folder>", "Show a resource folder in the Content Browser (e.g. game/playground/models)",
                    &ContentBrowserCommandsInternal::runContentOpen );
    // 순서 칸(`*.commands.xml`): 100 탐색기 · 경로, 200 참조, 300 버전 관리, 900 파일 편집.
    SW_EDITOR_CONTEXT_COMMAND( AssetShowInExplorer, "asset.showInExplorer", &ContentBrowserCommandsInternal::showInExplorer, &ContentBrowserCommandsInternal::hasTarget, NoScene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( AssetCopyRelativePath, "asset.copyRelativePath", &ContentBrowserCommandsInternal::copyRelativePath, &ContentBrowserCommandsInternal::hasTarget, NoScene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( AssetCopyAbsolutePath, "asset.copyAbsolutePath", &ContentBrowserCommandsInternal::copyAbsolutePath, &ContentBrowserCommandsInternal::hasTarget, NoScene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( AssetBrowseTo, editorcommand::kAssetBrowseTo, &ContentBrowserCommandsInternal::revealAsset, &ContentBrowserCommandsInternal::hasFileTarget, NoScene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( AssetFindReferences, "asset.findReferences", &ContentBrowserCommandsInternal::findReferences, &ContentBrowserCommandsInternal::hasFileTarget, NoScene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( AssetShowDependencies, "asset.showDependencies", &ContentBrowserCommandsInternal::showDependencies, &ContentBrowserCommandsInternal::hasTextAssetTarget, NoScene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( AssetLock, "asset.lock", &ContentBrowserCommandsInternal::lockAsset, &ContentBrowserCommandsInternal::canLock, NoScene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( AssetUnlock, "asset.unlock", &ContentBrowserCommandsInternal::unlockAsset, &ContentBrowserCommandsInternal::canUnlock, NoScene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( AssetRefreshSourceControl, "asset.refreshSourceControl", &ContentBrowserCommandsInternal::refreshSourceControl, &ContentBrowserCommandsInternal::canRefreshSourceControl, NoScene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( AssetRename, "asset.rename", &ContentBrowserCommandsInternal::renameAsset, &ContentBrowserCommandsInternal::hasFileTarget, NoScene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( AssetDuplicate, "asset.duplicate", &ContentBrowserCommandsInternal::duplicateAsset, &ContentBrowserCommandsInternal::hasFileTarget, NoScene, nullptr );
    SW_EDITOR_CONTEXT_COMMAND( AssetDelete, "asset.delete", &ContentBrowserCommandsInternal::deleteAsset, &ContentBrowserCommandsInternal::hasFileTarget, NoScene, nullptr );
} // namespace sw::editor
