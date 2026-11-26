#include "pch.h"

#include "Engine/Audio/Scene/SceneAudio.h"

#include "Core/Log/LogOnce.h"
#include "Core/Log/ScopedDeferredLog.h"
#include "Core/Math/MathUtil.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/AudioSubsystem.h"
#include "Engine/Audio/Component/AudioEmitterComponent.h"
#include "Engine/Audio/Component/AudioListenerComponent.h"
#include "Engine/Audio/Component/AudioReverbZoneComponent.h"
#include "Engine/Audio/Scene/PhysicsAudioOcclusionQuery.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Foundation/EngineParallel.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Subsystem/SceneEngineSubsystems.h"
#include "Engine/Object/GameObject/Subsystem/ScenePreviewPolicy.h"
#include "Engine/Object/GameObject/Subsystem/SceneSubsystemRegistry.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/Scene/ScenePhysics.h"

namespace sw
{
    namespace
    {
        struct SceneAudioInternal
        {
            /** @brief 직교 카메라의 화면 가로세로비 가정입니다(창 크기를 모르는 자리 — 정확한 반폭은 리스너 컴포넌트로). */
            static constexpr float32 kAssumedAspect = 16.0f / 9.0f;
            /** @brief 속도의 한계(m/s)입니다 — 순간이동한 프레임이 도플러를 튀게 하지 않게. */
            static constexpr float32 kMaxSpeed = 100.0f;

            /** @brief 켜져 있고 삭제 대기가 아닌 컴포넌트인지입니다. */
            static bool isUsable( const Component* pComponent )
            {
                // 소유 오브젝트의 삭제 대기는 다시 보지 않는다. 그 오브젝트의 컴포넌트는 항상 삭제 대기다(`GameObject::isPendingDestroy`).
                const bool bUnusableComponent = pComponent == nullptr || pComponent->isPendingDestroy() || pComponent->isActive() == false;
                return bUnusableComponent == false && pComponent->getOwner() != nullptr;
            }

            /** @brief 지난 자리와 지금 자리로 속도를 측정합니다(한계로 묶음). 첫 프레임은 0 입니다. */
            static float3 computeVelocity( const float3& position, const optional<float3>& lastPosition, float32 deltaSeconds )
            {
                if ( lastPosition.has_value() == false || deltaSeconds <= 1e-5f )
                    return float3{};
                float3        velocity = ( position - *lastPosition ) / deltaSeconds;
                const float32 speed    = velocity.getLength();
                if ( speed > kMaxSpeed )
                    velocity *= kMaxSpeed / speed;
                return velocity;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    // 진행은 `Scene::update` 이 카메라와 함께 부른다 — 단계는 자리만 정하고 `update( deltaTime )` 은 빈 함수다.
    SW_SCENE_SUBSYSTEM( SceneAudio, FlushEnd, makeSceneFrameAccess( SceneDataKindSet{}, SceneDataKindSet{}, SceneDataKindSet{ SceneDataKind::Structure, SceneDataKind::Transform } ) )

    SceneAudio::SceneAudio( GameObjectManager& manager )
        : _listener{ "SceneAudio.Listener", SW_DELEGATE_METHOD( RegistrationList<AudioListenerComponent>::ReleaseFunction, &SceneAudio::releaseListener, this ) }
        , _emitter{}
        , _zone{ "SceneAudio.ReverbZone", SW_DELEGATE_METHOD( RegistrationList<AudioReverbZoneComponent>::ReleaseFunction, &SceneAudio::releaseReverbZone, this ) }
        , _listEmitterSnapshot{}
        , _listEmitterMotion{}
        , _listZoneValue{}
        , _listZoneCurrent{}
        , _manager{ manager }
        , _pAudioSubsystem{ findSubsystemFrom<AudioSubsystem>( manager.getSceneEngineSubsystems()._pEngineSubsystems ) }
        , _pEngineOverride{ nullptr }
        , _pOcclusionOverride{ nullptr }
        , _primaryListenerPosition{}
        , _lastCameraPosition{}
        , _mutex{}
        , _occlusionChecksPerFrame{ 0 }
        , _occlusionCursor{ 0 }
        , _activeListenerMask{ 0 }
        , _bUpdateEnabled{ SW_TRUE }
    {
        const EngineConfig* const pConfig = findSubsystemFrom<EngineConfig>( manager.getSceneEngineSubsystems()._pEngineSubsystems );
        if ( pConfig != nullptr )
            _occlusionChecksPerFrame = pConfig->_audio._occlusionChecksPerFrame;
    }

    SceneAudio::~SceneAudio()
    {
        releaseEngineState();
    }

    void SceneAudio::releaseEngineState()
    {
        AudioEngine* const pEngine = findAudioEngine();
        ScopedLock<mutex>  lock{ _mutex };
        // 0 번(주 리스너)은 다음 씬의 첫 갱신이 덮는다 — 여기서 끄면 겹쳐 뜬 다음 씬의 소리가 한 블록 끊긴다.
        for ( uint32 listenerIndex = 1; pEngine != nullptr && listenerIndex < AudioEngine::kMaxListenerCount; ++listenerIndex )
        {
            if ( ( _activeListenerMask & ( 1u << listenerIndex ) ) != 0 )
                pEngine->setListener( listenerIndex, AudioListenerState{} );
        }
        for ( const ZoneValue& value : _listZoneValue )
        {
            if ( pEngine != nullptr )
                pEngine->setSnapshotIntensity( value._snapshot, 0.0f );
        }
        _activeListenerMask = 0;
        _listZoneValue.clear();
        _lastCameraPosition.reset();
    }

    void SceneAudio::onSceneCleared()
    {
        releaseEngineState();
    }

    AudioEngine* SceneAudio::findAudioEngine() const
    {
        if ( _pEngineOverride != nullptr )
            return _pEngineOverride;
        // 포인터는 생성할 때 받은 것이고, 오디오가 올라와 있는지는 부를 때마다 본다.
        if ( _pAudioSubsystem == nullptr || _pAudioSubsystem->isInitialized() == false )
            return nullptr;
        return &_pAudioSubsystem->getEngine();
    }

    Subscription SceneAudio::addListener( AudioListenerComponent* pListener )
    {
        const ScopedDeferredLog deferredLog;
        ScopedLock<mutex>       lock{ _mutex };
        return _listener.addSubscribed( pListener, "Audio listener" );
    }

    Subscription SceneAudio::addEmitter( AudioEmitterComponent* pEmitter )
    {
        const ScopedDeferredLog deferredLog;
        ScopedLock<mutex>       lock{ _mutex };
        if ( pEmitter == nullptr )
        {
            reportRegistrationRejected( RegistrationResult::NullItem, "Audio emitter", string_view{} );
            return {};
        }
        if ( _emitter.insert( pEmitter ).second == false )
            return {}; // 이미 올라 있다 — 먼저 받은 구독이 쥔다
        return _emitterSubscriptions.issue( pEmitter );
    }

    Subscription SceneAudio::addReverbZone( AudioReverbZoneComponent* pZone )
    {
        const ScopedDeferredLog deferredLog;
        ScopedLock<mutex>       lock{ _mutex };
        return _zone.addSubscribed( pZone, "Audio reverb zone" );
    }

    void SceneAudio::releaseListener( AudioListenerComponent* pListener )
    {
        ScopedLock<mutex> lock{ _mutex };
        (void)_listener.remove( pListener ); // 구독 하나가 등록 하나라 늘 있다
    }

    void SceneAudio::releaseReverbZone( AudioReverbZoneComponent* pZone )
    {
        ScopedLock<mutex> lock{ _mutex };
        (void)_zone.remove( pZone ); // 구독 하나가 등록 하나라 늘 있다
    }

    void SceneAudio::releaseEmitter( AudioEmitterComponent* pEmitter )
    {
        {
            ScopedLock<mutex> lock{ _mutex };
            if ( _emitter.erase( pEmitter ) == 0 )
                return;
        }
        AudioEngine* pEngine = findAudioEngine();
        if ( pEngine != nullptr )
            pEngine->removeEmitter( pEmitter->getEmitterID() );
    }

    void SceneAudio::applyPreviewPolicy( const ScenePreviewPolicy& policy )
    {
        _bUpdateEnabled = policy._bAudio;
        if ( _bUpdateEnabled == false )
            releaseEngineState();
    }

    void SceneAudio::update( const float32 deltaTime )
    {
        if ( _bUpdateEnabled == false )
            return;
        const ScenePhysics& physics = _manager.getSubsystem<ScenePhysics>();
        update( deltaTime, _manager.getFrameCamera(), physics.findScene3D(), physics.findScene2D() );
    }

    void SceneAudio::update( float32 deltaSeconds, const CameraComponent* pCamera, const IPhysicsQueryScene3D* pScene3D, const IPhysicsQueryScene2D* pScene2D )
    {
        AudioEngine* pEngine = findAudioEngine();
        if ( pEngine == nullptr )
            return;
        bool bListenerMoved = false;
        {
            ScopedLock<mutex> lock{ _mutex };
            const float3      previousListenerPosition = _primaryListenerPosition;
            updateListeners( *pEngine, deltaSeconds, pCamera );
            collectEmitters();
            bListenerMoved = previousListenerPosition != _primaryListenerPosition;
        }
        // 에미터 진행은 잠금 밖이다 — 병렬 합류를 기다리는 이 스레드가 다른 태스크를 돕다 등록(`addEmitter`, 비동기 씬 로드)으로 같은 잠금을 다시 잡으면 교착한다.
        // 사본의 에미터는 이 스레드(게임 스레드)의 파괴 단계에서만 사라지므로 사본을 도는 동안 살아 있다.
        updateEmitters( *pEngine, deltaSeconds, bListenerMoved );
        updateOcclusion( *pEngine, pScene3D, pScene2D );
        ScopedLock<mutex> lock{ _mutex };
        updateReverbZones( *pEngine );
    }

    void SceneAudio::onWorldOriginShifted( const float3& delta )
    {
        if ( AudioEngine* const pEngine = findAudioEngine() )
            pEngine->shiftWorldOrigin( delta );
        ScopedLock<mutex> lock{ _mutex };
        _primaryListenerPosition += delta;
        if ( _lastCameraPosition.has_value() )
            *_lastCameraPosition += delta;
    }

    float32 SceneAudio::findZoneIntensity( const hashed_string& snapshot ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        for ( const ZoneValue& value : _listZoneValue )
        {
            if ( value._snapshot == snapshot )
                return value._intensity;
        }
        return 0.0f;
    }

    void SceneAudio::updateListeners( AudioEngine& audioEngine, float32 deltaSeconds, const CameraComponent* pCamera )
    {
        // 리스너 — 리스너 컴포넌트가 있으면 그것들, 없으면 게임 카메라(직교면 2D 화면 팬).
        uint32 listenerMask = 0;
        bool   bHavePrimary = false;
        for ( AudioListenerComponent* pListener : _listener.getItems() )
        {
            if ( SceneAudioInternal::isUsable( pListener ) == false )
                continue;
            const uint32 listenerIndex = MathUtil::min( pListener->getListenerIndex(), AudioEngine::kMaxListenerCount - 1 );
            // 트랜스폼이 그대로면(월드 변경 번호) 자리 · 앞 · 위를 지난 상태에서 잇는다 — 자리가 같으니 속도는 0 이다. 모드 · 반폭은 세터가 바꾸므로 늘 넣는다.
            const uint32       version        = pListener->getWorldTransformVersion();
            const bool         bPoseUnchanged = pListener->_bHasLastState && version == pListener->_seenTransformVersion;
            AudioListenerState state          = pListener->makeListenerState( bPoseUnchanged );
            if ( bPoseUnchanged == false )
                state._velocity = SceneAudioInternal::computeVelocity( state._position, pListener->_bHasLastState ? optional<float3>{ pListener->_lastState._position } : nullopt, deltaSeconds );
            pListener->_lastState            = state;
            pListener->_seenTransformVersion = version;
            pListener->_bHasLastState        = true;
            audioEngine.setListener( listenerIndex, state );
            listenerMask |= 1u << listenerIndex;
            if ( listenerIndex == 0 || bHavePrimary == false )
                _primaryListenerPosition = state._position;
            bHavePrimary = true;
        }
        if ( listenerMask == 0 && pCamera != nullptr )
        {
            const float4x4     world = pCamera->getWorldMatrix();
            AudioListenerState state;
            state._position = world.getTranslation();
            state._forward  = float3::transformVector( float3( 0.0f, 0.0f, 1.0f ), world ).normalize();
            state._up       = float3::transformVector( float3( 0.0f, 1.0f, 0.0f ), world ).normalize();
            state._velocity = SceneAudioInternal::computeVelocity( state._position, _lastCameraPosition, deltaSeconds );
            if ( pCamera->isOrthographic() )
            {
                state._mode            = AudioSpatialMode::Screen2D;
                state._screenHalfWidth = pCamera->getOrthoHeight() * SceneAudioInternal::kAssumedAspect * 0.5f;
            }
            state._bActive           = SW_TRUE;
            _lastCameraPosition      = state._position;
            _primaryListenerPosition = state._position;
            audioEngine.setListener( 0, state );
            listenerMask = 1u;
        }
        // 지난 프레임에 켰는데 이번에 없는 칸은 끈다.
        for ( uint32 listenerIndex = 0; listenerIndex < AudioEngine::kMaxListenerCount; ++listenerIndex )
        {
            const uint32 bit = 1u << listenerIndex;
            if ( ( _activeListenerMask & bit ) != 0 && ( listenerMask & bit ) == 0 )
                audioEngine.setListener( listenerIndex, AudioListenerState{} );
        }
        _activeListenerMask = listenerMask;
    }

    void SceneAudio::collectEmitters()
    {
        _listEmitterSnapshot.clear();
        for ( AudioEmitterComponent* pEmitter : _emitter )
        {
            if ( SceneAudioInternal::isUsable( pEmitter ) )
                _listEmitterSnapshot.push_back( pEmitter );
        }
    }

    void SceneAudio::updateEmitters( AudioEngine& audioEngine, float32 deltaSeconds, bool bListenerMoved )
    {
        // 에미터 자리 · 속도. 자리는 에미터의 트랜스폼과(넓은 소리는) 리스너 자리를 따르므로, 둘 다 그대로면(월드 변경 번호 · 리스너 자리) 지난 자리를
        // 그대로 쓰고 속도는 0 이다. 엔진에는 그래도 넣는다 — 엔진이 지난 값을 들고 있는지에 기대지 않는다.
        // 자리 구하기는 에미터마다 독립이라 워커로 나눈다(언리얼 오디오 컴포넌트 갱신의 ParallelFor 자리). 스레드 안전: 본문은 리스너 자리(이 객체, 병렬 동안 바뀌지 않는다) ·
        // 에미터의 월드 캐시를 읽고, 쓰는 것은 그 에미터 자신의 지난 상태 칸과 결과 칸 `_listEmitterMotion[index]` 뿐이다(사본에 같은 에미터는 한 번).
        // 엔진에 넣기(`setEmitter` — 엔진의 명령 줄)는 이 스레드가 사본 순서로 한다. 월드 캐시는 `FlushEnd`(펼친 뒤)라 읽기만이다.
        const uint32 count = static_cast<uint32>( _listEmitterSnapshot.size() );
        _listEmitterMotion.resize( count );
        AudioEmitterComponent* const* const arrEmitter = _listEmitterSnapshot.data();
        const float3                        listener   = _primaryListenerPosition;
        engine::runParallelIntoByCost<EmitterMotion>( _listEmitterMotion, [arrEmitter, listener, deltaSeconds, bListenerMoved]( uint32 index, EmitterMotion& outMotion )
        {
            AudioEmitterComponent& emitter  = *arrEmitter[index];
            const uint32           version  = emitter.getWorldTransformVersion();
            const bool             bUnmoved = emitter._lastAudioPosition.has_value() && bListenerMoved == false && version == emitter._seenTransformVersion;
            outMotion._position             = bUnmoved ? *emitter._lastAudioPosition : emitter.computeAudioPosition( listener );
            outMotion._velocity             = bUnmoved ? float3{} : SceneAudioInternal::computeVelocity( outMotion._position, emitter._lastAudioPosition, deltaSeconds );
            emitter._lastAudioPosition      = outMotion._position;
            emitter._seenTransformVersion   = version;
        } );
        for ( uint32 index = 0; index < count; ++index )
            audioEngine.setEmitter( arrEmitter[index]->getEmitterID(), _listEmitterMotion[index]._position, _listEmitterMotion[index]._velocity );
    }

    void SceneAudio::updateOcclusion( AudioEngine& audioEngine, const IPhysicsQueryScene3D* pScene3D, const IPhysicsQueryScene2D* pScene2D )
    {
        // 가림 — 프레임마다 몇 개씩 돌아가며 측정한다(엔진이 부드럽게 따라간다).
        PhysicsAudioOcclusionQuery  physicsQuery;
        const IAudioOcclusionQuery* pQuery = _pOcclusionOverride;
        if ( pQuery == nullptr && pScene3D != nullptr )
        {
            physicsQuery.setScene( pScene3D, MathUtil::kMaxUInt32 );
            pQuery = &physicsQuery;
        }
        else if ( pQuery == nullptr && pScene2D != nullptr )
        {
            physicsQuery.setScene( pScene2D, MathUtil::kMaxUInt32 );
            pQuery = &physicsQuery;
        }
        const uint32 emitterCount = static_cast<uint32>( _listEmitterSnapshot.size() );
        if ( pQuery != nullptr && emitterCount > 0 )
        {
            if ( _occlusionChecksPerFrame == 0 )
                SW_LOG_ERROR_ONCE_ID( "SceneAudio.NoOcclusionBudget", "Scene audio: the scene has no EngineConfig (_audio._occlusionChecksPerFrame) - sound occlusion is not measured" );
            const uint32 checkCount = MathUtil::min( emitterCount, _occlusionChecksPerFrame );
            for ( uint32 validateIndex = 0; validateIndex < checkCount; ++validateIndex )
            {
                AudioEmitterComponent* pEmitter = _listEmitterSnapshot[( _occlusionCursor + validateIndex ) % emitterCount];
                if ( pEmitter->usesOcclusion() == false )
                    continue;
                audioEngine.setEmitterPropagation( pEmitter->getEmitterID(), pQuery->computePropagation( _primaryListenerPosition, pEmitter->_lastAudioPosition.value_or( float3{} ) ) );
            }
            _occlusionCursor = ( _occlusionCursor + checkCount ) % emitterCount;
        }
    }

    void SceneAudio::updateReverbZones( AudioEngine& audioEngine )
    {
        // 리버브 존 — 스냅샷마다 가장 큰 세기. 리스너처럼 프레임마다 넣는다(바뀐 것만 넣으면 겹쳐 뜬 앞 씬이 내려가며 0 으로 내린 값이 되살아나지 않는다). 이번에 거는 존이 없는 스냅샷은 0 으로 내린다.
        _listZoneCurrent.clear();
        for ( AudioReverbZoneComponent* pZone : _zone.getItems() )
        {
            if ( SceneAudioInternal::isUsable( pZone ) == false || pZone->getSnapshot().empty() )
                continue;
            const float32 intensity = pZone->computeIntensity( _primaryListenerPosition );
            bool          bFound    = false;
            for ( ZoneValue& value : _listZoneCurrent )
            {
                if ( value._snapshot == pZone->getSnapshot() )
                {
                    value._intensity = MathUtil::max( value._intensity, intensity );
                    bFound           = true;
                }
            }
            if ( bFound == false )
                _listZoneCurrent.push_back( ZoneValue{ pZone->getSnapshot(), intensity } );
        }
        for ( const ZoneValue& current : _listZoneCurrent )
        {
            audioEngine.setSnapshotIntensity( current._snapshot, current._intensity );
        }
        for ( const ZoneValue& previous : _listZoneValue )
        {
            bool bStillZoned = false;
            for ( const ZoneValue& current : _listZoneCurrent )
            {
                bStillZoned = bStillZoned || current._snapshot == previous._snapshot;
            }
            if ( bStillZoned == false )
                audioEngine.setSnapshotIntensity( previous._snapshot, 0.0f );
        }
        _listZoneValue.swap( _listZoneCurrent );
    }
} // namespace sw
