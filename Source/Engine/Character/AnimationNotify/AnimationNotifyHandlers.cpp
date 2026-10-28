#include "pch.h"

#include "Engine/Character/AnimationNotify/AnimationNotifyHandlers.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/Scene/MotionWarpingComponent.h"
#include "Engine/Audio/Component/SceneAudio.h"
#include "Engine/Audio/IAudioService.h"
#include "Engine/Character/AnimationNotify/AnimationNotifyComponent.h"
#include "Engine/Character/AnimationNotify/AnimationNotifyTable.h"
#include "Engine/Character/Hit/CharacterHit.h"
#include "Engine/Character/Hit/MeleeHitWindow.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/CameraRegistry.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Resource/AssetManager.h"

namespace sw
{
    SW_LOG_CALLER( "AnimationNotify" );

    namespace
    {
        struct AnimationNotifyHandlersInternal
        {
            static constexpr uint32 kAllLayers = 0xFFFFFFFFu;

            /** @brief 처리기가 한 일 하나를 컴포넌트에 적습니다. */
            static void record( AnimationNotifyContext& context, const hashed_string& detail, const float3& position, uint64 targetObjectID, AnimationNotifyPhase phase )
            {
                AnimationNotifyAction action;
                action._notify         = context._fired._name;
                action._handler        = context._entry._handlerName;
                action._detail         = detail;
                action._position       = position;
                action._targetObjectID = targetObjectID;
                action._phase          = phase;
                context._component.recordAction( action );
            }

            /**
             * @brief 알림을 받은 오브젝트가 사는 씬의 오디오 서비스입니다. 없으면 nullptr 입니다.
             * @details 처리기는 기본 레지스트리(프로세스 수명)에 한 번 등록되므로 서비스를 들고 있지 않는다. 씬이 받은 것을 그때그때 씬에서 읽는다.
             */
            static IAudioService* findAudioService( const AnimationNotifyContext& context )
            {
                GameObjectManager* pManager = context._owner.getManager();
                return pManager != nullptr ? pManager->getSubsystem<sw::SceneAudio>().findAudioService() : nullptr;
            }

            /** @class PlaySoundHandler @brief 소켓 위치에서 소리를 냅니다(현재 오디오는 위치를 받지 않으므로, 위치는 기록에만 남깁니다). */
            class PlaySoundHandler final : public IAnimationNotifyHandler
            {
            public:
                vector_reference<const AnimationNotifyParamDef> getParams() const override { return vector_reference<const AnimationNotifyParamDef>{ kArrParam }; }
                void                                            onNotify( AnimationNotifyContext& context ) const override
                {
                    static const hashed_string s_sound( "sound" );
                    static const hashed_string s_socket( "socket" );
                    const string               sound    = context._entry.getTextParam( s_sound );
                    const float3               position = context._component.findSocketWorldPosition( context._entry.getNameParam( s_socket ) );
                    IAudioService*             pAudio   = findAudioService( context );
                    if ( pAudio != nullptr )
                        (void)pAudio->play( ResourceID{ sound } );
                    record( context, hashed_string( sound ), position, 0, AnimationNotifyPhase::Instant );
                }

            private:
                static constexpr AnimationNotifyParamDef kArrParam[] = {
                    { "sound", AnimationNotifyParamKind::Text,  true},
                    {"socket", AnimationNotifyParamKind::Name, false},
                };
            };

            /** @class SpawnPrefabHandler @brief 소켓 변환에 프리팹을 스폰합니다(이펙트, 탄피). `attach` 면 오브젝트에 붙입니다. */
            class SpawnPrefabHandler final : public IAnimationNotifyHandler
            {
            public:
                vector_reference<const AnimationNotifyParamDef> getParams() const override { return vector_reference<const AnimationNotifyParamDef>{ kArrParam }; }
                void                                            onNotify( AnimationNotifyContext& context ) const override
                {
                    static const hashed_string s_prefab( "prefab" );
                    static const hashed_string s_socket( "socket" );
                    static const hashed_string s_offset( "offset" );
                    static const hashed_string s_attach( "attach" );
                    const string               prefab = context._entry.getTextParam( s_prefab );
                    float4x4                   world;
                    (void)context._component.findSocketWorldTransform( context._entry.getNameParam( s_socket ), world ); // 없으면 오브젝트 루트 변환이 담긴다(선언 설명)
                    const float3       offset   = context._entry.getVectorParam( s_offset );
                    const float3       position = float3::transform( offset, world );
                    GameObjectManager* pManager = context._owner.getManager();
                    AssetManager*      pAssets  = pManager != nullptr ? pManager->findAssetManager() : nullptr;
                    GameObject*        pSpawned = ( pManager != nullptr && pAssets != nullptr ) ? pAssets->getAssetCache<PrefabCache>().spawn( pManager, prefab ) : nullptr;
                    SceneComponent*    pScene   = pSpawned != nullptr ? pSpawned->getPrimarySceneComponent() : nullptr;
                    if ( pScene != nullptr )
                    {
                        float4x4 spawnWorld = world;
                        spawnWorld.setTranslation( position );
                        pScene->setWorldTransform( spawnWorld );
                        SceneComponent* pOwnerRoot = context._owner.getPrimarySceneComponent();
                        if ( context._entry.getBoolParam( s_attach ) && pOwnerRoot != nullptr )
                            (void)pScene->attachToComponent( pOwnerRoot, AttachRule::KeepWorld ); // 붙일 수 없는 부모면 월드 위치에 따로 둔다.
                    }
                    record( context, hashed_string( prefab ), position, pSpawned != nullptr ? pSpawned->getObjectID() : 0, AnimationNotifyPhase::Instant );
                }

            private:
                static constexpr AnimationNotifyParamDef kArrParam[] = {
                    {"prefab",   AnimationNotifyParamKind::Text,  true},
                    {"socket",   AnimationNotifyParamKind::Name, false},
                    {"offset", AnimationNotifyParamKind::Vector, false},
                    {"attach",   AnimationNotifyParamKind::Bool, false},
                };
            };

            /** @class FootstepHandler @brief 발 소켓 아래의 물리 재질을 바닥 종류로 고르고, 소리 경로의 `{surface}` 를 그 이름으로 바꿉니다. */
            class FootstepHandler final : public IAnimationNotifyHandler
            {
            public:
                vector_reference<const AnimationNotifyParamDef> getParams() const override { return vector_reference<const AnimationNotifyParamDef>{ kArrParam }; }
                void                                            onNotify( AnimationNotifyContext& context ) const override
                {
                    static const hashed_string s_socket( "socket" );
                    static const hashed_string s_distance( "distance" );
                    static const hashed_string s_sound( "sound" );
                    const float3               foot     = context._component.findSocketWorldPosition( context._entry.getNameParam( s_socket ) );
                    const float32              distance = MathUtil::max( context._entry.getFloatParam( s_distance ), 0.01f );
                    // 발 위 반 거리에서 아래로 쏜다. 발이 바닥에 조금 묻혀 있어도 바닥을 맞힌다.
                    const float3    from = foot + float3{ 0.0f, distance * 0.5f, 0.0f };
                    const float3    to   = foot - float3{ 0.0f, distance, 0.0f };
                    CharacterRayHit hit;
                    if ( context._component.castFromTo( from, to, 0.0f, kAllLayers, hit ) == false )
                    {
                        record( context, hashed_string{}, foot, 0, AnimationNotifyPhase::Instant );
                        return;
                    }
                    static const hashed_string s_default( "Default" );
                    const hashed_string        surface = hit._material.empty() ? s_default : hit._material;
                    const string               sound   = context._entry.getTextParam( s_sound );
                    IAudioService*             pAudio  = findAudioService( context );
                    if ( sound.empty() == false && pAudio != nullptr )
                        (void)pAudio->play( ResourceID{ StringUtil::replace( sound, "{surface}", StringUtil::toLower( surface.c_str() ) ) } );
                    record( context, surface, hit._point, hit._pObject != nullptr ? hit._pObject->getObjectID() : 0, AnimationNotifyPhase::Instant );
                }

            private:
                static constexpr AnimationNotifyParamDef kArrParam[] = {
                    { "socket", AnimationNotifyParamKind::Name, true },
                    { "distance", AnimationNotifyParamKind::Float, false, 0.5f },
                    { "sound", AnimationNotifyParamKind::Text, false },
                };
            };

            /**
             * @class HitWindowHandler
             * @brief 구간 동안 두 소켓 사이 칼날을 프레임마다 쓸어서 맞힙니다. 판정은 `MeleeHitWindowUtil::sweep` 한 길이고, 이 처리기는 소켓에서 칼날을 만들고
             *        맞음을 기록에 남기기만 합니다. 한 구간에 오브젝트 하나는 한 번만 맞고, 클라이언트는 맞음을 기록만 하고 전달하지 않습니다(서버 권위).
             */
            class HitWindowHandler final : public IAnimationNotifyHandler
            {
            public:
                vector_reference<const AnimationNotifyParamDef> getParams() const override { return vector_reference<const AnimationNotifyParamDef>{ kArrParam }; }
                bool                                            supportsState() const override { return true; }
                void                                            onNotify( AnimationNotifyContext& context ) const override
                {
                    // 길이 없는 판정이면 현재 칼날 하나만 측정한다(지난 칼날이 없다).
                    AnimationNotifyStateData data{};
                    vector<float3>           listCurrent;
                    collectBlade( context, listCurrent );
                    sweep( context, data, listCurrent );
                }
                void onNotifyBegin( AnimationNotifyContext& context ) const override
                {
                    if ( context._pState == nullptr )
                        return;
                    context._pState->_listHitTarget.clear();
                    collectBlade( context, context._pState->_listPreviousPoint );
                }
                void onNotifyTick( AnimationNotifyContext& context ) const override { trace( context ); }
                void onNotifyEnd( AnimationNotifyContext& context ) const override { trace( context ); }

            private:
                /** @brief 칼날 위 표본점(소켓 A → B, 양 끝 포함)입니다. */
                static void collectBlade( const AnimationNotifyContext& context, vector<float3>& outListPoint )
                {
                    static const hashed_string s_socketA( "socketA" );
                    static const hashed_string s_socketB( "socketB" );
                    static const hashed_string s_samples( "samples" );
                    const float3               pointA      = context._component.findSocketWorldPosition( context._entry.getNameParam( s_socketA ) );
                    const float3               pointB      = context._component.findSocketWorldPosition( context._entry.getNameParam( s_socketB ) );
                    const uint32               sampleCount = static_cast<uint32>( MathUtil::clamp( context._entry.getFloatParam( s_samples ), 2.0f, 16.0f ) );
                    MeleeHitWindowUtil::makeBlade( pointA, pointB, sampleCount, outListPoint );
                }

                /** @brief 지난 위치에서 현재 위치까지 쓸고, 현재 위치를 다음 기준으로 넘깁니다. */
                static void trace( AnimationNotifyContext& context )
                {
                    if ( context._pState == nullptr )
                        return;
                    vector<float3> listCurrent;
                    collectBlade( context, listCurrent );
                    sweep( context, *context._pState, listCurrent );
                    context._pState->_listPreviousPoint = std::move( listCurrent );
                }

                static void sweep( AnimationNotifyContext& context, AnimationNotifyStateData& state, const vector<float3>& listCurrent )
                {
                    static const hashed_string s_radius( "radius" );
                    static const hashed_string s_damage( "damage" );
                    static const hashed_string s_impulse( "impulse" );
                    MeleeHitParams             params;
                    params._kind      = context._fired._name;
                    params._radius    = MathUtil::max( context._entry.getFloatParam( s_radius ), 0.0f );
                    params._damage    = context._entry.getFloatParam( s_damage );
                    params._impulse   = context._entry.getFloatParam( s_impulse );
                    params._layerMask = kAllLayers;
                    params._bIs2D     = context._component.is2D();
                    vector<MeleeHit> listHit;
                    (void)MeleeHitWindowUtil::sweep( context._owner, state._listPreviousPoint, listCurrent, params, MeleeHitWindowUtil::getProcessAuthority(),
                                                     state._listHitTarget, listHit );
                    for ( const MeleeHit& melee : listHit )
                    {
                        record( context, melee._hit._zone, melee._hit._point, melee._target.objectID(), context._fired._phase );
                    }
                }

                static constexpr AnimationNotifyParamDef kArrParam[] = {
                    { "socketA", AnimationNotifyParamKind::Name, true },
                    { "socketB", AnimationNotifyParamKind::Name, true },
                    { "radius", AnimationNotifyParamKind::Float, false },
                    { "damage", AnimationNotifyParamKind::Float, false, 1.0f },
                    { "impulse", AnimationNotifyParamKind::Float, false },
                    { "samples", AnimationNotifyParamKind::Float, false, 3.0f },
                };
            };

            /** @class CameraShakeHandler @brief 카메라 충격을 요청합니다(듣는 카메라가 받는다). */
            class CameraShakeHandler final : public IAnimationNotifyHandler
            {
            public:
                vector_reference<const AnimationNotifyParamDef> getParams() const override { return vector_reference<const AnimationNotifyParamDef>{ kArrParam }; }
                void                                            onNotify( AnimationNotifyContext& context ) const override
                {
                    static const hashed_string s_amplitude( "amplitude" );
                    static const hashed_string s_duration( "duration" );
                    static const hashed_string s_frequency( "frequency" );
                    static const hashed_string s_radius( "radius" );
                    static const hashed_string s_socket( "socket" );
                    CameraShakeRequest         request;
                    request._origin         = context._component.findSocketWorldPosition( context._entry.getNameParam( s_socket ) );
                    request._sourceObjectID = context._owner.getObjectID();
                    request._amplitude      = context._entry.getFloatParam( s_amplitude );
                    request._duration       = context._entry.getFloatParam( s_duration );
                    request._frequency      = context._entry.getFloatParam( s_frequency );
                    request._falloffRadius  = context._entry.getFloatParam( s_radius );
                    // 같은 씬의 카메라만 받는다. 매니저가 없는 오브젝트(씬 밖)는 들을 카메라도 없다.
                    if ( GameObjectManager* pManager = context._owner.getManager(); pManager != nullptr )
                        pManager->getCameraRegistry().getShakeChannel().broadcast( request );
                    record( context, hashed_string{}, request._origin, 0, AnimationNotifyPhase::Instant );
                }

            private:
                static constexpr AnimationNotifyParamDef kArrParam[] = {
                    { "amplitude", AnimationNotifyParamKind::Float, false, 0.1f },
                    { "duration", AnimationNotifyParamKind::Float, false, 0.3f },
                    { "frequency", AnimationNotifyParamKind::Float, false, 30.0f },
                    { "radius", AnimationNotifyParamKind::Float, false },
                    { "socket", AnimationNotifyParamKind::Name, false },
                };
            };

            /** @class GameplayEventHandler @brief 같은 오브젝트의 켜진 컴포넌트에 `onAnimationNotify` 를 보냅니다(구간이면 시작과 끝). */
            class GameplayEventHandler final : public IAnimationNotifyHandler
            {
            public:
                vector_reference<const AnimationNotifyParamDef> getParams() const override { return vector_reference<const AnimationNotifyParamDef>{ kArrParam }; }
                bool                                            supportsState() const override { return true; }
                void                                            onNotify( AnimationNotifyContext& context ) const override { deliver( context, AnimationNotifyPhase::Instant ); }
                void                                            onNotifyBegin( AnimationNotifyContext& context ) const override { deliver( context, AnimationNotifyPhase::Begin ); }
                void                                            onNotifyEnd( AnimationNotifyContext& context ) const override { deliver( context, AnimationNotifyPhase::End ); }

            private:
                static void deliver( AnimationNotifyContext& context, AnimationNotifyPhase phase )
                {
                    static const hashed_string s_event( "event" );
                    AnimationNotifyInfo        info;
                    info._notify = context._fired._name;
                    info._event  = context._entry.getNameParam( s_event );
                    info._weight = context._fired._weight;
                    info._phase  = phase;
                    record( context, info._event, context._component.findSocketWorldPosition( hashed_string{} ), context._owner.getObjectID(), phase );
                    const vector<Component*> listTarget( context._owner.getComponents().begin(), context._owner.getComponents().end() );
                    for ( Component* pComponent : listTarget )
                    {
                        const bool bComponentCanReceiveNotify = pComponent != nullptr && pComponent->isPendingDestroy() == false && pComponent->isSelfActive();
                        if ( bComponentCanReceiveNotify )
                            pComponent->onAnimationNotify( info );
                    }
                }

                static constexpr AnimationNotifyParamDef kArrParam[] = {
                    { "event", AnimationNotifyParamKind::Name, true },
                };
            };

            /**
             * @class MotionWarpHandler
             * @brief 구간 동안 같은 오브젝트의 `MotionWarpingComponent` 에 워프 창을 엽니다. 창 끝(구간 끝)에서 이름 붙은 목표에 닿도록 루트 모션을 휩니다.
             */
            class MotionWarpHandler final : public IAnimationNotifyHandler
            {
            public:
                vector_reference<const AnimationNotifyParamDef> getParams() const override { return vector_reference<const AnimationNotifyParamDef>{ kArrParam }; }
                bool                                            supportsState() const override { return true; }
                void                                            onNotifyBegin( AnimationNotifyContext& context ) const override
                {
                    static const hashed_string s_target( "target" );
                    static const hashed_string s_translation( "translation" );
                    static const hashed_string s_rotation( "rotation" );
                    static const hashed_string s_vertical( "vertical" );
                    MotionWarpingComponent*    pWarping = context._owner.getComponent<MotionWarpingComponent>();
                    if ( pWarping == nullptr || context._fired._pSource == nullptr )
                        return;
                    MotionWarpWindow window;
                    window._target       = context._entry.getNameParam( s_target );
                    window._pClip        = context._fired._pSource;
                    window._endTime      = MathUtil::min( context._fired._time + context._fired._duration, context._fired._pSource->getPlayLength() );
                    window._bTranslation = context._entry.getBoolParam( s_translation );
                    window._bRotation    = context._entry.getBoolParam( s_rotation );
                    window._bVertical    = context._entry.getBoolParam( s_vertical );
                    pWarping->beginWindow( window );
                    record( context, window._target, context._component.findSocketWorldPosition( hashed_string{} ), 0, AnimationNotifyPhase::Begin );
                }
                void onNotifyEnd( AnimationNotifyContext& context ) const override
                {
                    static const hashed_string s_target( "target" );
                    MotionWarpingComponent*    pWarping = context._owner.getComponent<MotionWarpingComponent>();
                    if ( pWarping != nullptr )
                        pWarping->endWindow( context._entry.getNameParam( s_target ), context._fired._pSource );
                }

            private:
                static constexpr AnimationNotifyParamDef kArrParam[] = {
                    { "target", AnimationNotifyParamKind::Name, true },
                    { "translation", AnimationNotifyParamKind::Bool, false, 0.0f, true },
                    { "rotation", AnimationNotifyParamKind::Bool, false, 0.0f, true },
                    { "vertical", AnimationNotifyParamKind::Bool, false },
                };
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool AnimationNotifyHandlerUtil::registerBuiltInHandlers( AnimationNotifyHandlerRegistry& registry )
    {
        registry.registerHandler( hashed_string( "PlaySound" ), make_unique<AnimationNotifyHandlersInternal::PlaySoundHandler>() );
        registry.registerHandler( hashed_string( "SpawnPrefab" ), make_unique<AnimationNotifyHandlersInternal::SpawnPrefabHandler>() );
        registry.registerHandler( hashed_string( "Footstep" ), make_unique<AnimationNotifyHandlersInternal::FootstepHandler>() );
        registry.registerHandler( hashed_string( "HitWindow" ), make_unique<AnimationNotifyHandlersInternal::HitWindowHandler>() );
        registry.registerHandler( hashed_string( "CameraShake" ), make_unique<AnimationNotifyHandlersInternal::CameraShakeHandler>() );
        registry.registerHandler( hashed_string( "GameplayEvent" ), make_unique<AnimationNotifyHandlersInternal::GameplayEventHandler>() );
        registry.registerHandler( hashed_string( "MotionWarp" ), make_unique<AnimationNotifyHandlersInternal::MotionWarpHandler>() );
        return true;
    }
} // namespace sw
