#include "pch.h"

#include "Engine/Environment/Surface/SurfaceFootprintSystem.h"

#include "Core/Foundation/BuildInfo.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Environment/Surface/SurfaceDeformSystem.h"
#include "Engine/Environment/Surface/SurfaceKind.h"
#include "Engine/Environment/Surface/SurfaceSample.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Service/SceneEngineServices.h"
#include "Engine/Presentation/ISoundPlayer.h"

namespace sw
{
    namespace
    {
        struct SurfaceFootprintSystemInternal
        {
            static const SurfaceFootprintSystem* findSystem( const AutomationWatchQuery& query )
            {
                return query._pManager != nullptr ? &query._pManager->getSubsystem<SurfaceFootprintSystem>() : nullptr;
            }

            /** @brief 감시 값 `Surface.FootprintPlaced` 입니다. 이 씬에서 찍은 발자국 수(누적)입니다. */
            [[nodiscard]] static bool readFootprintPlaced( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SurfaceFootprintSystem* pSystem = findSystem( query );
                if ( pSystem == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pSystem->getPlacedCount() ) );
                return true;
            }

            /** @brief 감시 값 `Surface.WetPrintPlaced` 입니다. 이 씬에서 찍은 젖은 자국 수(누적)입니다. */
            [[nodiscard]] static bool readWetPrintPlaced( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SurfaceFootprintSystem* pSystem = findSystem( query );
                if ( pSystem == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pSystem->getWetPrintCount() ) );
                return true;
            }

            /** @brief 감시 값이 읽을 기록입니다 — @p bTarget 이면 대상 오브젝트의 기록, 아니면 씬 전체입니다. 없으면 @p outValue 에 까닭을 적고 nullptr 입니다. */
            static const SurfaceFootstepRecord* findRecord( const AutomationWatchQuery& query, bool bTarget, AutomationWatchValue& outValue )
            {
                const SurfaceFootprintSystem* pSystem = findSystem( query );
                if ( pSystem == nullptr )
                    return nullptr;
                if ( bTarget == false )
                    return &pSystem->getSceneRecord();
                const GameObject* pTarget = query.findTargetObject();
                if ( pTarget == nullptr )
                {
                    outValue.setText( query.getTargetProblem() );
                    return nullptr;
                }
                const SurfaceFootstepRecord* pRecord = pSystem->findObjectRecord( pTarget->getObjectID() );
                if ( pRecord == nullptr )
                    outValue.setText( "the target has not stepped yet" );
                return pRecord;
            }

            /** @brief 기록이 있고 마지막 발걸음이 있으면 그 기록입니다. 없으면 까닭을 적고 nullptr 입니다. */
            static const SurfaceFootstepRecord* findLastStep( const AutomationWatchQuery& query, bool bTarget, AutomationWatchValue& outValue )
            {
                const SurfaceFootstepRecord* pRecord = findRecord( query, bTarget, outValue );
                if ( pRecord != nullptr && pRecord->_lastKind.empty() )
                {
                    outValue.setText( "no footstep yet" );
                    return nullptr;
                }
                return pRecord;
            }

            /** @brief 종류 `key` 를 밟은 발걸음 수(누적)입니다. */
            template <bool bTarget>
            [[nodiscard]] static bool readCount( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SurfaceFootstepRecord* pRecord = findRecord( query, bTarget, outValue );
                // 대상이 아직 걷지 않았으면 0 이다 — 조건 대기가 첫 걸음을 기다릴 수 있게.
                const bool bNotYet = bTarget && pRecord == nullptr && query.findTargetObject() != nullptr;
                if ( pRecord == nullptr && bNotYet == false )
                    return false;
                outValue.setNumber( static_cast<float64>( pRecord != nullptr ? pRecord->getCount( hashed_string( query._key ) ) : 0u ) );
                return true;
            }

            /** @brief 마지막 발걸음의 표면 종류 이름입니다. */
            template <bool bTarget>
            [[nodiscard]] static bool readLastKind( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SurfaceFootstepRecord* pRecord = findLastStep( query, bTarget, outValue );
                if ( pRecord == nullptr )
                    return false;
                outValue.setText( pRecord->_lastKind.c_str() );
                return true;
            }

            /** @brief 마지막 발걸음이 고른 반응("dry" · "wet")입니다. */
            template <bool bTarget>
            [[nodiscard]] static bool readLastState( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SurfaceFootstepRecord* pRecord = findLastStep( query, bTarget, outValue );
                if ( pRecord == nullptr )
                    return false;
                outValue.setText( pRecord->isLastWet() ? "wet" : "dry" );
                return true;
            }

            /** @brief 마지막 발걸음 반응의 소리 이벤트("none" = 조용함)입니다. */
            template <bool bTarget>
            [[nodiscard]] static bool readLastSound( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SurfaceFootstepRecord* pRecord = findLastStep( query, bTarget, outValue );
                if ( pRecord == nullptr )
                    return false;
                outValue.setText( pRecord->_lastSound.empty() ? "none" : pRecord->_lastSound.c_str() );
                return true;
            }

            /** @brief 마지막 발걸음 반응의 효과 경로("none" = 없음)입니다. */
            template <bool bTarget>
            [[nodiscard]] static bool readLastEffect( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SurfaceFootstepRecord* pRecord = findLastStep( query, bTarget, outValue );
                if ( pRecord == nullptr )
                    return false;
                outValue.setText( pRecord->_lastEffect.empty() ? "none" : pRecord->_lastEffect.c_str() );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( surfaceFootprintPlaced, "Surface.FootprintPlaced", "Footprints stamped on the surface deform layer in this scene (cumulative)",
                               &SurfaceFootprintSystemInternal::readFootprintPlaced, false, false );
    SW_AUTOMATION_QUERY_WATCH( surfaceWetPrintPlaced, "Surface.WetPrintPlaced", "Wet prints left by wet feet on dry ground in this scene (cumulative)",
                               &SurfaceFootprintSystemInternal::readWetPrintPlaced, false, false );
    SW_AUTOMATION_QUERY_WATCH( surfaceFootstepCount, "Surface.FootstepCount", "Footsteps on the surface kind named key (cumulative)", &SurfaceFootprintSystemInternal::readCount<false>,
                               false, true );
    SW_AUTOMATION_QUERY_WATCH( surfaceLastFootstep, "Surface.LastFootstep", "Surface kind of the last footstep", &SurfaceFootprintSystemInternal::readLastKind<false>, false, false );
    SW_AUTOMATION_QUERY_WATCH( surfaceLastFootstepState, "Surface.LastFootstepState", "Response the last footstep took: dry or wet", &SurfaceFootprintSystemInternal::readLastState<false>,
                               false, false );
    SW_AUTOMATION_QUERY_WATCH( surfaceLastFootstepSound, "Surface.LastFootstepSound", "Audio event of the last footstep response (none: silent)",
                               &SurfaceFootprintSystemInternal::readLastSound<false>, false, false );
    SW_AUTOMATION_QUERY_WATCH( surfaceLastFootstepEffect, "Surface.LastFootstepEffect", "Effect of the last footstep response (none: no effect)",
                               &SurfaceFootprintSystemInternal::readLastEffect<false>, false, false );
    SW_AUTOMATION_QUERY_WATCH( surfaceObjectFootstepCount, "Surface.ObjectFootstepCount", "Footsteps of the target object on the surface kind named key (cumulative; 0 before its first step)",
                               &SurfaceFootprintSystemInternal::readCount<true>, true, true );
    SW_AUTOMATION_QUERY_WATCH( surfaceObjectLastFootstep, "Surface.ObjectLastFootstep", "Surface kind of the target object's last footstep", &SurfaceFootprintSystemInternal::readLastKind<true>,
                               true, false );
    SW_AUTOMATION_QUERY_WATCH( surfaceObjectLastFootstepState, "Surface.ObjectLastFootstepState", "Response the target object's last footstep took: dry or wet",
                               &SurfaceFootprintSystemInternal::readLastState<true>, true, false );
    SW_AUTOMATION_QUERY_WATCH( surfaceObjectLastFootstepSound, "Surface.ObjectLastFootstepSound", "Audio event of the target object's last footstep response (none: silent)",
                               &SurfaceFootprintSystemInternal::readLastSound<true>, true, false );
    SW_AUTOMATION_QUERY_WATCH( surfaceObjectLastFootstepEffect, "Surface.ObjectLastFootstepEffect", "Effect of the target object's last footstep response (none: no effect)",
                               &SurfaceFootprintSystemInternal::readLastEffect<true>, true, false );
} // namespace sw

namespace sw
{
    SurfaceFootstepRecord::SurfaceFootstepRecord()
        : _bLastWet{ SW_FALSE }
    {
    }

    uint32 SurfaceFootstepRecord::getCount( const hashed_string& kind ) const
    {
        const auto found = _mapCount.find( kind );
        return found != _mapCount.end() ? found->second : 0;
    }

    void SurfaceFootstepRecord::note( const hashed_string& kind, const SurfaceStepResponse& step, bool bWet )
    {
        ++_mapCount[kind];
        _lastKind   = kind;
        _lastSound  = step._sound;
        _lastEffect = step._effect.isSet() ? step._effect.get() : string{};
        _bLastWet   = bWet ? SW_TRUE : SW_FALSE;
    }

    SW_SCENE_SERVICE( SurfaceFootprintSystem, FlushEnd, makeSceneFrameAccess( SceneDataKindSet{}, SceneDataKindSet{}, SceneDataKindSet{ SceneDataKind::Structure, SceneDataKind::Transform } ) )

    SurfaceFootprintSystem::SurfaceFootprintSystem( GameObjectManager& manager )
        : _pManager{ &manager }
        , _sceneRecord{}
        , _mapObjectRecord{}
        , _mapWetFeet{}
        , _placedCount{ 0 }
        , _wetPrintCount{ 0 }
    {
    }

    void SurfaceFootprintSystem::onSceneCleared()
    {
        _sceneRecord = SurfaceFootstepRecord{};
        _mapObjectRecord.clear();
        _mapWetFeet.clear();
        _placedCount   = 0;
        _wetPrintCount = 0;
    }

    const SurfaceStepResponse* SurfaceFootprintSystem::addFootstep( const SurfaceSample& sample, float32 yaw, uint64 ownerObjectID )
    {
        SW_ASSERT( sample._pKind != nullptr ); // 질의가 성공한 표본만 온다(`SurfaceSample::resolveHit`)
        const SurfaceKindDef&                    kind   = *sample._pKind;
        const shared_ptr<const SurfaceKindTable> pTable = SurfaceKindTable::findDefault();
        if ( pTable == nullptr )
            return nullptr; // 표를 읽지 못한 까닭은 findDefault 가 남겼다
        const bool                 bWet = kind.isWetStep( sample._wetness, pTable->_wetThreshold );
        const SurfaceStepResponse& step = kind.selectStep( sample._wetness, pTable->_wetThreshold );
        _sceneRecord.note( kind._name, step, bWet );
        if ( ownerObjectID != 0 )
            _mapObjectRecord[ownerObjectID].note( kind._name, step, bWet );

        SurfaceDeformSystem& deform    = _pManager->getSubsystem<SurfaceDeformSystem>();
        const uint32         stampedBy = deform.getStampCount();
        deform.stamp( step._footprint, sample._point, yaw );
        // 찍지 못한 발걸음(전용 서버 · 빈 도장)은 세지 않는다 — 감시 값이 보이는 자국 수와 같다.
        if ( deform.getStampCount() != stampedBy )
            ++_placedCount;
        // 젖은 땅(비)에서는 젖은 자국이 보이지 않는다 — 젖은 발은 마른 땅에서만 자국을 남긴다.
        updateWetFeet( *pTable, step, sample._wetness >= pTable->_wetThreshold, sample._point, yaw, ownerObjectID );
        if ( build::kWithClientCode && step._sound.empty() == false )
            playSound( step._sound, sample._point ); // 소리는 들리는 것뿐이다
        return &step;                                // 반응은 공유 캐시의 표 안에 있다 — 표는 표본(sample._pKind)이 함께 쥐므로 부르는 쪽이 표본을 쥔 동안 살아 있다
    }

    const SurfaceFootstepRecord* SurfaceFootprintSystem::findObjectRecord( uint64 objectID ) const
    {
        const auto found = _mapObjectRecord.find( objectID );
        return found != _mapObjectRecord.end() ? &found->second : nullptr;
    }

    void SurfaceFootprintSystem::updateWetFeet( const SurfaceKindTable& table, const SurfaceStepResponse& step, bool bGroundWet, const float3& point, float32 yaw, uint64 ownerObjectID )
    {
        if ( ownerObjectID == 0 )
            return;
        if ( step._feetWetness > 0.0f )
        {
            WetFeetState& feet   = _mapWetFeet[ownerObjectID];
            feet._remainingSteps = table._wetFeetSteps;
            feet._wetness        = step._feetWetness;
            return;
        }
        const auto found = _mapWetFeet.find( ownerObjectID );
        if ( found == _mapWetFeet.end() )
            return;
        WetFeetState& feet = found->second;
        // 걸음마다 옅어진다 — 첫 자국이 가장 젖고 마지막 자국은 1 / _wetFeetSteps 만큼이다.
        const float32 strength = table._wetFeetSteps > 0 ? feet._wetness * static_cast<float32>( feet._remainingSteps ) / static_cast<float32>( table._wetFeetSteps ) : 0.0f;
        if ( bGroundWet == false && _pManager->getSubsystem<SurfaceDeformSystem>().applyWetPrint( point, yaw, table._wetPrintSize, strength, table._wetPrintDrySpeed ) )
            ++_wetPrintCount;
        if ( feet._remainingSteps <= 1 )
            _mapWetFeet.erase( found );
        else
            --feet._remainingSteps;
    }

    void SurfaceFootprintSystem::playSound( const hashed_string& sound, const float3& point ) const
    {
        ISoundPlayer* pSound = locateSubsystem<ISoundPlayer>( _pManager->getSceneEngineServices()._pEngineSubsystems );
        if ( pSound != nullptr )
            (void)pSound->postEventAt( sound, point );
    }
} // namespace sw
