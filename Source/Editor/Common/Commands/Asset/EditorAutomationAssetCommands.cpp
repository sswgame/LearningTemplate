/**
 * @file EditorAutomationAssetCommands.cpp
 * @brief 시나리오가 저장소 리소스를 바꾸지 않고 에셋 편집 · 임포트 · 프리팹 만들기를 거치게 하는 콘솔 명령입니다(`asset.open` · `asset.import` · `prefab.create` …).
 * @details 쓰는 자리는 작업 폴더의 `Saved/Automation/`(리소스 트리 밖)과 사본 도메인의 `scratch/import` 만 받는다 — 이 파일의 경로 검사가 그 문지기다.
 *          예전 `EditorDevCommands.cpp`(여러 기능의 콘솔 명령을 모은 파일)에서 뗐다. 플레이 · 씬 · 선택 명령은 그 기능의 명령 파일로 갔다.
 */
#include "pch.h"

#include "Core/Container/unordered_map.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Name/TagID.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Commands/Asset/EditorAssetCommands.h"
#include "Editor/Common/Commands/Asset/EditorAssetFileCommands.h"
#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Commands/Scene/EditorInspectorCommands.h"
#include "Editor/Common/Commands/Scene/EditorSceneCommands.h"
#include "Editor/Common/Commands/Scene/EditorSceneFileCommands.h"
#include "Editor/Common/Workspace/EditorCamera.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationScratch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DebugOverlayState.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Object/Prefab/PrefabCache.h"
#include "Engine/Reflection/TypeRegistry.h"

namespace sw::editor
{
    // 콘솔 명령 — 시나리오 · 콘솔 입력 줄이 부른다(`DevCommandRegistry`). 그 기능 옆에 둔다(언리얼 Exec 명령이 그 시스템 파일에 있는 것과 같다).
    namespace
    {
        struct EditorAutomationAssetCommandsInternal
        {
            /**
             * @brief `asset.import` 가 받는 임시 리소스 폴더(리소스 id)입니다. 사본 도메인(`AutomationScratch::kScratchDomain`) 아래라 자동화 쓰기 게이트가 허락하고
             *        `.gitignore` 의 `/Content/scratch/` 가 무시한다. 자동화가 쓰는 리소스 폴더는 이 도메인 하나다(결정 5-11).
             */
            static constexpr const utf8* kImportTempFolder = "scratch/import";

            /** @brief `asset.snapshotScratch` 가 붙잡은 사본 파일의 글입니다(id → 글). 에디터 범위의 상태 저장소가 소유한다. */
            struct ScratchSnapshotState
            {
                unordered_map<string, string> _mapTextByID{};
            };

            /** @brief 마지막으로 `asset.open` 한 임시 에셋의 상태입니다. 감시 값 `Editor.TempAssetExists` 가 읽는다. 에디터 범위의 상태 저장소가 소유한다. */
            struct TempAssetState
            {
                string _lastPath{}; ///< 마지막으로 연 임시 에셋의 절대 경로. 연 적이 없으면 비어 있다
            };

            /**
             * @brief 임시 에셋 인자를 읽습니다. 자동화 산출물 폴더(`makeAutomationOutputFolderPath`) 아래이고 리소스 트리 밖인 절대 경로일 때만 true 입니다.
             * @details 시나리오가 저장 단계를 밟아도 저장소 리소스가 바뀌지 않게 하는 문지기다. 절대 경로만 받는다(`scene.saveAs` 와 같다).
             */
            [[nodiscard]] static bool resolveTempAssetPath( const string& path, string& outAbsolutePath, string& outReply )
            {
                string absolutePath;
                if ( DevCommandRegistry::readDiskPathArgument( path, absolutePath, outReply ) == false )
                    return false;
                const string automationFolder = makeAutomationOutputFolderPath();
                if ( FileUtil::isPathAtOrUnderRoot( automationFolder, absolutePath ) == false )
                {
                    outReply = "temp assets must be under Saved/Automation/: '" + absolutePath + "'";
                    return false;
                }
                const string& resourceRoot = editor::getSubsystem<AssetPaths>().getRootFolderPath();
                if ( resourceRoot.empty() == false && FileUtil::isPathAtOrUnderRoot( resourceRoot, absolutePath ) )
                {
                    outReply = "temp assets must not be in the resource tree: '" + absolutePath + "'";
                    return false;
                }
                outAbsolutePath = absolutePath;
                return true;
            }

            /** @brief 임시 에셋 하나를 새 문서로 엽니다. 남은 옛 파일을 먼저 지워 매번 같은 상태(파일 없이 기본 문서)로 시작합니다. */
            static bool runOpenTempAsset( const vector<string>& listArgument, string& outReply )
            {
                string absolutePath;
                if ( listArgument.size() != 1 || resolveTempAssetPath( listArgument[0], absolutePath, outReply ) == false )
                    return false;
                if ( FileUtil::exists( absolutePath ) && FileUtil::removeFile( absolutePath ) == false )
                {
                    outReply = "could not remove the old temp asset '" + absolutePath + "'";
                    return false;
                }
                (void)FileUtil::ensureDirectoryExists( FileUtil::getDirectoryPart( absolutePath ) ); // 못 만들면 ensure 가 폴더와 이유를 남기고 뒤따르는 쓰기가 실패로 답한다
                if ( EditorAssetCommands::openPath( absolutePath ) == false )
                {
                    outReply = "no editor opens '" + absolutePath + "'";
                    return false;
                }
                getEditorState<TempAssetState>()._lastPath = absolutePath;
                outReply                                   = "opening " + absolutePath;
                return true;
            }

            /**
             * @brief 시나리오 사본 폴더(`<Scratch as="scratch/…"/>`)의 에셋 하나를 콘텐츠 브라우저 더블클릭처럼 엽니다.
             * @details Quick Open 은 파일 이름으로 찾아 원본과 사본이 같은 이름이면 둘 중 하나가 걸린다. 그래서 id 로 연다.
             *          `scratch/` 밖의 id 는 받지 않는다 — 시나리오가 추적 중인 원본을 열어 고치는 경로를 형식으로 막는다.
             */
            static bool runOpenScratchAsset( const vector<string>& listArgument, string& outReply )
            {
                const string scratchPrefix = string( AutomationScratch::kScratchDomain ) + "/";
                const bool   bBadID        = listArgument.size() != 1 || StringUtil::startsWith( listArgument[0], scratchPrefix ) == false || AssetPaths::hasParentDirectoryComponent( listArgument[0] );
                if ( bBadID )
                {
                    outReply = "asset.openScratch takes one resource id under " + scratchPrefix;
                    return false;
                }
                const optional<AbsolutePath> scratchPath  = editor::getSubsystem<AssetPaths>().findLooseFile( FileUtil::normalizePath( listArgument[0] ) );
                const string                 absolutePath = scratchPath.has_value() ? scratchPath->str() : string{};
                if ( absolutePath.empty() )
                {
                    outReply = "no scratch asset '" + listArgument[0] + "' (does the scenario declare <Scratch>?)";
                    return false;
                }
                // 콘텐츠 브라우저 · Quick Open 과 같게 리소스 id 로 연다(`openPath` 의 인자는 id 다).
                if ( EditorAssetCommands::openPath( listArgument[0] ) == false )
                {
                    outReply = "no editor opens '" + listArgument[0] + "'";
                    return false;
                }
                outReply = "opening " + listArgument[0];
                return true;
            }

            /**
             * @brief 사본 폴더(`scratch/…`)의 파일 하나의 글을 다른 사본 파일에 덮어씁니다 — 바깥 편집기(텍스트 편집기 · git)의 저장과 같은 길(원자적 쓰기 → 파일 감시)을 시나리오가 밟게 한다.
             * @details 두 id 모두 `scratch/` 아래만 받는다 — 추적 중인 원본을 고치는 길을 형식으로 막는다(`asset.openScratch` 와 같다). 연쇄 리로드 시나리오(assetcascade)가 부모 머티리얼을 바꾼다.
             */
            static bool runOverwriteScratchAsset( const vector<string>& listArgument, string& outReply )
            {
                const string scratchPrefix = string( AutomationScratch::kScratchDomain ) + "/";
                const auto   isScratchID   = [&scratchPrefix]( const string& id )
                { return StringUtil::startsWith( id, scratchPrefix ) && AssetPaths::hasParentDirectoryComponent( id ) == false; };
                if ( listArgument.size() != 2 || isScratchID( listArgument[0] ) == false || isScratchID( listArgument[1] ) == false )
                {
                    outReply = "asset.overwriteScratch takes two resource ids under " + scratchPrefix + " (source, target)";
                    return false;
                }
                const AssetPaths&            assetPaths = editor::getSubsystem<AssetPaths>();
                const optional<AbsolutePath> sourcePath = assetPaths.findLooseFile( FileUtil::normalizePath( listArgument[0] ) );
                const optional<AbsolutePath> targetPath = assetPaths.findLooseFile( FileUtil::normalizePath( listArgument[1] ) );
                string                       text;
                if ( sourcePath.has_value() == false || targetPath.has_value() == false || FileUtil::readTextFile( sourcePath->str(), text ) == false )
                {
                    outReply = "no scratch asset '" + listArgument[0] + "' or '" + listArgument[1] + "' (does the scenario declare <Scratch>?)";
                    return false;
                }
                if ( FileUtil::writeTextFile( targetPath->str(), text ) == false )
                {
                    outReply = "could not write '" + targetPath->str() + "'";
                    return false;
                }
                outReply = "overwrote " + listArgument[1] + " with " + listArgument[0];
                return true;
            }

            /** @brief `scratch/` 아래 id 하나를 받아 그 사본 파일의 글을 읽습니다. id 가 틀렸거나 파일이 없으면 그 까닭입니다. */
            [[nodiscard]] static expected<string> readScratchText( const vector<string>& listArgument, const utf8* pCommand )
            {
                const string scratchPrefix = string( AutomationScratch::kScratchDomain ) + "/";
                const bool   bBadID        = listArgument.size() != 1 || StringUtil::startsWith( listArgument[0], scratchPrefix ) == false || AssetPaths::hasParentDirectoryComponent( listArgument[0] );
                if ( bBadID )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "the command takes one resource id under the scratch folder", pCommand }.appendDetail( " " ).appendDetail( scratchPrefix ) );
                const optional<AbsolutePath> scratchPath = editor::getSubsystem<AssetPaths>().findLooseFile( FileUtil::normalizePath( listArgument[0] ) );
                string                       text;
                if ( scratchPath.has_value() == false || FileUtil::readTextFile( scratchPath->str(), text ) == false )
                    return unexpected( Error{ ErrorKind::NotFound, "no scratch asset (does the scenario declare <Scratch>?)", listArgument[0] } );
                return text;
            }

            /** @brief 사본 파일 하나의 지금 글을 붙잡습니다 — 저장 왕복의 첫 저장 뒤에 부르고, 다시 읽고 저장한 뒤 `asset.expectScratchUnchanged` 로 견준다. */
            static bool runSnapshotScratchAsset( const vector<string>& listArgument, string& outReply )
            {
                expected<string> text = readScratchText( listArgument, "asset.snapshotScratch" );
                if ( text.has_value() == false )
                {
                    outReply = AutomationStep::describeReason( text.error() );
                    return false;
                }
                getEditorState<ScratchSnapshotState>()._mapTextByID.insert_or_assign( listArgument[0], std::move( *text ) );
                outReply = "snapshot of " + listArgument[0];
                return true;
            }

            /**
             * @brief 사본 파일의 글이 `asset.snapshotScratch` 때와 같은지 봅니다. 다르면(또는 붙잡은 적이 없으면) 실패입니다.
             * @details 저장 → 다시 읽기 → 저장이 같은 바이트여야 한다 — 다르면 읽기가 저장한 값을 잃었거나(대칭이 아니다) 저장이 결정적이지 않다.
             */
            static bool runExpectScratchUnchanged( const vector<string>& listArgument, string& outReply )
            {
                const expected<string> text = readScratchText( listArgument, "asset.expectScratchUnchanged" );
                if ( text.has_value() == false )
                {
                    outReply = AutomationStep::describeReason( text.error() );
                    return false;
                }
                const unordered_map<string, string>& mapSnapshot = getEditorState<ScratchSnapshotState>()._mapTextByID;
                const auto                           iter        = mapSnapshot.find( listArgument[0] );
                if ( iter == mapSnapshot.end() )
                {
                    outReply = "no snapshot of '" + listArgument[0] + "' - run asset.snapshotScratch first";
                    return false;
                }
                if ( iter->second != *text )
                {
                    outReply = "'" + listArgument[0] + "' changed after save, reload and save again - loading drops or alters what saving wrote";
                    return false;
                }
                outReply = listArgument[0] + " is unchanged";
                return true;
            }

            /**
             * @brief 임시 폴더(`Saved/Automation/` 아래)를 만듭니다. 이미 있으면 성공입니다.
             * @details 에디터 저장은 없는 폴더를 만들지 않고 오류로 멈춘다(`FileUtil::writeTextFile`). 패널이 문서를 열지 않고 쓸 곳을 먼저 마련해야 하는 시나리오(타일맵 저장 왕복)가 쓴다.
             */
            static bool runMakeTempFolder( const vector<string>& listArgument, string& outReply )
            {
                string folderPath;
                if ( listArgument.size() != 1 || resolveTempAssetPath( listArgument[0], folderPath, outReply ) == false )
                    return false;
                if ( FileUtil::ensureDirectoryExists( folderPath ) == false )
                {
                    outReply = "could not create '" + folderPath + "'";
                    return false;
                }
                outReply = "ready " + folderPath;
                return true;
            }

            /** @brief 시나리오가 끝날 때 임시 에셋 파일을 지웁니다. 파일이 없으면 할 일이 없으므로 성공입니다. */
            static bool runRemoveTempAsset( const vector<string>& listArgument, string& outReply )
            {
                string absolutePath;
                if ( listArgument.size() != 1 || resolveTempAssetPath( listArgument[0], absolutePath, outReply ) == false )
                    return false;
                if ( FileUtil::exists( absolutePath ) && FileUtil::removeFile( absolutePath ) == false )
                {
                    outReply = "could not remove '" + absolutePath + "'";
                    return false;
                }
                outReply = "removed " + absolutePath;
                return true;
            }

            /**
             * @brief 임포트 대상 폴더(리소스 id)를 절대 경로로 바꿉니다. 임시 리소스 폴더(`kImportTempFolder`) 아래일 때만 true 입니다.
             * @details 임포트한 에셋은 리소스 id 가 있어야 씬이 쓸 수 있는데, `Saved/Automation` 에는 id 가 없다. 그래서 리소스 트리 안의 한 폴더만 받는다.
             *          그 폴더는 `.gitignore` 가 무시하고 시나리오가 처음과 끝에 지운다(`asset.removeImportFolder`). 이 검사가 저장소 리소스를 지킨다.
             */
            [[nodiscard]] static expected<string> resolveImportFolder( const string& folderID )
            {
                const string  normalized       = FileUtil::normalizePath( folderID );
                const string& resourceRoot     = editor::getSubsystem<AssetPaths>().getRootFolderPath();
                const bool    bOutsideTempArea = FileUtil::startsWithPathComponent( normalized, kImportTempFolder ) == false || normalized.find( ".." ) != string::npos; // 리소스 id 끼리(소문자)
                if ( bOutsideTempArea || resourceRoot.empty() )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "imports go under the temporary resource folder", kImportTempFolder }.appendDetail( "/: " ).appendDetail( folderID ) );
                return FileUtil::joinPath( resourceRoot, folderID );
            }

            /**
             * @brief 원본 파일 하나를 임시 리소스 폴더로 가져옵니다. 콘텐츠 브라우저 Import 와 같은 경로(`EditorAssetFileCommands::importFiles`)이고
             *        OS 파일 대화상자만 건너뜁니다.
             * @details 원본은 절대 경로나 리소스 id 다(`readSourceFileArgument`). 같은 이름이 있으면 새 이름을 받으므로 시나리오는 먼저 폴더를 지운다.
             */
            static bool runImportAsset( const vector<string>& listArgument, string& outReply )
            {
                string sourcePath;
                if ( listArgument.size() != 2 || EditorAssetFileCommands::readSourceFileArgument( listArgument[0], sourcePath, outReply ) == false )
                    return false; // 원본이 없으면 readSourceFileArgument 가 이유를 적었다
                const expected<string> folderPath = resolveImportFolder( listArgument[1] );
                if ( folderPath.has_value() == false )
                {
                    outReply = AutomationStep::describeReason( folderPath.error() );
                    return false;
                }
                if ( EditorAssetFileCommands::importFiles( *folderPath, { sourcePath } ) != 1 )
                {
                    outReply = "could not import '" + sourcePath + "' into '" + listArgument[1] + "'";
                    return false;
                }
                outReply = "imported " + FileUtil::getFileNamePart( sourcePath ) + " into " + listArgument[1];
                return true;
            }

            /** @brief 시나리오의 처음과 끝에 임시 리소스 폴더 하나를 통째로 지웁니다. 폴더가 없으면 할 일이 없으므로 성공입니다. */
            static bool runRemoveImportFolder( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 )
                    return false;
                const expected<string> folderPath = resolveImportFolder( listArgument[0] );
                if ( folderPath.has_value() == false )
                {
                    outReply = AutomationStep::describeReason( folderPath.error() );
                    return false;
                }
                if ( FileUtil::removeDirectory( *folderPath ) == false )
                {
                    outReply = "could not remove '" + *folderPath + "'";
                    return false;
                }
                outReply = "removed " + listArgument[0];
                return true;
            }

            /** @brief 마지막 `asset.open` 의 파일이 디스크에 있으면 1 입니다. 연 적이 없으면 0 입니다. */
            [[nodiscard]] static bool readTempAssetExists( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const string& path = getEditorState<TempAssetState>()._lastPath;
                outValue           = ( path.empty() == false && FileUtil::exists( path ) ) ? 1.0 : 0.0;
                return true;
            }

            /**
             * @brief 고른 오브젝트(주 선택)를 임시 프리팹 파일로 저장하고 그 오브젝트를 프리팹 인스턴스로 연결합니다(유니티의 "오브젝트를 Project 로 끌어 프리팹 만들기").
             * @details 경로는 `asset.open` 과 같은 임시 에셋 규칙(작업 폴더의 `Saved/Automation/` 아래, 리소스 트리 밖)이고 확장자(`.prefab.xml` · `.prefab.json`)가
             *          형식을 정한다. 시나리오가 저장소 리소스를 바꾸지 않고 프리팹 만들기, 고치기, 적용을 거치는 경로다. 시나리오를 반복하므로 이미 있는 파일은 덮어쓴다.
             */
            static bool runCreatePrefab( const vector<string>& listArgument, string& outReply )
            {
                EditorContext* pContext = editor::findSubsystem<EditorContext>();
                string         absolutePath;
                const bool     bInvalidArguments = listArgument.size() != 1 || pContext == nullptr || resolveTempAssetPath( listArgument[0], absolutePath, outReply ) == false;
                if ( bInvalidArguments )
                    return false;
                // 놓인 프리팹 테이블(워크스페이스)이 메인 씬의 것이므로 메인 선택을 쓴다.
                GameObject* pObject = editor::getMainSceneSelection().getPrimaryObject();
                if ( pObject == nullptr )
                {
                    outReply = "select the object to turn into a prefab first";
                    return false;
                }
                (void)FileUtil::ensureDirectoryExists( FileUtil::getDirectoryPart( absolutePath ) );                                 // 못 만들면 ensure 가 폴더와 이유를 남기고 뒤따르는 쓰기가 실패로 답한다
                const EditorInspectorCommands::AdoptResult adopted = EditorInspectorCommands::attachChildPrefabInstances( pObject ); // 옮긴 수는 로그가 알린다
                if ( adopted.isRejected() )
                {
                    outReply = adopted._rejectReason;
                    return false;
                }
                PrefabAsset asset;
                asset.setFromGameObject( pObject );
                const optional<AbsolutePath> assetFile = FileUtil::requireAbsolutePath( absolutePath );
                if ( assetFile.has_value() == false || asset.saveToFile( *assetFile ) == false )
                {
                    outReply = "could not write the prefab '" + absolutePath + "'";
                    return false;
                }
                // 캐시 · 놓인 프리팹 표는 리소스 id 로 찾는다 — 작업 폴더의 `Saved/` 는 시나리오가 붙인 디스크 도메인이다.
                const string prefabID = editor::getSubsystem<AssetPaths>().toAssetID( absolutePath );
                if ( prefabID.empty() )
                {
                    outReply = "'" + absolutePath + "' is not under a mounted disk domain - run it from a scenario";
                    return false;
                }
                // 같은 경로를 다시 만들면 캐시의 옛 내용으로 스폰하지 않게 다음 로드가 파일을 다시 읽게 한다.
                editor::getSubsystem<AssetManager>().getAssetCache<PrefabCache>().reload( AssetID{ prefabID } );
                pContext->getSubsystem<EditorWorkspace>().setPlacedPrefabPath( *pObject, prefabID );
                pContext->getSubsystem<EditorWorkspace>().markSceneDirty();
                outReply = "created prefab " + absolutePath + " from " + pObject->getName().c_str();
                return true;
            }

            /**
             * @brief 프리팹 인스턴스 하나를 활성 씬 루트에 스폰하고 고릅니다. 콘텐츠 브라우저에서 끌어 놓기와 같은 경로이고 되돌리기를 하나 남깁니다.
             * @details `Saved/` 로 시작하는 경로는 `prefab.create` 가 만든 임시 프리팹(작업 폴더 기준)이라 시나리오가 붙인 `saved` 도메인의 id 로 바꿔 스폰하고, 나머지는 리소스 id 다.
             */
            static bool runSpawnPrefab( const vector<string>& listArgument, string& outReply )
            {
                GameObjectManager* pManager = editor::getActiveObjectManager();
                if ( listArgument.size() != 1 || pManager == nullptr )
                    return false;
                string path = listArgument[0];
                // 인자는 리소스 경로이거나 절대 디스크 경로다 — 글자(`Saved/`)로 추측하지 않는다. 디스크 경로는 이 경계에서 한 번 리소스 경로로 바꾼다.
                if ( FileUtil::isAbsolutePath( path ) )
                {
                    string absolutePath;
                    if ( resolveTempAssetPath( listArgument[0], absolutePath, outReply ) == false )
                        return false;
                    path = editor::getSubsystem<AssetPaths>().toAssetID( absolutePath ); // 시나리오가 붙인 `saved` 도메인의 id
                    if ( path.empty() )
                    {
                        outReply = "'" + absolutePath + "' is not under a mounted disk domain - run it from a scenario";
                        return false;
                    }
                }
                const GameObject* pSpawned = EditorAssetCommands::spawnPrefab( pManager, path.c_str(), nullptr );
                if ( pSpawned == nullptr )
                {
                    outReply = "could not spawn '" + path + "'";
                    return false;
                }
                outReply = "spawned " + string( pSpawned->getName().c_str() );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( OpenTempAsset, "asset.open", "asset.open <path>",
                    "Open a new document at a temp path under Saved/Automation/ in its editor panel (an old file there is removed first)",
                    &EditorAutomationAssetCommandsInternal::runOpenTempAsset );
    SW_DEV_COMMAND( OpenScratchAsset, "asset.openScratch", "asset.openScratch <scratch/...resource id>",
                    "Open an asset of a scenario's <Scratch> copy in its editor panel by resource id (Quick Open matches by file name and can pick the original)",
                    &EditorAutomationAssetCommandsInternal::runOpenScratchAsset );
    SW_DEV_COMMAND( OverwriteScratchAsset, "asset.overwriteScratch", "asset.overwriteScratch <scratch/...source id> <scratch/...target id>",
                    "Overwrite a scratch asset with another scratch file's text the way an outside editor saves (atomic write, the file watch reports it)",
                    &EditorAutomationAssetCommandsInternal::runOverwriteScratchAsset );
    SW_DEV_COMMAND( SnapshotScratchAsset, "asset.snapshotScratch", "asset.snapshotScratch <scratch/...resource id>",
                    "Remember the current text of a scratch asset (save round trips compare against it with asset.expectScratchUnchanged)",
                    &EditorAutomationAssetCommandsInternal::runSnapshotScratchAsset );
    SW_DEV_COMMAND( ExpectScratchUnchanged, "asset.expectScratchUnchanged", "asset.expectScratchUnchanged <scratch/...resource id>",
                    "Fail unless a scratch asset still has the text asset.snapshotScratch remembered (save, reload and save again must give the same bytes)",
                    &EditorAutomationAssetCommandsInternal::runExpectScratchUnchanged );
    SW_DEV_COMMAND( MakeTempFolder, "asset.makeTempFolder", "asset.makeTempFolder <folder>", "Create a temp folder under Saved/Automation/ (editor saves do not create missing folders)",
                    &EditorAutomationAssetCommandsInternal::runMakeTempFolder );
    SW_DEV_COMMAND( RemoveTempAsset, "asset.removeTemp", "asset.removeTemp <path>", "Remove a temp asset file under Saved/Automation/",
                    &EditorAutomationAssetCommandsInternal::runRemoveTempAsset );
    SW_DEV_COMMAND( ImportAsset, "asset.import", "asset.import <source file> <scratch/import/...folder>",
                    "Import a file into a temp resource folder the way Content Browser Import does, without the file dialog",
                    &EditorAutomationAssetCommandsInternal::runImportAsset );
    SW_DEV_COMMAND( RemoveImportFolder, "asset.removeImportFolder", "asset.removeImportFolder <scratch/import/...folder>",
                    "Remove a temp resource folder that asset.import filled", &EditorAutomationAssetCommandsInternal::runRemoveImportFolder );
    SW_AUTOMATION_WATCH( editorTempAssetExists, "Editor.TempAssetExists", "1 when the file of the last asset.open exists on disk",
                         &EditorAutomationAssetCommandsInternal::readTempAssetExists );
    SW_DEV_COMMAND( CreatePrefab, "prefab.create", "prefab.create <Saved/Automation/...prefab.xml>",
                    "Save the selected object as a temp prefab under Saved/Automation/ and link the object to it as an instance", &EditorAutomationAssetCommandsInternal::runCreatePrefab );
    SW_DEV_COMMAND( SpawnPrefab, "prefab.spawn", "prefab.spawn <path.prefab.xml>", "Spawn a prefab instance at the scene root and select it (one undo step)",
                    &EditorAutomationAssetCommandsInternal::runSpawnPrefab );
} // namespace sw::editor
