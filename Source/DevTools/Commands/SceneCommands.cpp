#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Name/TagID.h"
#include "Core/Name/hashed_string.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Asset/AssetStreamingQueue.h"
#include "Engine/Config/GameTimeScale.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneCellSplitter.h"
#include "Engine/Scene/SceneDataLayerSubsystem.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Scene/SceneStreamingSubsystem.h"
#include "Engine/Scene/SceneSubLevelSubsystem.h"

namespace sw
{
    namespace
    {
        /** @brief 활성 씬의 개발 명령(순간이동 · 데이터 레이어 · 하위 레벨 · 원점 · 스트리밍 · 누수 곡선 · 시간 배율)입니다. */
        struct SceneCommandsInternal
        {
            /** @brief `<object> <x> <y> <z>` 인자를 활성 씬 오브젝트의 루트 트랜스폼과 위치로 풉니다. 못 풀면 nullptr 이고 @p outReply 에 까닭을 적습니다. */
            static SceneComponent* resolveMoveTarget( const vector<string>& listArgument, float3& outPosition, string& outReply )
            {
                const bool bInvalidMoveArguments = listArgument.size() != 4 || StringUtil::parseFloat( listArgument[1], outPosition._x ) == false ||
                                                   StringUtil::parseFloat( listArgument[2], outPosition._y ) == false || StringUtil::parseFloat( listArgument[3], outPosition._z ) == false;
                if ( bInvalidMoveArguments )
                    return nullptr;
                GameObject*     pObject   = engine::findActiveObject( hashed_string( listArgument[0] ) );
                SceneComponent* pRootNode = pObject != nullptr ? pObject->getComponent<SceneComponent>() : nullptr;
                if ( pRootNode == nullptr )
                {
                    StringBuilder<constant::buffer::kMax256> reply;
                    reply.appendFormat( "no object named '%#' with a transform in the active scene", listArgument[0] );
                    outReply = string( reply.view() );
                }
                return pRootNode;
            }

            static bool runTeleport( const vector<string>& listArgument, string& outReply )
            {
                float3          position{};
                SceneComponent* pRootNode = resolveMoveTarget( listArgument, position, outReply );
                if ( pRootNode == nullptr )
                    return false;
                pRootNode->teleportTo( position );
                StringBuilder<constant::buffer::kMax256> reply;
                reply.appendFormat( "teleported %#", listArgument[0] );
                outReply = string( reply.view() );
                return true;
            }

            /** @brief `move <object> <x> <y> <z>` — 순간이동이 아니라 이어진 움직임으로 옮깁니다. 화면 속도 · 물리의 이동 경로 기록이 남는다(시나리오가 이어진 카메라 움직임을 만든다). */
            static bool runMove( const vector<string>& listArgument, string& outReply )
            {
                float3          position{};
                SceneComponent* pRootNode = resolveMoveTarget( listArgument, position, outReply );
                if ( pRootNode == nullptr )
                    return false;
                pRootNode->setWorldPosition( position );
                StringBuilder<constant::buffer::kMax256> reply;
                reply.appendFormat( "moved %#", listArgument[0] );
                outReply = string( reply.view() );
                return true;
            }

            /** @brief `scene.layer <name> <on|off>` — 활성 씬의 데이터 레이어를 켜고 끕니다. 인자가 없으면 켜진 레이어를 알립니다. */
            static bool runDataLayer( const vector<string>& listArgument, string& outReply )
            {
                SceneManager* pManager = engine::findSubsystem<SceneManager>();
                Scene*        pScene   = pManager != nullptr ? pManager->getActiveScene() : nullptr;
                if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
                {
                    outReply = "no active scene";
                    return false;
                }
                SceneDataLayerSubsystem& layers = pScene->getObjectManager()->getSubsystem<SceneDataLayerSubsystem>();
                if ( listArgument.empty() )
                {
                    vector<string> listActive;
                    layers.collectActiveLayers( listActive );
                    StringBuilder<constant::buffer::kMax512> reply;
                    reply.append( "active data layers:" );
                    for ( const string& layer : listActive )
                    {
                        reply.appendFormat( " %#", layer );
                    }
                    outReply = string( reply.view() );
                    return true;
                }
                const bool bOn = listArgument.size() == 2 && listArgument[1] == "on";
                if ( listArgument.size() != 2 || ( bOn == false && listArgument[1] != "off" ) )
                    return false;
                StringBuilder<constant::buffer::kMax256> reply;
                if ( layers.setLayerActive( listArgument[0], bOn ) == false )
                {
                    reply.appendFormat( "the active scene does not declare data layer '%#'", listArgument[0] );
                    outReply = string( reply.view() );
                    return false;
                }
                reply.appendFormat( "data layer %# %#", listArgument[0], listArgument[1] );
                outReply = string( reply.view() );
                return true;
            }

            /**
             * @brief `scene.sublevel add <name> <scene> <x> <y> <z> [yaw]` · `scene.sublevel remove <name>` — 활성 씬에 하위 레벨을 더하고 뺍니다(다음 프레임에 짓는다). 인자가 없으면 하위 레벨과 단계를 알립니다.
             */
            static bool runSubLevel( const vector<string>& listArgument, string& outReply )
            {
                SceneManager* pManager = engine::findSubsystem<SceneManager>();
                Scene*        pScene   = pManager != nullptr ? pManager->getActiveScene() : nullptr;
                if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
                {
                    outReply = "no active scene";
                    return false;
                }
                SceneSubLevelSubsystem& subLevels = pScene->getObjectManager()->getSubsystem<SceneSubLevelSubsystem>();
                if ( listArgument.empty() )
                {
                    static constexpr const utf8* kArrStateName[] = { "queued", "building", "loaded", "failed", "removing" };
                    vector<string>               listName;
                    subLevels.collectSubLevelNames( listName );
                    StringBuilder<constant::buffer::kMax512> reply;
                    reply.append( "sub-levels:" );
                    for ( const string& name : listName )
                    {
                        SubLevelState state{ SubLevelState::Queued };
                        (void)subLevels.findSubLevelState( name, state ); // 방금 모은 이름이다
                        reply.appendFormat( " %#(%#)", name, kArrStateName[static_cast<uint32>( state )] );
                    }
                    outReply = string( reply.view() );
                    return true;
                }
                StringBuilder<constant::buffer::kMax256> reply;
                if ( listArgument[0] == "remove" && listArgument.size() == 2 )
                {
                    if ( subLevels.removeSubLevel( listArgument[1] ) == false )
                    {
                        reply.appendFormat( "no sub-level named '%#'", listArgument[1] );
                        outReply = string( reply.view() );
                        return false;
                    }
                    reply.appendFormat( "removing sub-level %#", listArgument[1] );
                    outReply = string( reply.view() );
                    return true;
                }
                SceneSubLevelSubsystem::Placement placement{};
                float32                           yawDegrees{ 0.0f };
                const bool                        bParsed = listArgument[0] == "add" && ( listArgument.size() == 6 || listArgument.size() == 7 ) && StringUtil::parseFloat( listArgument[3], placement._position._x ) &&
                                     StringUtil::parseFloat( listArgument[4], placement._position._y ) && StringUtil::parseFloat( listArgument[5], placement._position._z ) &&
                                     ( listArgument.size() == 6 || StringUtil::parseFloat( listArgument[6], yawDegrees ) );
                if ( bParsed == false )
                    return false;
                placement._rotation._y = yawDegrees * MathUtil::kDegreeToRadian;
                if ( subLevels.addSubLevel( *pScene, listArgument[1], listArgument[2], placement ) == false )
                {
                    reply.appendFormat( "sub-level '%#' was not added (see the log)", listArgument[1] );
                    outReply = string( reply.view() );
                    return false;
                }
                reply.appendFormat( "adding sub-level %#", listArgument[1] );
                outReply = string( reply.view() );
                return true;
            }

            /** @brief `world.origin <x> <z>` — 활성 씬의 월드 원점을 절대 자리 (x, z) 로 옮깁니다(자동 옮기기와 같은 길). 인자가 없으면 지금 원점을 알립니다. */
            static bool runWorldOrigin( const vector<string>& listArgument, string& outReply )
            {
                SceneManager* pManager = engine::findSubsystem<SceneManager>();
                Scene*        pScene   = pManager != nullptr ? pManager->getActiveScene() : nullptr;
                if ( pScene == nullptr )
                {
                    outReply = "no active scene";
                    return false;
                }
                float32 x{ 0.0f };
                float32 z{ 0.0f };
                if ( listArgument.empty() == false )
                {
                    if ( listArgument.size() != 2 || StringUtil::parseFloat( listArgument[0], x ) == false || StringUtil::parseFloat( listArgument[1], z ) == false )
                        return false;
                    if ( pScene->shiftWorldOrigin( double3{ static_cast<float64>( x ), 0.0, static_cast<float64>( z ) } ) == false )
                    {
                        outReply = "the world origin cannot move now (see the log)";
                        return false;
                    }
                }
                const double3& origin = pScene->getWorldOrigin();
                outReply              = "world origin ";
                StringUtil::appendNumber( outReply, origin._x );
                outReply += ' ';
                StringUtil::appendNumber( outReply, origin._z );
                return true;
            }

            static bool runTagAdd( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 2 || listArgument[1].empty() )
                    return false;
                GameObject*                              pObject = engine::findActiveObject( hashed_string( listArgument[0] ) );
                StringBuilder<constant::buffer::kMax256> reply;
                if ( pObject == nullptr )
                {
                    reply.appendFormat( "no object named '%#' in the active scene", listArgument[0] );
                    outReply = string( reply.view() );
                    return false;
                }
                pObject->addTag( TagID::request( listArgument[1] ) );
                reply.appendFormat( "tagged %# with %#", listArgument[0], listArgument[1] );
                outReply = string( reply.view() );
                return true;
            }

            /**
             * @brief `world.cells` — 스트리밍 중이면 올라 있는 셀 수를, 아니면 활성 씬을 쿠커처럼 셀로 나눠 셀 수 · 늘 올라 있는 엔티티 수를 알립니다.
             * @details 나누지 못하면(셀을 넘는 참조) 쿠커와 같은 오류가 엔티티마다 로그에 남는다 — 쿠킹 전에 에디터에서 위반을 본다.
             */
            static bool runStreamingCells( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                SceneManager* pManager = engine::findSubsystem<SceneManager>();
                Scene*        pScene   = pManager != nullptr ? pManager->getActiveScene() : nullptr;
                if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
                {
                    outReply = "no active scene";
                    return false;
                }
                const SceneStreamingSubsystem& streaming = pScene->getObjectManager()->getSubsystem<SceneStreamingSubsystem>();
                if ( streaming.isStreaming() )
                {
                    StringBuilder<constant::buffer::kMax256> replyBuilder;
                    replyBuilder.appendFormat( "streaming: %# of %# cells loaded", streaming.getLoadedCellCount(), streaming.getCellCount() );
                    outReply = string( replyBuilder.view() );
                    return true;
                }
                if ( pScene->getSceneSettings()._bStreaming == false )
                {
                    outReply = "the active scene does not stream";
                    return true;
                }
                StreamingCellPreview preview{};
                if ( StreamingCellPreview::build( *pScene, preview ) == false )
                {
                    outReply = "the active scene could not be split into cells - see the log";
                    return false;
                }
                StringBuilder<constant::buffer::kMax256> textBuilder;
                if ( preview._listViolation.empty() == false )
                {
                    textBuilder.appendFormat( "the cook would fail - %# cross-cell references (see the log or the Streaming Cells window)", static_cast<uint32>( preview._listViolation.size() ) );
                    outReply = string( textBuilder.view() );
                    return false;
                }
                textBuilder.appendFormat( "%# cells, %# entities always loaded", static_cast<uint32>( preview._listCell.size() ), static_cast<uint32>( preview._listAlwaysLoadedID.size() ) );
                outReply = string( textBuilder.view() );
                return true;
            }

            /**
             * @brief `world.streamqueue` — 로드 큐를 보입니다: 올리려는 셀 조각(줄 차례 — 우선순위 · 거리 · 단계)과 HLOD 대리 수, 텍스처 밉 줄(우선순위 · 밉)과 읽는 중 · 업로드 중 수.
             * @details 셀과 텍스처가 같은 우선순위(`StreamingSourcePriority`)로 줄을 선다 — 둘을 한 화면에 놓아 차례가 어긋났는지 본다. 줄마다 앞 @p kMaxListedEntries 개만 적는다.
             */
            static bool runStreamingQueue( const vector<string>& listArgument, string& outReply )
            {
                constexpr uint32 kMaxListedEntries = 16;
                if ( listArgument.empty() == false )
                    return false;
                SceneManager* pManager = engine::findSubsystem<SceneManager>();
                Scene*        pScene   = pManager != nullptr ? pManager->getActiveScene() : nullptr;
                if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
                {
                    outReply = "no active scene";
                    return false;
                }
                const SceneStreamingSubsystem&                  streaming = pScene->getObjectManager()->getSubsystem<SceneStreamingSubsystem>();
                vector<SceneStreamingSubsystem::LoadQueueEntry> listCell;
                streaming.collectLoadQueue( listCell );
                StringBuilder<constant::buffer::kMax256> lineBuilder;
                lineBuilder.appendFormat( "cells: %# pending of %# (%# loaded), HLOD %# shown of %#", static_cast<uint32>( listCell.size() ), streaming.getCellCount(),
                                          streaming.getLoadedCellCount(), streaming.getShownHLODCount(), streaming.getHLODCount() );
                outReply = string( lineBuilder.view() );
                for ( size_t index = 0; index < listCell.size() && index < kMaxListedEntries; ++index )
                {
                    const SceneStreamingSubsystem::LoadQueueEntry& entry = listCell[index];
                    string                                         layers;
                    for ( const string& layer : entry._listDataLayer )
                    {
                        layers.append( layers.empty() ? " [" : "," ).append( layer );
                    }
                    layers.append( layers.empty() ? "" : "]" );
                    lineBuilder.clear();
                    lineBuilder.appendFormat( "\n  (%#, %#)%# %# %# m %#", entry._cell._x, entry._cell._z, layers, getEnumName( entry._priority ), entry._distance, getEnumName( entry._state ) );
                    outReply.append( lineBuilder.view() );
                }
                AssetManager* pAssets = pScene->getObjectManager()->findAssetManager();
                if ( pAssets == nullptr )
                    return true; // 에셋 서비스가 없다(도구) — 텍스처 줄이 없다
                const TextureStreamingQueueSnapshot textures = pAssets->getAssetCache<TextureCache>().captureStreamingQueue();
                lineBuilder.clear();
                lineBuilder.appendFormat( "\ntextures: %# waiting, %# reading, %# uploading", static_cast<uint32>( textures._listWaiting.size() ), textures._readingCount,
                                          textures._uploadingCount );
                outReply.append( lineBuilder.view() );
                for ( size_t index = 0; index < textures._listWaiting.size() && index < kMaxListedEntries; ++index )
                {
                    const TextureMipRequest& request = textures._listWaiting[index];
                    lineBuilder.clear();
                    lineBuilder.appendFormat( "\n  %# %# mips %#+%#", request._key, getEnumName( request._priority ), request._firstMip, request._mipCount );
                    outReply.append( lineBuilder.view() );
                }
                return true;
            }

            static bool runSceneLeakCurve( const vector<string>& listArgument, string& outReply )
            {
                constexpr int32 kDefaultMeasuredRounds = 8;
                int32           measuredRounds         = kDefaultMeasuredRounds;
                const bool      bInvalidArguments      = listArgument.empty() || listArgument.size() > 2 || ( listArgument.size() == 2 && StringUtil::parseInt( listArgument[1], measuredRounds ) == false ) || measuredRounds < 1;
                if ( bInvalidArguments )
                    return false;
                SceneManager* pManager = &engine::getSubsystem<SceneManager>();
                if ( pManager->startLoadLeakCheck( listArgument[0], static_cast<uint32>( measuredRounds ) ) == false )
                {
                    outReply = "could not start (see the log)";
                    return false;
                }
                outReply = "scene leak probe started - the verdict goes to the log when the rounds finish";
                return true;
            }

            /** @brief `timescale [scale]` — 게임 시간 배율을 보이거나 정합니다. */
            static bool runTimeScale( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() > 1 )
                    return false;
                if ( listArgument.size() == 1 )
                {
                    float32 scale = 1.0f;
                    if ( StringUtil::parseFloat( listArgument[0], scale ) == false )
                        return false;
                    engine::getSubsystem<GameTimeScale>().set( scale );
                }
                // 정한 배율을 답한다 — 정지 요청(정지 메뉴 · 로딩 화면)이 걸려 있으면 실효 배율은 0 이라 그것을 덧붙인다.
                const GameTimeScale&                     timeScale = engine::getSubsystem<GameTimeScale>();
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "time scale = %#%#", timeScale.getSetting(), timeScale.isPaused() ? " (paused)" : "" );
                outReply = string( replyBuilder.view() );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( SceneLeakCurve, "scene.leakcurve", "scene.leakcurve <scene> [measuredRounds]",
                    "Load a scene again and again (1 warmup + N measured, default 8) and judge whether leftover memory grows (leak detection L3)",
                    &SceneCommandsInternal::runSceneLeakCurve );
    SW_DEV_COMMAND( Teleport, "teleport", "teleport <object> <x> <y> <z>", "Teleport a named object of the active scene", &SceneCommandsInternal::runTeleport );
    SW_DEV_COMMAND( Move, "move", "move <object> <x> <y> <z>", "Move a named object of the active scene without teleporting (keeps motion history)", &SceneCommandsInternal::runMove );
    SW_DEV_COMMAND( StreamingCells, "world.cells", "world.cells", "Show loaded streaming cells, or split the active scene into cells as the cooker would",
                    &SceneCommandsInternal::runStreamingCells );
    SW_DEV_COMMAND( StreamingQueue, "world.streamqueue", "world.streamqueue",
                    "Show the streaming load queue: pending cells (priority, distance, stage), HLOD proxies shown, and texture mip requests (priority) with reads in flight",
                    &SceneCommandsInternal::runStreamingQueue );
    SW_DEV_COMMAND( DataLayer, "scene.layer", "scene.layer [<name> <on|off>]", "Turn a data layer of the active scene on or off, or list the active layers", &SceneCommandsInternal::runDataLayer );
    SW_DEV_COMMAND( SubLevel, "scene.sublevel", "scene.sublevel [add <name> <scene> <x> <y> <z> [yaw] | remove <name>]",
                    "Add a scene to the active scene as a runtime sub-level at a position, remove one, or list them with their state", &SceneCommandsInternal::runSubLevel );
    SW_DEV_COMMAND( WorldOrigin, "world.origin", "world.origin [<x> <z>]", "Move the world origin of the active scene to an absolute XZ position (all root objects shift), or print it",
                    &SceneCommandsInternal::runWorldOrigin );
    SW_DEV_COMMAND( TagAdd, "tag.add", "tag.add <object> <tag>", "Add a gameplay tag to a named object of the active scene", &SceneCommandsInternal::runTagAdd );
    SW_DEV_COMMAND( TimeScale, "timescale", "timescale [scale]", "Show or set the game time scale (gv_timeScale, 0 = frozen)",
                    &SceneCommandsInternal::runTimeScale );
} // namespace sw
