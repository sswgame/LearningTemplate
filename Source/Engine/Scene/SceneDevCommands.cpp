/**
 * @file SceneDevCommands.cpp
 * @brief 활성 씬을 다루는 개발 명령(순간이동 · 태그 · 스트리밍 셀 · 누수 곡선)입니다. Shipping 에는 없습니다.
 * @details 명령은 소유 코드 옆에 `SW_DEV_COMMAND` 로 둡니다(언리얼 `FAutoConsoleCommand`). 애니메이션 되감기 명령은 `Animation/Scene/AnimationDevCommands.cpp` 에 있다.
 */
#include "pch.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/Container/StringUtil.h"
#include "Core/String/TagID.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneCooker.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Scene/SceneStreamingSubsystem.h"

namespace sw
{
    namespace
    {
        struct SceneDevCommandsInternal
        {
            static bool runTeleport( const vector<string>& listArgument, string& outReply )
            {
                float3     position{};
                const bool bInvalidMoveArguments = listArgument.size() != 4 || StringUtil::parseFloat( listArgument[1], position._x ) == false ||
                                                   StringUtil::parseFloat( listArgument[2], position._y ) == false || StringUtil::parseFloat( listArgument[3], position._z ) == false;
                if ( bInvalidMoveArguments )
                    return false;
                GameObject*     pObject   = engine::findActiveObject( hashed_string( listArgument[0] ) );
                SceneComponent* pRootNode = pObject != nullptr ? pObject->getComponent<SceneComponent>() : nullptr;
                if ( pRootNode == nullptr )
                {
                    outReply = "no object named '" + listArgument[0] + "' with a transform in the active scene";
                    return false;
                }
                pRootNode->teleportTo( position );
                outReply = "teleported " + listArgument[0];
                return true;
            }

            static bool runTagAdd( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 2 || listArgument[1].empty() )
                    return false;
                GameObject* pObject = engine::findActiveObject( hashed_string( listArgument[0] ) );
                if ( pObject == nullptr )
                {
                    outReply = "no object named '" + listArgument[0] + "' in the active scene";
                    return false;
                }
                pObject->addTag( TagID::request( listArgument[1] ) );
                outReply = "tagged " + listArgument[0] + " with " + listArgument[1];
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
                    outReply = "streaming: " + to_string( streaming.getLoadedCellCount() ) + " of " + to_string( streaming.getCellCount() ) + " cells loaded";
                    return true;
                }
                if ( pScene->getSceneSettings()._bStreaming == false )
                {
                    outReply = "the active scene does not stream";
                    return true;
                }
                StreamingCellPreview preview{};
                if ( SceneCooker::previewStreamingCells( *pScene, preview ) == false )
                {
                    outReply = "the active scene could not be split into cells - see the log";
                    return false;
                }
                if ( preview._listViolation.empty() == false )
                {
                    outReply = "the cook would fail - " + to_string( static_cast<uint32>( preview._listViolation.size() ) ) +
                               " cross-cell references (see the log or the Streaming Cells window)";
                    return false;
                }
                outReply = to_string( static_cast<uint32>( preview._listCell.size() ) ) + " cells, " +
                           to_string( static_cast<uint32>( preview._listAlwaysLoadedID.size() ) ) + " entities always loaded";
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
                if ( pManager->startLoadLeakProbe( listArgument[0], static_cast<uint32>( measuredRounds ) ) == false )
                {
                    outReply = "could not start (see the log)";
                    return false;
                }
                outReply = "scene leak probe started - the verdict goes to the log when the rounds finish";
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( SceneLeakCurve, "scene.leakcurve", "scene.leakcurve <scene> [measuredRounds]",
                    "Load a scene again and again (1 warmup + N measured, default 8) and judge whether leftover memory grows (leak detection L3)",
                    &SceneDevCommandsInternal::runSceneLeakCurve );
    SW_DEV_COMMAND( Teleport, "teleport", "teleport <object> <x> <y> <z>", "Teleport a named object of the active scene", &SceneDevCommandsInternal::runTeleport );
    SW_DEV_COMMAND( StreamingCells, "world.cells", "world.cells", "Show loaded streaming cells, or split the active scene into cells as the cooker would",
                    &SceneDevCommandsInternal::runStreamingCells );
    SW_DEV_COMMAND( TagAdd, "tag.add", "tag.add <object> <tag>", "Add a gameplay tag to a named object of the active scene", &SceneDevCommandsInternal::runTagAdd );
} // namespace sw
