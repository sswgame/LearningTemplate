#include "pch.h"

#include "Engine/Audio/Scene/SceneAudio.h"

#include "Core/Subsystem/SubsystemScope.h"

#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/Component/AudioEmitterComponent.h"
#include "Engine/Audio/Component/AudioListenerComponent.h"
#include "Engine/Audio/Component/AudioReverbZoneComponent.h"
#include "Engine/Audio/IAudioService.h"
#include "Engine/Audio/Scene/Physics2DAudioOcclusionQuery.h"
#include "Engine/Audio/Scene/PhysicsAudioOcclusionQuery.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Foundation/EngineParallel.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Service/SceneEngineServices.h"
#include "Engine/Object/GameObject/Service/ScenePreviewPolicy.h"
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
            static float3 computeVelocity( const float3& position, const float3& lastPosition, bool bHasLast, float32 deltaSeconds )
            {
                if ( bHasLast == false || deltaSeconds <= 1e-5f )
                    return float3{};
                float3        velocity = ( position - lastPosition ) / deltaSeconds;
                const float32 speed    = velocity.getLength();
                if ( speed > kMaxSpeed )
                    velocity *= kMaxSpeed / speed;
                return velocity;
            }

            /** @brief 감시 값 `Audio.ReverbIntensity` 입니다. 리버브 존들이 스냅샷 `key` 에 건 세기입니다. */
            [[nodiscard]] static bool readReverbIntensity( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                if ( query._pManager == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( query._pManager->getSubsystem<SceneAudio>().findZoneIntensity( hashed_string{ query._key } ) ) );
                return true;
            }

            /** @brief 감시 값 `Audio.EmitterDistance` 입니다. 대상 에미터가 지난 프레임에 엔진에 넣은 소리 자리와 리스너 사이 거리(m)입니다 — 영역 앰비언트는 안에 서면 0 입니다. */
            [[nodiscard]] static bool readEmitterDistance( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GameObject*            pTarget  = query.findTargetObject();
                const AudioEmitterComponent* pEmitter = pTarget != nullptr ? pTarget->getComponent<AudioEmitterComponent>() : nullptr;
                if ( pEmitter == nullptr || pEmitter->_bHasLastAudioPosition == false )
                {
                    outValue.setText( pTarget == nullptr ? query.getTargetProblem() : query.describeMissing( "audio emitter updated by the scene audio" ) );
                    return false;
                }
                const float3& listener = query._pManager->getSubsystem<SceneAudio>().getPrimaryListenerPosition();
                outValue.setNumber( static_cast<float64>( float3::getDistance( pEmitter->_lastAudioPosition, listener ) ) );
                return true;
            }

            /** @brief 감시 값 `Audio.InstanceCount` 입니다. 오디오 엔진에 살아 있는 이벤트 인스턴스 수입니다. */
            [[nodiscard]] static bool readInstanceCount( const GameObjectManager* pManager, float64& outValue )
            {
                const AudioEngine* pEngine = pManager != nullptr ? pManager->getSubsystem<SceneAudio>().findAudioEngine() : nullptr;
                if ( pEngine == nullptr )
                    return false;
                outValue = static_cast<float64>( pEngine->getStats()._instanceCount );
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_AUTOMATION_QUERY_WATCH( audioReverbIntensity, "Audio.ReverbIntensity", "Intensity the reverb zones put on the mix snapshot named key (0..1)", &SceneAudioInternal::readReverbIntensity, false, true );
    SW_AUTOMATION_QUERY_WATCH( audioEmitterDistance, "Audio.EmitterDistance", "Distance from the listener to where the target emitter sounds (m); 0 inside an ambient volume", &SceneAudioInternal::readEmitterDistance, true,
                               false );
    // 오디오 스레드가 명령을 옮겨야 값이 바뀐다 — 벽시계 시한으로 기다린다.
    SW_AUTOMATION_WALL_WATCH( audioInstanceCount, "Audio.InstanceCount", "Sound event instances alive in the audio engine", &SceneAudioInternal::readInstanceCount );
} // namespace sw

namespace sw
{
    // 진행은 `Scene::update` 이 카메라와 함께 부른다 — 단계는 자리만 정하고 `update( deltaTime )` 은 빈 함수다.
    SW_SCENE_SERVICE( SceneAudio, FlushEnd, makeSceneFrameAccess( SceneDataKindSet{}, SceneDataKindSet{}, SceneDataKindSet{ SceneDataKind::Structure, SceneDataKind::Transform } ) )

    SceneAudio::SceneAudio( GameObjectManager& manager )
        : _listener{ "SceneAudio.Listener", RegistrationList<AudioListenerComponent>::ReleaseFunction::create<&SceneAudio::releaseListener>( this ) }
        , _emitter{}
        , _zone{ "SceneAudio.ReverbZone", RegistrationList<AudioReverbZoneComponent>::ReleaseFunction::create<&SceneAudio::releaseReverbZone>( this ) }
        , _listEmitterSnapshot{}
        , _listEmitterMotion{}
        , _listZoneValue{}
        , _listZoneCurrent{}
        , _manager{ manager }
        , _pAudioService{ locateSubsystem<IAudioService>( manager.getSceneEngineServices()._pEngineSubsystems ) }
        , _pEngineOverride{ nullptr }
        , _pOcclusionOverride{ nullptr }
        , _primaryListenerPosition{}
        , _lastCameraPosition{}
        , _mutex{}
        , _occlusionCursor{ 0 }
        , _activeListenerMask{ 0 }
        , _bHasLastCameraPosition{ SW_FALSE }
        , _bUpdateEnabled{ SW_TRUE }
    {
    }

    SceneAudio::~SceneAudio() = default;

    AudioEngine* SceneAudio::findAudioEngine() const
    {
        if ( _pEngineOverride != nullptr )
            return _pEngineOverride;
        // 포인터는 생성할 때 받은 것이고, 오디오가 올라와 있는지는 부를 때마다 본다.
        if ( _pAudioService == nullptr || _pAudioService->isInitialized() == false )
            return nullptr;
        return &_pAudioService->getEngine();
    }

    Subscription SceneAudio::addListener( AudioListenerComponent* pListener )
    {
        ScopedLock<mutex> lock{ _mutex };
        return _listener.addSubscribed( pListener, "Audio listener" );
    }

    Subscription SceneAudio::addEmitter( AudioEmitterComponent* pEmitter )
    {
        ScopedLock<mutex> lock{ _mutex };
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
        ScopedLock<mutex> lock{ _mutex };
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
                state._velocity = SceneAudioInternal::computeVelocity( state._position, pListener->_lastState._position, pListener->_bHasLastState, deltaSeconds );
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
            state._velocity = SceneAudioInternal::computeVelocity( state._position, _lastCameraPosition, _bHasLastCameraPosition, deltaSeconds );
            if ( pCamera->isOrthographic() )
            {
                state._mode            = AudioSpatialMode::Screen2D;
                state._screenHalfWidth = pCamera->getOrthoHeight() * SceneAudioInternal::kAssumedAspect * 0.5f;
            }
            state._bActive           = SW_TRUE;
            _lastCameraPosition      = state._position;
            _bHasLastCameraPosition  = SW_TRUE;
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
        EmitterMotion* const                arrMotion  = _listEmitterMotion.data();
        const float3                        listener   = _primaryListenerPosition;
        auto                                moveRange  = [arrEmitter, arrMotion, listener, deltaSeconds, bListenerMoved]( uint32 start, uint32 end )
        {
            for ( uint32 index = start; index < end; ++index )
            {
                AudioEmitterComponent& emitter  = *arrEmitter[index];
                const uint32           version  = emitter.getWorldTransformVersion();
                const bool             bUnmoved = emitter._bHasLastAudioPosition && bListenerMoved == false && version == emitter._seenTransformVersion;
                EmitterMotion&         motion   = arrMotion[index];
                motion._position                = bUnmoved ? emitter._lastAudioPosition : emitter.computeAudioPosition( listener );
                motion._velocity                = bUnmoved ? float3{} : SceneAudioInternal::computeVelocity( motion._position, emitter._lastAudioPosition, emitter._bHasLastAudioPosition, deltaSeconds );
                emitter._lastAudioPosition      = motion._position;
                emitter._seenTransformVersion   = version;
                emitter._bHasLastAudioPosition  = true;
            }
        };
        engine::runParallelByCost( count, SW_DELEGATE_LAMBDA( ParallelBlockDelegate, moveRange ) );
        for ( uint32 index = 0; index < count; ++index )
            audioEngine.setEmitter( arrEmitter[index]->getEmitterID(), arrMotion[index]._position, arrMotion[index]._velocity );
    }

    void SceneAudio::updateOcclusion( AudioEngine& audioEngine, const IPhysicsQueryScene3D* pScene3D, const IPhysicsQueryScene2D* pScene2D )
    {
        // 가림 — 프레임마다 몇 개씩 돌아가며 측정한다(엔진이 부드럽게 따라간다).
        PhysicsAudioOcclusionQuery   physicsQuery;
        Physics2DAudioOcclusionQuery physics2DQuery;
        const IAudioOcclusionQuery*  pQuery = _pOcclusionOverride;
        if ( pQuery == nullptr && pScene3D != nullptr )
        {
            physicsQuery.setScene( pScene3D, MathUtil::kMaxUInt32 );
            pQuery = &physicsQuery;
        }
        else if ( pQuery == nullptr && pScene2D != nullptr )
        {
            physics2DQuery.setScene( pScene2D, MathUtil::kMaxUInt32 );
            pQuery = &physics2DQuery;
        }
        const uint32 emitterCount = static_cast<uint32>( _listEmitterSnapshot.size() );
        if ( pQuery != nullptr && emitterCount > 0 )
        {
            const uint32 checkCount = MathUtil::min( emitterCount, kOcclusionChecksPerFrame );
            for ( uint32 checkIndex = 0; checkIndex < checkCount; ++checkIndex )
            {
                AudioEmitterComponent* pEmitter = _listEmitterSnapshot[( _occlusionCursor + checkIndex ) % emitterCount];
                if ( pEmitter->usesOcclusion() == false )
                    continue;
                audioEngine.setEmitterPropagation( pEmitter->getEmitterID(), pQuery->computePropagation( _primaryListenerPosition, pEmitter->_lastAudioPosition ) );
            }
            _occlusionCursor = ( _occlusionCursor + checkCount ) % emitterCount;
        }
    }

    void SceneAudio::updateReverbZones( AudioEngine& audioEngine )
    {
        // 리버브 존 — 스냅샷마다 가장 큰 세기. 바뀐 것만 넣고, 이번에 거는 존이 없는 스냅샷은 0 으로 내린다.
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
            const ZoneValue* pPrevious = nullptr;
            for ( const ZoneValue& previous : _listZoneValue )
            {
                if ( previous._snapshot == current._snapshot )
                    pPrevious = &previous;
            }
            if ( pPrevious == nullptr || MathUtil::abs( pPrevious->_intensity - current._intensity ) > 1e-4f )
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
