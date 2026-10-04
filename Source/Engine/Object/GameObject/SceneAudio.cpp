#include "pch.h"

#include "Engine/Object/GameObject/SceneAudio.h"

#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/IAudioSystem.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/Audio/AudioEmitterComponent.h"
#include "Engine/Object/Component/Audio/AudioListenerComponent.h"
#include "Engine/Object/Component/Audio/AudioReverbZoneComponent.h"
#include "Engine/Object/Component/Audio/PhysicsAudioOcclusionQuery.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"

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
                if ( pComponent == nullptr || pComponent->isPendingDestroy() || pComponent->isActive() == false )
                    return false;
                const GameObject* pOwner = pComponent->getOwner();
                return pOwner != nullptr && pOwner->isPendingDestroy() == false;
            }

            /** @brief 지난 자리와 지금 자리로 속도를 잽니다(한계로 묶음). 첫 프레임은 0 입니다. */
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
        };
    } // namespace
} // namespace sw

namespace sw
{
    SceneAudio::SceneAudio()
        : _listener{}
        , _emitter{}
        , _zone{}
        , _listEmitterSnapshot{}
        , _listZoneValue{}
        , _listZoneCurrent{}
        , _pEngineOverride{ nullptr }
        , _pOcclusionOverride{ nullptr }
        , _primaryListenerPosition{}
        , _lastCameraPosition{}
        , _mutex{}
        , _occlusionCursor{ 0 }
        , _activeListenerMask{ 0 }
        , _bHasLastCameraPosition{ false }
    {
    }

    SceneAudio::~SceneAudio() = default;

    AudioEngine* SceneAudio::findAudioEngine() const
    {
        if ( _pEngineOverride != nullptr )
            return _pEngineOverride;
        if ( engine::areEngineServicesBound() == false )
            return nullptr;
        IAudioSystem& audioSystem = engine::getAudioSystem();
        return audioSystem.isInitialized() ? &audioSystem.getEngine() : nullptr;
    }

    void SceneAudio::addListener( AudioListenerComponent* pListener )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        (void)_listener.add( pListener );
    }

    void SceneAudio::removeListener( AudioListenerComponent* pListener )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        (void)_listener.remove( pListener );
    }

    void SceneAudio::addEmitter( AudioEmitterComponent* pEmitter )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        (void)_emitter.add( pEmitter );
    }

    void SceneAudio::removeEmitter( AudioEmitterComponent* pEmitter )
    {
        {
            std::scoped_lock<mutex> lock{ _mutex };
            (void)_emitter.remove( pEmitter );
        }
        AudioEngine* pEngine = findAudioEngine();
        if ( pEngine != nullptr && pEmitter != nullptr )
            pEngine->removeEmitter( pEmitter->getEmitterId() );
    }

    void SceneAudio::addReverbZone( AudioReverbZoneComponent* pZone )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        (void)_zone.add( pZone );
    }

    void SceneAudio::removeReverbZone( AudioReverbZoneComponent* pZone )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        (void)_zone.remove( pZone );
    }

    void SceneAudio::update( float32 deltaSeconds, const CameraComponent* pCamera, const IPhysicsScene3D* pScene3D )
    {
        AudioEngine* pEngine = findAudioEngine();
        if ( pEngine == nullptr )
            return;
        std::scoped_lock<mutex> lock{ _mutex };

        // 리스너 — 리스너 컴포넌트가 있으면 그것들, 없으면 게임 카메라(직교면 2D 화면 팬).
        uint32 listenerMask = 0;
        bool   bHavePrimary = false;
        for ( AudioListenerComponent* pListener : _listener.getItems() )
        {
            if ( SceneAudioInternal::isUsable( pListener ) == false )
                continue;
            const uint32       listenerIndex = MathUtil::min( pListener->getListenerIndex(), AudioEngine::kMaxListenerCount - 1 );
            AudioListenerState state         = pListener->makeListenerState();
            state._velocity                  = SceneAudioInternal::computeVelocity( state._position, pListener->_lastPosition, pListener->_bHasLastPosition, deltaSeconds );
            pListener->_lastPosition         = state._position;
            pListener->_bHasLastPosition     = true;
            pEngine->setListener( listenerIndex, state );
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
            state._bActive           = true;
            _lastCameraPosition      = state._position;
            _bHasLastCameraPosition  = true;
            _primaryListenerPosition = state._position;
            pEngine->setListener( 0, state );
            listenerMask = 1u;
        }
        // 지난 프레임에 켰는데 이번에 없는 칸은 끈다.
        for ( uint32 listenerIndex = 0; listenerIndex < AudioEngine::kMaxListenerCount; ++listenerIndex )
        {
            const uint32 bit = 1u << listenerIndex;
            if ( ( _activeListenerMask & bit ) != 0 && ( listenerMask & bit ) == 0 )
                pEngine->setListener( listenerIndex, AudioListenerState{} );
        }
        _activeListenerMask = listenerMask;

        // 에미터 자리 · 속도.
        _listEmitterSnapshot.clear();
        for ( AudioEmitterComponent* pEmitter : _emitter.getItems() )
        {
            if ( SceneAudioInternal::isUsable( pEmitter ) == false )
                continue;
            const float3 position            = pEmitter->computeAudioPosition( _primaryListenerPosition );
            const float3 velocity            = SceneAudioInternal::computeVelocity( position, pEmitter->_lastAudioPosition, pEmitter->_bHasLastAudioPosition, deltaSeconds );
            pEmitter->_lastAudioPosition     = position;
            pEmitter->_bHasLastAudioPosition = true;
            pEngine->setEmitter( pEmitter->getEmitterId(), position, velocity );
            _listEmitterSnapshot.push_back( pEmitter );
        }

        // 가림 — 프레임마다 몇 개씩 돌아가며 잰다(엔진이 부드럽게 따라간다).
        PhysicsAudioOcclusionQuery  physicsQuery;
        const IAudioOcclusionQuery* pQuery = _pOcclusionOverride;
        if ( pQuery == nullptr && pScene3D != nullptr )
        {
            physicsQuery.setScene( pScene3D, MathUtil::MaxUInt32 );
            pQuery = &physicsQuery;
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
                pEngine->setEmitterOcclusion( pEmitter->getEmitterId(), pQuery->computeOcclusion( _primaryListenerPosition, pEmitter->_lastAudioPosition ) );
            }
            _occlusionCursor = ( _occlusionCursor + checkCount ) % emitterCount;
        }

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
                pEngine->setSnapshotIntensity( current._snapshot, current._intensity );
        }
        for ( const ZoneValue& previous : _listZoneValue )
        {
            bool bStillZoned = false;
            for ( const ZoneValue& current : _listZoneCurrent )
                bStillZoned = bStillZoned || current._snapshot == previous._snapshot;
            if ( bStillZoned == false )
                pEngine->setSnapshotIntensity( previous._snapshot, 0.0f );
        }
        _listZoneValue.swap( _listZoneCurrent );
    }
} // namespace sw
