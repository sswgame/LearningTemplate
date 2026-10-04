#include "pch.h"

#include "Engine/Character/AnimNotifyHandlers.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Audio/IAudioSystem.h"
#include "Engine/Character/AnimNotifyComponent.h"
#include "Engine/Character/AnimNotifyTable.h"
#include "Engine/Character/CharacterHit.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Resource/AssetManager.h"

namespace sw
{
    SW_LOG_CALLER( "AnimNotify" );

    namespace
    {
        struct AnimNotifyHandlersInternal
        {
            static constexpr uint32 kAllLayers = 0xFFFFFFFFu;

            /** @brief 처리기가 한 일 하나를 컴포넌트에 적습니다. */
            static void record( AnimNotifyContext& context, const hashed_string& detail, const float3& position, uint64 targetObjectId, AnimNotifyPhase phase )
            {
                AnimNotifyAction action;
                action._notify         = context._fired._name;
                action._handler        = context._entry._handlerName;
                action._detail         = detail;
                action._position       = position;
                action._targetObjectId = targetObjectId;
                action._phase          = phase;
                context._component.recordAction( action );
            }

            static IAudioSystem* findAudioSystem()
            {
                return engine::getBoundEngineServices()._pAudioSystem;
            }

            /** @class PlaySoundHandler @brief 소켓 자리에서 소리를 냅니다(지금 오디오는 위치를 받지 않는다 — 자리는 기록에 남긴다). */
            class PlaySoundHandler final : public IAnimNotifyHandler
            {
            public:
                vector_reference<const AnimNotifyParamDef> getParams() const override { return vector_reference<const AnimNotifyParamDef>{ kArrParam }; }
                void                                       onNotify( AnimNotifyContext& context ) const override
                {
                    static const hashed_string s_sound( "sound" );
                    static const hashed_string s_socket( "socket" );
                    const string               sound    = context._entry.getTextParam( s_sound );
                    const float3               position = context._component.findSocketWorldPosition( context._entry.getNameParam( s_socket ) );
                    IAudioSystem*              pAudio   = findAudioSystem();
                    if ( pAudio != nullptr )
                        (void)pAudio->play( sound );
                    record( context, hashed_string( sound ), position, 0, AnimNotifyPhase::Instant );
                }

            private:
                static constexpr AnimNotifyParamDef kArrParam[] = {
                    { "sound", AnimNotifyParamKind::Text,  true},
                    {"socket", AnimNotifyParamKind::Name, false},
                };
            };

            /** @class SpawnPrefabHandler @brief 소켓 변환에 프리팹을 스폰합니다(이펙트 · 탄피). `attach` 면 오브젝트에 붙입니다. */
            class SpawnPrefabHandler final : public IAnimNotifyHandler
            {
            public:
                vector_reference<const AnimNotifyParamDef> getParams() const override { return vector_reference<const AnimNotifyParamDef>{ kArrParam }; }
                void                                       onNotify( AnimNotifyContext& context ) const override
                {
                    static const hashed_string s_prefab( "prefab" );
                    static const hashed_string s_socket( "socket" );
                    static const hashed_string s_offset( "offset" );
                    static const hashed_string s_attach( "attach" );
                    const string               prefab = context._entry.getTextParam( s_prefab );
                    float4x4                   world;
                    (void)context._component.findSocketWorldTransform( context._entry.getNameParam( s_socket ), world );
                    const float3       offset   = context._entry.getVectorParam( s_offset, float3{} );
                    const float3       position = float3::transform( offset, world );
                    GameObjectManager* pManager = context._owner.getManager();
                    AssetManager*      pAssets  = engine::getBoundEngineServices()._pAssetManager;
                    GameObject*        pSpawned = ( pManager != nullptr && pAssets != nullptr ) ? pAssets->getPrefabCache().spawn( pManager, prefab ) : nullptr;
                    SceneComponent*    pScene   = pSpawned != nullptr ? pSpawned->getPrimarySceneComponent() : nullptr;
                    if ( pScene != nullptr )
                    {
                        float4x4 spawnWorld = world;
                        spawnWorld.setTranslation( position );
                        pScene->setWorldTransform( spawnWorld );
                        SceneComponent* pOwnerRoot = context._owner.getPrimarySceneComponent();
                        if ( context._entry.getBoolParam( s_attach, false ) && pOwnerRoot != nullptr )
                            (void)pScene->attachToComponent( pOwnerRoot, AttachRule::KeepWorld );
                    }
                    record( context, hashed_string( prefab ), position, pSpawned != nullptr ? pSpawned->getObjectId() : 0, AnimNotifyPhase::Instant );
                }

            private:
                static constexpr AnimNotifyParamDef kArrParam[] = {
                    {"prefab",   AnimNotifyParamKind::Text,  true},
                    {"socket",   AnimNotifyParamKind::Name, false},
                    {"offset", AnimNotifyParamKind::Vector, false},
                    {"attach",   AnimNotifyParamKind::Bool, false},
                };
            };

            /** @class FootstepHandler @brief 발 소켓 아래의 물리 재질을 바닥 종류로 고르고, 소리 경로의 `{surface}` 를 그 이름으로 바꿉니다. */
            class FootstepHandler final : public IAnimNotifyHandler
            {
            public:
                vector_reference<const AnimNotifyParamDef> getParams() const override { return vector_reference<const AnimNotifyParamDef>{ kArrParam }; }
                void                                       onNotify( AnimNotifyContext& context ) const override
                {
                    static const hashed_string s_socket( "socket" );
                    static const hashed_string s_distance( "distance" );
                    static const hashed_string s_sound( "sound" );
                    const float3               foot     = context._component.findSocketWorldPosition( context._entry.getNameParam( s_socket ) );
                    const float32              distance = MathUtil::max( context._entry.getFloatParam( s_distance, 0.5f ), 0.01f );
                    // 발 위 반 거리에서 아래로 — 발이 바닥에 조금 묻혀 있어도 바닥을 맞힌다.
                    const float3    from = foot + float3{ 0.0f, distance * 0.5f, 0.0f };
                    const float3    to   = foot - float3{ 0.0f, distance, 0.0f };
                    CharacterRayHit hit;
                    if ( context._component.castFromTo( from, to, 0.0f, kAllLayers, hit ) == false )
                    {
                        record( context, hashed_string{}, foot, 0, AnimNotifyPhase::Instant );
                        return;
                    }
                    static const hashed_string s_default( "Default" );
                    const hashed_string        surface = hit._material.empty() ? s_default : hit._material;
                    const string               sound   = context._entry.getTextParam( s_sound );
                    IAudioSystem*              pAudio  = findAudioSystem();
                    if ( sound.empty() == false && pAudio != nullptr )
                        (void)pAudio->play( StringUtil::replace( sound, "{surface}", StringUtil::toLower( surface.c_str() ) ) );
                    record( context, surface, hit._point, hit._pObject != nullptr ? hit._pObject->getObjectId() : 0, AnimNotifyPhase::Instant );
                }

            private:
                static constexpr AnimNotifyParamDef kArrParam[] = {
                    {  "socket",  AnimNotifyParamKind::Name,  true},
                    {"distance", AnimNotifyParamKind::Float, false},
                    {   "sound",  AnimNotifyParamKind::Text, false},
                };
            };

            /**
             * @class HitWindowHandler
             * @brief 구간 동안 두 소켓 사이 칼날을 프레임마다 쓸어 맞힙니다 — 칼날 위 표본점마다 지난 자리 → 지금 자리, 그리고 지금의 칼날 자체.
             *        한 구간에 오브젝트 하나는 한 번만 맞습니다(언리얼 근접 판정 창 · AnimNotifyState 의 trace).
             */
            class HitWindowHandler final : public IAnimNotifyHandler
            {
            public:
                vector_reference<const AnimNotifyParamDef> getParams() const override { return vector_reference<const AnimNotifyParamDef>{ kArrParam }; }
                bool                                       supportsState() const override { return true; }
                void                                       onNotify( AnimNotifyContext& context ) const override
                {
                    // 길이 없는 판정 — 지금 칼날 하나만 잰다.
                    AnimNotifyStateData data{};
                    AnimNotifyContext   stateContext{ context._component, context._owner, context._entry, context._fired, &data, context._deltaSeconds };
                    collectBlade( stateContext, data._listPreviousPoint );
                    sweep( stateContext, data, data._listPreviousPoint );
                }
                void onNotifyBegin( AnimNotifyContext& context ) const override
                {
                    if ( context._pState == nullptr )
                        return;
                    context._pState->_listHitObjectId.clear();
                    collectBlade( context, context._pState->_listPreviousPoint );
                    context._pState->_bHasPrevious = SW_TRUE;
                }
                void onNotifyTick( AnimNotifyContext& context ) const override { trace( context ); }
                void onNotifyEnd( AnimNotifyContext& context ) const override { trace( context ); }

            private:
                /** @brief 칼날 위 표본점(소켓 A → B, 양 끝 포함)입니다. */
                static void collectBlade( const AnimNotifyContext& context, vector<float3>& outListPoint )
                {
                    static const hashed_string s_socketA( "socketA" );
                    static const hashed_string s_socketB( "socketB" );
                    static const hashed_string s_samples( "samples" );
                    const float3               pointA      = context._component.findSocketWorldPosition( context._entry.getNameParam( s_socketA ) );
                    const float3               pointB      = context._component.findSocketWorldPosition( context._entry.getNameParam( s_socketB ) );
                    const uint32               sampleCount = static_cast<uint32>( MathUtil::clamp( context._entry.getFloatParam( s_samples, 3.0f ), 2.0f, 16.0f ) );
                    outListPoint.resize( sampleCount );
                    for ( uint32 sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex )
                        outListPoint[sampleIndex] = float3::lerp( pointA, pointB, static_cast<float32>( sampleIndex ) / static_cast<float32>( sampleCount - 1 ) );
                }

                /** @brief 지난 자리 → 지금 자리를 쓸고 지금으로 넘깁니다. */
                static void trace( AnimNotifyContext& context )
                {
                    if ( context._pState == nullptr )
                        return;
                    vector<float3> listCurrent;
                    collectBlade( context, listCurrent );
                    sweep( context, *context._pState, listCurrent );
                    context._pState->_listPreviousPoint = std::move( listCurrent );
                    context._pState->_bHasPrevious      = SW_TRUE;
                }

                static void sweep( AnimNotifyContext& context, AnimNotifyStateData& state, const vector<float3>& listCurrent )
                {
                    static const hashed_string s_radius( "radius" );
                    const float32              radius = MathUtil::max( context._entry.getFloatParam( s_radius, 0.0f ), 0.0f );
                    const bool                 bMoved = state._bHasPrevious == SW_TRUE && state._listPreviousPoint.size() == listCurrent.size();
                    for ( size_t pointIndex = 0; bMoved && pointIndex < listCurrent.size(); ++pointIndex )
                        castAndHit( context, state, state._listPreviousPoint[pointIndex], listCurrent[pointIndex], radius );
                    if ( listCurrent.size() >= 2 )
                        castAndHit( context, state, listCurrent.front(), listCurrent.back(), radius );
                }

                static void castAndHit( AnimNotifyContext& context, AnimNotifyStateData& state, const float3& from, const float3& to, float32 radius )
                {
                    static const hashed_string s_damage( "damage" );
                    static const hashed_string s_impulse( "impulse" );
                    CharacterRayHit            rayHit;
                    if ( context._component.castFromTo( from, to, radius, kAllLayers, rayHit ) == false || rayHit._pObject == nullptr )
                        return;
                    const uint64 targetId = rayHit._pObject->getObjectId();
                    if ( std::find( state._listHitObjectId.begin(), state._listHitObjectId.end(), targetId ) != state._listHitObjectId.end() )
                        return;
                    state._listHitObjectId.push_back( targetId );
                    HitInfo hit;
                    hit._pInstigator = &context._owner;
                    hit._body        = rayHit._body;
                    hit._kind        = context._fired._name;
                    hit._point       = rayHit._point;
                    hit._normal      = rayHit._normal;
                    hit._direction   = ( to - from ).getLengthSquared() > 1.0e-12f ? ( to - from ).normalize() : float3{};
                    hit._damage      = context._entry.getFloatParam( s_damage, 1.0f );
                    hit._impulse     = context._entry.getFloatParam( s_impulse, 0.0f );
                    hit._bIs2D       = rayHit._bIs2D;
                    CharacterHitUtil::resolveHitZone( *rayHit._pObject, rayHit._body, hit );
                    record( context, hit._zone, hit._point, targetId, context._fired._phase );
                    CharacterHitUtil::deliverHit( *rayHit._pObject, hit );
                }

                static constexpr AnimNotifyParamDef kArrParam[] = {
                    {"socketA",  AnimNotifyParamKind::Name,  true},
                    {"socketB",  AnimNotifyParamKind::Name,  true},
                    { "radius", AnimNotifyParamKind::Float, false},
                    { "damage", AnimNotifyParamKind::Float, false},
                    {"impulse", AnimNotifyParamKind::Float, false},
                    {"samples", AnimNotifyParamKind::Float, false},
                };
            };

            /** @class CameraShakeHandler @brief 카메라 충격을 요청합니다(듣는 카메라가 받는다). */
            class CameraShakeHandler final : public IAnimNotifyHandler
            {
            public:
                vector_reference<const AnimNotifyParamDef> getParams() const override { return vector_reference<const AnimNotifyParamDef>{ kArrParam }; }
                void                                       onNotify( AnimNotifyContext& context ) const override
                {
                    static const hashed_string s_amplitude( "amplitude" );
                    static const hashed_string s_duration( "duration" );
                    static const hashed_string s_frequency( "frequency" );
                    static const hashed_string s_radius( "radius" );
                    static const hashed_string s_socket( "socket" );
                    CameraShakeRequest         request;
                    request._origin         = context._component.findSocketWorldPosition( context._entry.getNameParam( s_socket ) );
                    request._sourceObjectId = context._owner.getObjectId();
                    request._amplitude      = context._entry.getFloatParam( s_amplitude, request._amplitude );
                    request._duration       = context._entry.getFloatParam( s_duration, request._duration );
                    request._frequency      = context._entry.getFloatParam( s_frequency, request._frequency );
                    request._falloffRadius  = context._entry.getFloatParam( s_radius, request._falloffRadius );
                    AnimNotifyHandlerUtil::getCameraShakeRequested().broadcast( request );
                    record( context, hashed_string{}, request._origin, 0, AnimNotifyPhase::Instant );
                }

            private:
                static constexpr AnimNotifyParamDef kArrParam[] = {
                    {"amplitude", AnimNotifyParamKind::Float, false},
                    { "duration", AnimNotifyParamKind::Float, false},
                    {"frequency", AnimNotifyParamKind::Float, false},
                    {   "radius", AnimNotifyParamKind::Float, false},
                    {   "socket",  AnimNotifyParamKind::Name, false},
                };
            };

            /** @class GameplayEventHandler @brief 같은 오브젝트의 켜진 컴포넌트에 `onAnimNotify` 를 보냅니다(구간이면 시작 · 끝). */
            class GameplayEventHandler final : public IAnimNotifyHandler
            {
            public:
                vector_reference<const AnimNotifyParamDef> getParams() const override { return vector_reference<const AnimNotifyParamDef>{ kArrParam }; }
                bool                                       supportsState() const override { return true; }
                void                                       onNotify( AnimNotifyContext& context ) const override { deliver( context, AnimNotifyPhase::Instant ); }
                void                                       onNotifyBegin( AnimNotifyContext& context ) const override { deliver( context, AnimNotifyPhase::Begin ); }
                void                                       onNotifyEnd( AnimNotifyContext& context ) const override { deliver( context, AnimNotifyPhase::End ); }

            private:
                static void deliver( AnimNotifyContext& context, AnimNotifyPhase phase )
                {
                    static const hashed_string s_event( "event" );
                    AnimNotifyInfo             info;
                    info._notify = context._fired._name;
                    info._event  = context._entry.getNameParam( s_event );
                    info._weight = context._fired._weight;
                    info._phase  = phase;
                    record( context, info._event, context._component.findSocketWorldPosition( hashed_string{} ), context._owner.getObjectId(), phase );
                    const vector<Component*> listTarget( context._owner.getComponents().begin(), context._owner.getComponents().end() );
                    for ( Component* pComponent : listTarget )
                    {
                        if ( pComponent != nullptr && pComponent->isPendingDestroy() == false && pComponent->isSelfActive() )
                            pComponent->onAnimNotify( info );
                    }
                }

                static constexpr AnimNotifyParamDef kArrParam[] = {
                    { "event", AnimNotifyParamKind::Name, true },
                };
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool AnimNotifyHandlerUtil::registerBuiltInHandlers( AnimNotifyHandlerRegistry& registry )
    {
        registry.registerHandler( hashed_string( "PlaySound" ), make_unique<AnimNotifyHandlersInternal::PlaySoundHandler>() );
        registry.registerHandler( hashed_string( "SpawnPrefab" ), make_unique<AnimNotifyHandlersInternal::SpawnPrefabHandler>() );
        registry.registerHandler( hashed_string( "Footstep" ), make_unique<AnimNotifyHandlersInternal::FootstepHandler>() );
        registry.registerHandler( hashed_string( "HitWindow" ), make_unique<AnimNotifyHandlersInternal::HitWindowHandler>() );
        registry.registerHandler( hashed_string( "CameraShake" ), make_unique<AnimNotifyHandlersInternal::CameraShakeHandler>() );
        registry.registerHandler( hashed_string( "GameplayEvent" ), make_unique<AnimNotifyHandlersInternal::GameplayEventHandler>() );
        return true;
    }

    CameraShakeRequestDelegate& AnimNotifyHandlerUtil::getCameraShakeRequested()
    {
        static CameraShakeRequestDelegate s_delegate;
        return s_delegate;
    }
} // namespace sw
