#include "pch.h"

#include "Core/Name/hashed_string.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Character/Hit/CharacterHit.h"
#include "Engine/Character/Hit/RagdollComponent.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Navigation/Component/NavMeshAgentComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Physics/Asset/PhysicsAsset.h"
#include "Engine/Scene/SceneManager.h"

namespace sw
{
    namespace
    {
        /** @brief 캐릭터 개발 명령(맞힘 · 내비 이동)입니다. */
        struct CharacterCommandsInternal
        {
            /** @brief 명령이 보내는 맞음의 충격량(N·s)입니다. */
            static constexpr float32 kHitImpulse = 40.0f;
            /** @brief `combat.fatalhit` 의 피해입니다. */
            static constexpr float32 kFatalHitDamage = 100.0f;

            /**
             * @brief 이름 붙은 오브젝트의 래그돌에서 뼈 @p boneName 의 바디를 맞힙니다 — 때린 쪽 없이 맞음 길(래그돌 반응 · 체력 · 절단)을 그대로 지나게 한다(시나리오).
             * @param bFatal 참이면 치명 표시를 달아 보낸다(때린 쪽이 정한 치명). 거짓이면 표시 없이 보내 받는 쪽의 체력이 쓰러짐을 정한다.
             */
            static bool deliverBoneHit( const string& objectName, const string& boneName, float32 damage, bool bFatal, string& outReply )
            {
                GameObject*             pObject  = engine::findActiveObject( hashed_string( objectName ) );
                const RagdollComponent* pRagdoll = pObject != nullptr ? pObject->getComponent<RagdollComponent>() : nullptr;
                const PhysicsAsset*     pAsset   = pRagdoll != nullptr ? pRagdoll->getPhysicsAsset() : nullptr;
                if ( pAsset == nullptr )
                {
                    StringBuilder<constant::buffer::kMax256> builder;
                    builder.appendFormat( "no object named '%#' with a ragdoll physics asset in the active scene", objectName );
                    outReply = string( builder.view() );
                    return false;
                }
                const hashed_string bone( boneName );
                for ( size_t bodyIndex = 0; bodyIndex < pAsset->_listBody.size(); ++bodyIndex )
                {
                    if ( pAsset->_listBody[bodyIndex]._bone != bone )
                        continue;
                    HitInfo hit;
                    hit._bodyIndex = static_cast<uint32>( bodyIndex );
                    hit._direction = float3{ 0.0f, 0.0f, 1.0f };
                    hit._damage    = damage;
                    hit._impulse   = kHitImpulse;
                    hit._bFatal    = bFatal;
                    CharacterRayHit::deliverHit( *pObject, hit );
                    StringBuilder<constant::buffer::kMax256> builder;
                    builder.appendFormat( "%#%#", bFatal ? "fatal hit on " : "hit on ", boneName );
                    outReply = string( builder.view() );
                    return true;
                }
                StringBuilder<constant::buffer::kMax256> builder;
                builder.appendFormat( "the ragdoll has no body on bone '%#'", boneName );
                outReply = string( builder.view() );
                return false;
            }

            /** @brief `combat.fatalhit <name> <bone>` — 치명 표시를 단 맞음(피해 100)입니다. */
            static bool runFatalHit( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 2 )
                    return false;
                return deliverBoneHit( listArgument[0], listArgument[1], kFatalHitDamage, true, outReply );
            }

            /** @brief `combat.hit <name> <bone> <damage>` — 표시 없는 맞음입니다. 쓰러짐은 받는 쪽의 체력 원천이 정하고 치명 신호를 다시 보낸다(게임의 총알 길과 같다). */
            static bool runHit( const vector<string>& listArgument, string& outReply )
            {
                float32 damage = 0.0f;
                if ( listArgument.size() != 3 || StringUtil::parseFloat( listArgument[2], damage ) == false )
                    return false;
                return deliverBoneHit( listArgument[0], listArgument[1], damage, false, outReply );
            }

            /** @brief 개발 명령 `nav.moveto <agent> <target>` · `nav.moveto <agent> <x> <y> <z>` — 이름 붙은 오브젝트의 내비메시 에이전트에 목적지를 겁니다(시나리오가 NPC 를 먼 셀로 보낸다). */
            static bool runMoveTo( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 2 && listArgument.size() != 4 )
                    return false;
                GameObject*            pObject = engine::findActiveObject( hashed_string( listArgument[0] ) );
                NavMeshAgentComponent* pAgent  = pObject != nullptr ? pObject->getComponent<NavMeshAgentComponent>() : nullptr;
                if ( pAgent == nullptr )
                {
                    StringBuilder<constant::buffer::kMax256> reply;
                    reply.appendFormat( "no object named '%#' with a navmesh agent in the active scene", listArgument[0] );
                    outReply.assign( reply.view() );
                    return false;
                }
                float3 destination{};
                if ( listArgument.size() == 4 )
                {
                    const bool bParsed = StringUtil::parseFloat( listArgument[1], destination._x ) && StringUtil::parseFloat( listArgument[2], destination._y ) &&
                                         StringUtil::parseFloat( listArgument[3], destination._z );
                    if ( bParsed == false )
                        return false;
                }
                else
                {
                    const GameObject*     pGoal  = engine::findActiveObject( hashed_string( listArgument[1] ) );
                    const SceneComponent* pScene = pGoal != nullptr ? pGoal->getPrimarySceneComponent() : nullptr;
                    if ( pScene == nullptr )
                    {
                        StringBuilder<constant::buffer::kMax256> reply;
                        reply.appendFormat( "no object named '%#' with a scene component in the active scene", listArgument[1] );
                        outReply.assign( reply.view() );
                        return false;
                    }
                    destination = pScene->getWorldPosition();
                }
                pAgent->setDestination( destination );
                StringBuilder<constant::buffer::kMax128> reply;
                reply.appendFormat( "moving %#", listArgument[0] );
                outReply.assign( reply.view() );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( CombatFatalHit, "combat.fatalhit", "combat.fatalhit <name> <bone>", "Deliver a fatal hit to the ragdoll body on a bone of a named object of the active scene", &CharacterCommandsInternal::runFatalHit );
    SW_DEV_COMMAND( CombatHit, "combat.hit", "combat.hit <name> <bone> <damage>", "Deliver an unmarked hit to the ragdoll body on a bone of a named object - the target's health decides whether it is fatal",
                    &CharacterCommandsInternal::runHit );
    SW_DEV_COMMAND( NavMoveTo, "nav.moveto", "nav.moveto <agent> <target object> | nav.moveto <agent> <x> <y> <z>",
                    "Send a named navmesh agent of the active scene to an object or a world position (long routes over streamed-out cells)", &CharacterCommandsInternal::runMoveTo );
} // namespace sw
