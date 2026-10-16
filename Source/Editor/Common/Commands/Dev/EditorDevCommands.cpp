/**
 * @file EditorDevCommands.cpp
 * @brief 에디터가 내주는 개발 명령(에디터 커맨드 실행 · 플레이 세션 · 씬 열고 저장 · 임시 에셋 열기 · 선택 · 디버그 그리기 데모)입니다. 에디터 모듈과 함께 올라오고 내려갑니다.
 * @details 임시 에셋(`asset.open` · `asset.removeTemp` · 탐침 `Editor.TempAssetExists`)은 시나리오가 문서 패널의 저장을 저장소 리소스를
 *          바꾸지 않고 시험하는 길이다. 경로는 작업 폴더의 `Saved/Automation/` 아래만 받는다 — 리소스 트리는 거절한다.
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
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Automation/AutomationProbe.h"
#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Utility/DebugOverlayState.h"

namespace sw::editor
{
    namespace
    {
        struct EditorDevCommandsInternal
        {
            /** @brief `asset.import` 가 받는 임시 리소스 폴더(리소스 id)입니다. `.gitignore` 의 `Resource/editor/automationtemp/` 와 같은 자리입니다. */
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
                EditorContext* pContext = EditorContext::get();
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

            /** @brief 명령 인자 경로를 절대 경로로 바꿉니다(상대면 작업 폴더 기준 — 시나리오 산출물과 같은 `Saved/`). */
            [[nodiscard]] static bool resolveScenePath( const string& path, string& outAbsolutePath )
            {
                if ( path.empty() )
                    return false;
                if ( FileUtil::isAbsolutePath( path ) )
                {
                    outAbsolutePath = path;
                    return true;
                }
                // 리소스 id(`engine/automation/...`)면 리소스 트리의 그 파일이다 — 시나리오가 작업 폴더를 몰라도 된다.
                const string resourcePath = FileUtil::joinPath( ResourcePaths::getRootFolderPath(), path );
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
                // 미저장 씬이면 대화상자를 띄우는 길(tryOpenScene)이 아니라 바로 연다 — 무인 실행에서 대화상자는 아무도 닫지 않는다.
                if ( EditorSceneFileCommands::loadScene( absolutePath ) == false )
                {
                    outReply = "could not open '" + absolutePath + "'";
                    return false;
                }
                outReply = "opening " + absolutePath;
                return true;
            }

            /** @brief 마지막으로 `asset.open` 한 임시 에셋의 절대 경로입니다. 탐침 `Editor.TempAssetExists` 가 읽습니다. */
            static string& getLastTempAssetPath()
            {
                static string s_path;
                return s_path;
            }

            /** @brief @p path 가 @p folder 와 같거나 그 아래인지입니다. 둘 다 `normalizePath` 를 거친 뒤 대소문자 없이 견줍니다. */
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
                                              FileUtil::makeAbsolutePath( "Saved/Automation", automationFolder ) == false;
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

            /** @brief 임시 에셋 하나를 새 문서로 엽니다. 남은 옛 파일은 먼저 지워 매번 같은 상태(파일 없음 — 기본 문서)로 시작합니다. */
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
                getLastTempAssetPath() = absolutePath;
                outReply               = "opening " + absolutePath;
                return true;
            }

            /** @brief 시나리오 끝 정리 — 임시 에셋 파일을 지웁니다. 없으면 할 일이 없어 성공입니다. */
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
             * @details 임포트한 에셋은 리소스 id 가 있어야 씬이 쓴다 — `Saved/Automation` 에는 id 가 없다. 그래서 리소스 트리 안의 한 폴더만 받고,
             *          그 폴더는 `.gitignore` 가 무시하며 시나리오가 처음과 끝에 지운다(`asset.removeImportFolder`). 저장소 리소스를 바꾸지 않는 문지기다.
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
             * @brief 원본 파일 하나를 임시 리소스 폴더로 가져옵니다 — 콘텐츠 브라우저 Import 와 같은 길(`EditorAssetFileCommands::importFiles`, `.meta` 생성)이고
             *          OS 파일 대화상자만 건너뜁니다. 원본은 작업 폴더 기준 경로나 리소스 id 입니다. 같은 이름이 있으면 새 이름을 받으므로 시나리오는 먼저 폴더를 지운다.
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

            /** @brief 시나리오 처음 · 끝 정리 — 임시 리소스 폴더 하나를 통째로 지웁니다(`.meta` 포함). 없으면 할 일이 없어 성공입니다. */
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
                const string& path = getLastTempAssetPath();
                outValue           = ( path.empty() == false && FileUtil::exists( path ) ) ? 1.0 : 0.0;
                return true;
            }

            static bool runSelectType( const vector<string>& listArgument, string& outReply )
            {
                GameObjectManager* pManager               = editor::getActiveObjectManager();
                TypeRegistry*      pRegistry              = editor::findSubsystem<TypeRegistry>();
                const bool         bInvalidSpawnArguments = listArgument.size() != 1 || pManager == nullptr || pRegistry == nullptr;
                if ( bInvalidSpawnArguments )
                    return false;
                const TypeInfo* pType = pRegistry->findType( hashed_string( listArgument[0] ) );
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

            /** @brief 고른 오브젝트마다 컴포넌트 하나를 단다(되돌리기 한 줄씩). "Add Component" 메뉴에 숨긴 타입(`HideInMenu`)도 단다 — 시나리오가 테스트 컴포넌트를 단다. */
            static bool runAddComponent( const vector<string>& listArgument, string& outReply )
            {
                EditorContext* pContext = EditorContext::get();
                if ( listArgument.size() != 1 || pContext == nullptr )
                    return false;
                vector<GameObject*> listSelected;
                pContext->getSubsystem<EditorSelection>().getSelectedObjects( listSelected );
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
             * @brief 고른 오브젝트(주 선택)를 임시 프리팹 파일로 저장하고 그 오브젝트를 프리팹 인스턴스로 잇습니다(유니티의 "오브젝트를 Project 로 끌어 프리팹 만들기").
             * @details 경로는 `asset.open` 과 같은 임시 에셋 규칙(작업 폴더의 `Saved/Automation/` 아래, 리소스 트리 밖)이고 확장자(`.prefab.xml` · `.prefab.json`)가
             *          형식을 정한다. 시나리오가 저장소 리소스를 바꾸지 않고 프리팹 만들기 · 고치기 · 적용을 밟는 길이다. 이미 있는 파일은 덮어쓴다(시나리오 반복).
             */
            static bool runCreatePrefab( const vector<string>& listArgument, string& outReply )
            {
                EditorContext* pContext = EditorContext::get();
                string         absolutePath;
                const bool     bInvalidArguments = listArgument.size() != 1 || pContext == nullptr || resolveTempAssetPath( listArgument[0], absolutePath, outReply ) == false;
                if ( bInvalidArguments )
                    return false;
                GameObject* pObject = pContext->getSubsystem<EditorSelection>().getPrimaryObject();
                if ( pObject == nullptr )
                {
                    outReply = "select the object to turn into a prefab first";
                    return false;
                }
                (void)FileUtil::ensureDirectoryExists( FileUtil::getDirectoryPart( absolutePath ) );
                (void)EditorInspectorCommands::adoptChildPrefabInstances( pObject ); // 옮긴 수는 로그가 알린다
                PrefabAsset asset;
                asset.setFromGameObject( pObject );
                if ( asset.saveToFile( absolutePath ) == false )
                {
                    outReply = "could not write the prefab '" + absolutePath + "'";
                    return false;
                }
                // 같은 경로를 다시 만들면 캐시의 옛 내용으로 스폰하지 않게 다음 로드가 파일을 다시 읽게 한다.
                if ( AssetManager* pResources = editor::findSubsystem<AssetManager>() )
                    pResources->getAssetCache<PrefabCache>().reload( absolutePath, nullptr );
                pContext->getSubsystem<EditorWorkspace>().setPlacedPrefabPath( pObject->getObjectID(), absolutePath );
                pContext->getSubsystem<EditorWorkspace>().markSceneDirty();
                outReply = "created prefab " + absolutePath + " from " + pObject->getName().c_str();
                return true;
            }

            /**
             * @brief 프리팹 인스턴스 하나를 활성 씬 루트에 스폰하고 고릅니다(되돌리기 한 줄 — 콘텐츠 브라우저에서 끌어 놓기와 같은 길).
             * @details `Saved/` 로 시작하는 경로는 `prefab.create` 가 만든 임시 프리팹(작업 폴더 기준)이고, 나머지는 리소스 id 다.
             */
            static bool runSpawnPrefab( const vector<string>& listArgument, string& outReply )
            {
                GameObjectManager* pManager = editor::getActiveObjectManager();
                if ( listArgument.size() != 1 || pManager == nullptr )
                    return false;
                string path = listArgument[0];
                if ( StringUtil::startsWith( path, "Saved/", true ) && resolveTempAssetPath( listArgument[0], path, outReply ) == false )
                    return false;
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
             * @brief 씬 뷰 카메라 앞 6 m 에 디버그 도형 넷(상자 · 화살표 · 구 · 글자)과 오버레이 값 하나를 둡니다 — 씬 뷰 시각화 · 게임 뷰 HUD 가 도는지 눈으로 보는 용도.
             */
            static bool runDebugDrawDemo( const vector<string>& listArgument, string& outReply )
            {
                uint32 seconds = 10;
                if ( listArgument.size() > 1 || ( listArgument.size() == 1 && parseCount( listArgument[0], seconds ) == false ) )
                    return false;
                const CameraComponent* pCamera = EditorCamera::ensure( editor::getActiveScene() );
                DebugDrawQueue*        pQueue  = editor::findSubsystem<DebugDrawQueue>();
                if ( pCamera == nullptr || pQueue == nullptr )
                    return false;
                const float4x4 world    = pCamera->getWorldMatrix();
                const float3   forward  = float3::transformVector( float3{ 0.0f, 0.0f, 1.0f }, world ).normalize();
                const float3   right    = float3::transformVector( float3{ 1.0f, 0.0f, 0.0f }, world ).normalize();
                const float3   center   = pCamera->getWorldPosition() + forward * 6.0f;
                const float32  duration = static_cast<float32>( seconds );
                pQueue->drawBox( center - right * 1.5f, float3{ 0.5f, 0.5f, 0.5f }, float4{ 1.0f, 0.4f, 0.1f, 1.0f }, duration, "Demo" );
                pQueue->drawSphere( center + right * 1.5f, 0.6f, float4{ 0.2f, 0.8f, 1.0f, 1.0f }, duration, "Demo" );
                pQueue->drawArrow( center - right * 0.6f, center + right * 0.6f, float4{ 0.3f, 1.0f, 0.3f, 1.0f }, duration, "Demo" );
                pQueue->drawText( center + float3{ 0.0f, 1.0f, 0.0f }, "DebugDrawQueue demo", float4{ 1.0f, 1.0f, 0.3f, 1.0f }, duration, "Demo" );
                if ( DebugOverlayState* pOverlay = editor::findSubsystem<DebugOverlayState>() )
                    pOverlay->setFloat( hashed_string( "debugdraw.demo.seconds" ), duration );
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
