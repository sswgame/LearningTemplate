/**
 * @file EditorDevCommands.cpp
 * @brief 에디터가 제공하는 개발 명령입니다. 에디터 커맨드 실행, 플레이 세션, 씬 열기와 저장, 임시 에셋 열기, 선택, 디버그 그리기 데모가 있습니다.
 *        에디터 모듈과 함께 로드되고 언로드됩니다.
 * @details 임시 에셋 명령(`asset.open`, `asset.removeTemp`, 탐침 `Editor.TempAssetExists`)은 시나리오가 저장소 리소스를 바꾸지 않고
 *          문서 패널의 저장을 테스트하는 경로다. 경로는 작업 폴더의 `Saved/Automation/` 아래만 받고, 리소스 트리는 거절한다.
 */
#include "pch.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/String/TagID.h"

#include "Editor/Common/Commands/Asset/EditorAssetCommands.h"
#include "Editor/Common/Commands/Asset/EditorAssetFileCommands.h"
#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Commands/Scene/EditorInspectorCommands.h"
#include "Editor/Common/Commands/Scene/EditorSceneCommands.h"
#include "Editor/Common/Commands/Scene/EditorSceneFileCommands.h"
#include "Editor/Common/Workspace/EditorCamera.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Automation/AutomationScenario.h"
#include "Engine/Automation/AutomationScratch.h"
#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Probe/AutomationProbe.h"
#include "Engine/Profiling/DebugOverlayState.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Resource/ResourcePaths.h"

namespace sw::editor
{
    namespace
    {
        struct EditorDevCommandsInternal
        {
            /** @brief `asset.import` 가 받는 임시 리소스 폴더(리소스 id)입니다. `.gitignore` 의 `Resource/editor/automationtemp/` 와 같은 위치입니다. */
            static constexpr const utf8* kImportTempFolder = "editor/automationtemp";

            /** @brief 낱말을 양의 정수로 읽습니다. 숫자가 아니면 false 입니다. */
            [[nodiscard]] static bool parseCount( const string& text, uint32& outValue )
            {
                if ( text.empty() )
                    return false;
                uint64 value = 0;
                for ( const utf8 ch : text )
                {
                    const bool bInvalidCountDigit = ch < '0' || '9' < ch || value > 1000000;
                    if ( bInvalidCountDigit )
                        return false;
                    value = value * 10 + static_cast<uint64>( ch - '0' );
                }
                outValue = static_cast<uint32>( value );
                return true;
            }

            static bool runEditorCommand( const vector<string>& listArgument, string& outReply )
            {
                EditorContext* pContext = editor::findSubsystem<EditorContext>();
                if ( listArgument.size() != 1 || pContext == nullptr )
                    return false;
                if ( pContext->getSubsystem<EditorCommandRegistry>().execute( listArgument[0] ) == false )
                {
                    outReply = "no enabled editor command '" + listArgument[0] + "'";
                    return false;
                }
                outReply = "ran " + listArgument[0];
                return true;
            }

            static bool runPlay( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                EditorPlaySession::play();
                outReply = "play";
                return true;
            }

            static bool runSimulate( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                EditorPlaySession::simulate();
                outReply = "simulate";
                return true;
            }

            static bool runPause( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                EditorPlaySession::pause();
                outReply = "pause";
                return true;
            }

            static bool runStop( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                EditorPlaySession::stop();
                outReply = "stop";
                return true;
            }

            static bool runStep( const vector<string>& listArgument, string& outReply )
            {
                uint32     frameCount            = 1;
                const bool bInvalidStepArguments = listArgument.size() > 1 || ( listArgument.size() == 1 && parseCount( listArgument[0], frameCount ) == false ) || frameCount == 0;
                if ( bInvalidStepArguments )
                    return false;
                EditorPlaySession::stepFrames( frameCount );
                outReply = "step " + to_string( frameCount );
                return true;
            }

            /** @brief 명령 인자 경로를 절대 경로로 바꿉니다. 상대 경로면 시나리오 산출물과 같은 작업 폴더(`Saved/`)를 기준으로 합니다. */
            [[nodiscard]] static bool resolveScenePath( const string& path, string& outAbsolutePath )
            {
                if ( path.empty() )
                    return false;
                if ( FileUtil::isAbsolutePath( path ) )
                {
                    outAbsolutePath = path;
                    return true;
                }
                // 리소스 id(`engine/automation/...`)면 리소스 트리의 그 파일이다. 그래서 시나리오가 작업 폴더를 몰라도 된다.
                const string resourcePath = ResourcePaths::findLooseFile( path ).getDiskPath();
                if ( FileUtil::exists( resourcePath ) )
                {
                    outAbsolutePath = resourcePath;
                    return true;
                }
                return FileUtil::makeAbsolutePath( path, outAbsolutePath );
            }

            static bool runSaveSceneAs( const vector<string>& listArgument, string& outReply )
            {
                string absolutePath;
                if ( listArgument.size() != 1 || resolveScenePath( listArgument[0], absolutePath ) == false )
                    return false;
                (void)FileUtil::ensureDirectoryExists( FileUtil::getDirectoryPart( absolutePath ) );
                if ( EditorSceneFileCommands::saveActiveScene( absolutePath ) == false )
                {
                    outReply = "could not save the active scene to '" + absolutePath + "'";
                    return false;
                }
                outReply = "saved " + absolutePath;
                return true;
            }

            static bool runOpenScene( const vector<string>& listArgument, string& outReply )
            {
                string absolutePath;
                if ( listArgument.size() != 1 || resolveScenePath( listArgument[0], absolutePath ) == false )
                    return false;
                // 미저장 씬이 있어도 대화상자를 띄우는 경로(tryOpenScene)를 쓰지 않고 바로 연다. 무인 실행에서는 대화상자를 아무도 닫지 않는다.
                if ( EditorSceneFileCommands::loadScene( absolutePath ) == false )
                {
                    outReply = "could not open '" + absolutePath + "'";
                    return false;
                }
                outReply = "opening " + absolutePath;
                return true;
            }

            /** @brief 마지막으로 `asset.open` 한 임시 에셋의 상태입니다. 탐침 `Editor.TempAssetExists` 가 읽는다. 에디터 범위의 상태 저장소가 소유한다. */
            struct TempAssetState
            {
                string _lastPath{}; ///< 마지막으로 연 임시 에셋의 절대 경로. 연 적이 없으면 비어 있다
            };

            /** @brief @p path 가 @p folder 와 같거나 그 아래인지입니다. 둘 다 `normalizePath` 를 거친 뒤 대소문자 없이 비교합니다. */
            [[nodiscard]] static bool isUnderFolder( const string& path, const string& folder )
            {
                if ( folder.empty() || StringUtil::startsWith( path, folder, true ) == false )
                    return false;
                return path.size() == folder.size() || path[folder.size()] == '/';
            }

            /**
             * @brief 임시 에셋 인자를 절대 경로로 바꿉니다. 작업 폴더의 `Saved/Automation/` 아래이고 리소스 트리 밖일 때만 true 입니다.
             * @details 시나리오가 저장 단계를 밟아도 저장소 리소스가 바뀌지 않게 하는 문지기다. 상대 경로는 작업 폴더 기준이다(`scene.saveAs` 와 같다).
             */
            [[nodiscard]] static bool resolveTempAssetPath( const string& path, string& outAbsolutePath, string& outReply )
            {
                string     absolutePath;
                string     automationFolder;
                const bool bUnresolvedPaths = path.empty() || FileUtil::makeAbsolutePath( path, absolutePath ) == false ||
                                              FileUtil::makeAbsolutePath( kAutomationOutputFolder, automationFolder ) == false;
                if ( bUnresolvedPaths )
                {
                    outReply = "could not resolve '" + path + "'";
                    return false;
                }
                const string normalized = FileUtil::normalizePath( absolutePath );
                if ( isUnderFolder( normalized, FileUtil::normalizePath( automationFolder ) ) == false )
                {
                    outReply = "temp assets must be under Saved/Automation/: '" + absolutePath + "'";
                    return false;
                }
                const string& resourceRoot = ResourcePaths::getRootFolderPath();
                if ( resourceRoot.empty() == false && isUnderFolder( normalized, FileUtil::normalizePath( resourceRoot ) ) )
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
                (void)FileUtil::ensureDirectoryExists( FileUtil::getDirectoryPart( absolutePath ) );
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
                const bool   bBadID        = listArgument.size() != 1 || StringUtil::startsWith( listArgument[0], scratchPrefix ) == false || ResourcePaths::hasParentDirectoryComponent( listArgument[0] );
                if ( bBadID )
                {
                    outReply = "asset.openScratch takes one resource id under " + scratchPrefix;
                    return false;
                }
                const string absolutePath = ResourcePaths::findLooseFile( FileUtil::normalizePath( listArgument[0] ) ).getDiskPath();
                if ( absolutePath.empty() || FileUtil::exists( absolutePath ) == false )
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
            [[nodiscard]] static bool resolveImportFolder( const string& folderID, string& outAbsolutePath, string& outReply )
            {
                const string  normalized       = FileUtil::normalizePath( folderID );
                const string& resourceRoot     = ResourcePaths::getRootFolderPath();
                const bool    bOutsideTempArea = isUnderFolder( normalized, kImportTempFolder ) == false || normalized.find( ".." ) != string::npos;
                if ( bOutsideTempArea || resourceRoot.empty() )
                {
                    outReply = string( "imports go under the resource folder " ) + kImportTempFolder + "/: '" + folderID + "'";
                    return false;
                }
                outAbsolutePath = FileUtil::joinPath( resourceRoot, folderID );
                return true;
            }

            /**
             * @brief 원본 파일 하나를 임시 리소스 폴더로 가져옵니다. 콘텐츠 브라우저 Import 와 같은 경로(`EditorAssetFileCommands::importFiles`, `.meta` 생성)이고
             *        OS 파일 대화상자만 건너뜁니다.
             * @details 원본은 작업 폴더 기준 경로나 리소스 id 다. 같은 이름이 있으면 새 이름을 받으므로 시나리오는 먼저 폴더를 지운다.
             */
            static bool runImportAsset( const vector<string>& listArgument, string& outReply )
            {
                string     sourcePath;
                string     folderPath;
                const bool bInvalidArguments = listArgument.size() != 2 || resolveScenePath( listArgument[0], sourcePath ) == false ||
                                               resolveImportFolder( listArgument[1], folderPath, outReply ) == false;
                if ( bInvalidArguments )
                    return false;
                if ( FileUtil::exists( sourcePath ) == false )
                {
                    outReply = "no source file '" + sourcePath + "'";
                    return false;
                }
                if ( EditorAssetFileCommands::importFiles( folderPath, { sourcePath } ) != 1 )
                {
                    outReply = "could not import '" + sourcePath + "' into '" + listArgument[1] + "'";
                    return false;
                }
                outReply = "imported " + FileUtil::getFileNamePart( sourcePath ) + " into " + listArgument[1];
                return true;
            }

            /** @brief 시나리오의 처음과 끝에 임시 리소스 폴더 하나를 `.meta` 까지 통째로 지웁니다. 폴더가 없으면 할 일이 없으므로 성공입니다. */
            static bool runRemoveImportFolder( const vector<string>& listArgument, string& outReply )
            {
                string folderPath;
                if ( listArgument.size() != 1 || resolveImportFolder( listArgument[0], folderPath, outReply ) == false )
                    return false;
                if ( FileUtil::removeDirectory( folderPath ) == false )
                {
                    outReply = "could not remove '" + folderPath + "'";
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

            static bool runSelectType( const vector<string>& listArgument, string& outReply )
            {
                GameObjectManager* pManager               = editor::getActiveObjectManager();
                const bool         bInvalidSpawnArguments = listArgument.size() != 1 || pManager == nullptr;
                if ( bInvalidSpawnArguments )
                    return false;
                const TypeInfo* pType = editor::getSubsystem<TypeRegistry>().findType( hashed_string( listArgument[0] ) );
                if ( pType == nullptr )
                {
                    outReply = "unknown type '" + listArgument[0] + "'";
                    return false;
                }
                vector<GameObject*> listObject;
                EditorSceneCommands::collectObjectsWithComponent( *pManager, pType, listObject );
                outReply = "selected " + to_string( EditorSceneCommands::selectObjects( listObject ) );
                return true;
            }

            /** @brief 고른 오브젝트마다 컴포넌트 하나를 추가하고 되돌리기를 하나씩 남깁니다. 시나리오가 테스트 컴포넌트를 추가하므로 "Add Component" 메뉴에서 숨긴 타입(`HideInMenu`)도 받습니다. */
            static bool runAddComponent( const vector<string>& listArgument, string& outReply )
            {
                // 사람이 인스펙터의 "Add Component" 로 하는 일과 같다. 대상 창의 선택에 추가하고, 미리보기 창이면 기록도 그 창의 스택에 남긴다.
                const EditorSelection* pSelection = editor::getFocusedSelection();
                if ( listArgument.size() != 1 || pSelection == nullptr )
                    return false;
                vector<GameObject*> listSelected;
                pSelection->getSelectedObjects( listSelected );
                uint32 addedCount{ 0 };
                for ( GameObject* pObject : listSelected )
                {
                    if ( EditorSceneCommands::addComponent( pObject, hashed_string( listArgument[0] ) ) != nullptr )
                        ++addedCount;
                }
                outReply = "added " + listArgument[0] + " to " + to_string( addedCount ) + " object(s)";
                return addedCount > 0;
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
                (void)FileUtil::ensureDirectoryExists( FileUtil::getDirectoryPart( absolutePath ) );
                const EditorInspectorCommands::AdoptResult adopted = EditorInspectorCommands::adoptChildPrefabInstances( pObject ); // 옮긴 수는 로그가 알린다
                if ( adopted.isRejected() )
                {
                    outReply = adopted._rejectReason;
                    return false;
                }
                PrefabAsset asset;
                asset.setFromGameObject( pObject );
                if ( asset.saveToFile( absolutePath ) == false )
                {
                    outReply = "could not write the prefab '" + absolutePath + "'";
                    return false;
                }
                // 캐시 · 놓인 프리팹 표는 리소스 id 로 찾는다 — 작업 폴더의 `Saved/` 는 시나리오가 붙인 디스크 도메인이다.
                const string prefabID = ResourcePaths::toResourceID( absolutePath );
                if ( prefabID.empty() )
                {
                    outReply = "'" + absolutePath + "' is not under a mounted disk domain - run it from a scenario";
                    return false;
                }
                // 같은 경로를 다시 만들면 캐시의 옛 내용으로 스폰하지 않게 다음 로드가 파일을 다시 읽게 한다.
                editor::getSubsystem<AssetManager>().getAssetCache<PrefabCache>().reload( prefabID, nullptr );
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
                if ( StringUtil::startsWith( path, "Saved/", true ) )
                {
                    string absolutePath;
                    if ( resolveTempAssetPath( listArgument[0], absolutePath, outReply ) == false )
                        return false;
                    path = ResourcePaths::toResourceID( absolutePath ); // 시나리오가 붙인 `saved` 도메인의 id
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

            static bool runSelectTag( const vector<string>& listArgument, string& outReply )
            {
                GameObjectManager* pManager = editor::getActiveObjectManager();
                if ( listArgument.size() != 1 || pManager == nullptr )
                    return false;
                vector<GameObject*> listObject;
                EditorSceneCommands::collectObjectsWithTag( *pManager, TagID::request( listArgument[0] ), listObject );
                outReply = "selected " + to_string( EditorSceneCommands::selectObjects( listObject ) );
                return true;
            }

            /**
             * @brief 씬 뷰 카메라 앞 6 m 에 디버그 도형 네 개(상자, 화살표, 구, 글자)와 오버레이 값 하나를 둡니다. 씬 뷰 시각화와 게임 뷰 HUD 가 동작하는지 눈으로 확인할 때 씁니다.
             */
            static bool runDebugDrawDemo( const vector<string>& listArgument, string& outReply )
            {
                uint32 seconds = 10;
                if ( listArgument.size() > 1 || ( listArgument.size() == 1 && parseCount( listArgument[0], seconds ) == false ) )
                    return false;
                const CameraComponent* pCamera = EditorCamera::ensure( editor::getActiveScene() );
                if ( pCamera == nullptr )
                    return false;
                DebugDrawQueue* pQueue   = &editor::getSubsystem<DebugDrawQueue>();
                const float4x4  world    = pCamera->getWorldMatrix();
                const float3    forward  = float3::transformVector( float3{ 0.0f, 0.0f, 1.0f }, world ).normalize();
                const float3    right    = float3::transformVector( float3{ 1.0f, 0.0f, 0.0f }, world ).normalize();
                const float3    center   = pCamera->getWorldPosition() + forward * 6.0f;
                const float32   duration = static_cast<float32>( seconds );
                pQueue->drawBox( center - right * 1.5f, float3{ 0.5f, 0.5f, 0.5f }, float4{ 1.0f, 0.4f, 0.1f, 1.0f }, duration, "Demo" );
                pQueue->drawSphere( center + right * 1.5f, 0.6f, float4{ 0.2f, 0.8f, 1.0f, 1.0f }, duration, "Demo" );
                pQueue->drawArrow( center - right * 0.6f, center + right * 0.6f, float4{ 0.3f, 1.0f, 0.3f, 1.0f }, duration, "Demo" );
                pQueue->drawText( center + float3{ 0.0f, 1.0f, 0.0f }, "DebugDrawQueue demo", float4{ 1.0f, 1.0f, 0.3f, 1.0f }, duration, "Demo" );
                editor::getSubsystem<DebugOverlayState>().setFloat( hashed_string( "debugdraw.demo.seconds" ), duration );
                outReply = "debug draw demo for " + to_string( seconds ) + " s";
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( EditorCommand, "editor", "editor <commandID>", "Run an editor command by id (the command palette ids, e.g. scene.saveScene)",
                    &EditorDevCommandsInternal::runEditorCommand );
    SW_DEV_COMMAND( Play, "play", "play", "Start (or switch to) play-in-editor", &EditorDevCommandsInternal::runPlay );
    SW_DEV_COMMAND( Simulate, "simulate", "simulate", "Start (or switch to) simulate - world only, no player or game input",
                    &EditorDevCommandsInternal::runSimulate );
    SW_DEV_COMMAND( Pause, "pause", "pause", "Pause the play session", &EditorDevCommandsInternal::runPause );
    SW_DEV_COMMAND( Stop, "stop", "stop", "Stop the play session and restore the edited scene", &EditorDevCommandsInternal::runStop );
    SW_DEV_COMMAND( SaveSceneAs, "scene.saveAs", "scene.saveAs <path>", "Save the active scene to a file (relative paths start at the working folder)",
                    &EditorDevCommandsInternal::runSaveSceneAs );
    SW_DEV_COMMAND( OpenScene, "scene.open", "scene.open <path>",
                    "Open a scene file without the unsaved-changes prompt (a resource id, or a path relative to the working folder)",
                    &EditorDevCommandsInternal::runOpenScene );
    SW_DEV_COMMAND( OpenTempAsset, "asset.open", "asset.open <path>",
                    "Open a new document at a temp path under Saved/Automation/ in its editor panel (an old file there is removed first)",
                    &EditorDevCommandsInternal::runOpenTempAsset );
    SW_DEV_COMMAND( OpenScratchAsset, "asset.openScratch", "asset.openScratch <scratch/...resource id>",
                    "Open an asset of a scenario's <Scratch> copy in its editor panel by resource id (Quick Open matches by file name and can pick the original)",
                    &EditorDevCommandsInternal::runOpenScratchAsset );
    SW_DEV_COMMAND( MakeTempFolder, "asset.makeTempFolder", "asset.makeTempFolder <folder>", "Create a temp folder under Saved/Automation/ (editor saves do not create missing folders)",
                    &EditorDevCommandsInternal::runMakeTempFolder );
    SW_DEV_COMMAND( RemoveTempAsset, "asset.removeTemp", "asset.removeTemp <path>", "Remove a temp asset file under Saved/Automation/",
                    &EditorDevCommandsInternal::runRemoveTempAsset );
    SW_DEV_COMMAND( ImportAsset, "asset.import", "asset.import <source file> <editor/automationtemp/...folder>",
                    "Import a file into a temp resource folder the way Content Browser Import does, without the file dialog",
                    &EditorDevCommandsInternal::runImportAsset );
    SW_DEV_COMMAND( RemoveImportFolder, "asset.removeImportFolder", "asset.removeImportFolder <editor/automationtemp/...folder>",
                    "Remove a temp resource folder that asset.import filled (with its .meta files)", &EditorDevCommandsInternal::runRemoveImportFolder );
    SW_AUTOMATION_PROBE( editorTempAssetExists, "Editor.TempAssetExists", "1 when the file of the last asset.open exists on disk",
                         &EditorDevCommandsInternal::readTempAssetExists );
    SW_DEV_COMMAND( Step, "step", "step [frames]", "Advance the play session by N frames, then pause", &EditorDevCommandsInternal::runStep );
    SW_DEV_COMMAND( SelectType, "select.type", "select.type <ComponentType>", "Select every object with that component type (or a derived one)",
                    &EditorDevCommandsInternal::runSelectType );
    SW_DEV_COMMAND( AddComponent, "component.add", "component.add <ComponentType>", "Add a component to every selected object (hidden test types too)",
                    &EditorDevCommandsInternal::runAddComponent );
    SW_DEV_COMMAND( CreatePrefab, "prefab.create", "prefab.create <Saved/Automation/...prefab.xml>",
                    "Save the selected object as a temp prefab under Saved/Automation/ and link the object to it as an instance", &EditorDevCommandsInternal::runCreatePrefab );
    SW_DEV_COMMAND( SpawnPrefab, "prefab.spawn", "prefab.spawn <path.prefab.xml>", "Spawn a prefab instance at the scene root and select it (one undo step)",
                    &EditorDevCommandsInternal::runSpawnPrefab );
    SW_DEV_COMMAND( SelectTag, "select.tag", "select.tag <Tag>", "Select every object with that tag (or a child tag)", &EditorDevCommandsInternal::runSelectTag );
    SW_DEV_COMMAND( DebugDrawDemo, "debugdraw.demo", "debugdraw.demo [seconds]", "Draw a box, sphere, arrow and text in front of the scene view camera",
                    &EditorDevCommandsInternal::runDebugDrawDemo );
} // namespace sw::editor
