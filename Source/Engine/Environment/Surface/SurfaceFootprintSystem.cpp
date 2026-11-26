#include "pch.h"

#include "Engine/Environment/Surface/SurfaceFootprintSystem.h"

#include "Core/Foundation/BuildInfo.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Environment/Surface/SurfaceDeformSystem.h"
#include "Engine/Environment/Surface/SurfaceKind.h"
#include "Engine/Environment/Surface/SurfaceSample.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Subsystem/SceneEngineSubsystems.h"
#include "Engine/Object/GameObject/Subsystem/SceneSubsystemRegistry.h"
#include "Engine/Presentation/ISoundPlayer.h"

namespace sw
{
    uint32 SurfaceFootstepRecord::getCount( const hashed_string& kind ) const
    {
        const SurfaceFootstepKindNote* pNote = findKind( kind );
        return pNote != nullptr ? pNote->_count : 0;
    }

    const SurfaceFootstepKindNote* SurfaceFootstepRecord::findKind( const hashed_string& kind ) const
    {
        const auto found = _mapKind.find( kind );
        return found != _mapKind.end() ? &found->second : nullptr;
    }

    void SurfaceFootstepRecord::note( const hashed_string& kind, const SurfaceStepResponse& step, bool bWet )
    {
        SurfaceFootstepKindNote& note = _mapKind[kind];
        ++note._count;
        note._lastSound  = step._sound;
        note._lastEffect = step._effect.isSet() ? step._effect.get() : string{};
        note._bLastWet   = bWet ? SW_TRUE : SW_FALSE;
        _lastKind        = kind;
    }

    SW_SCENE_SUBSYSTEM( SurfaceFootprintSystem, FlushEnd, makeSceneFrameAccess( SceneDataKindSet{}, SceneDataKindSet{}, SceneDataKindSet{ SceneDataKind::Structure, SceneDataKind::Transform } ) )

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
        ISoundPlayer* pSound = findSubsystemFrom<ISoundPlayer>( _pManager->getSceneEngineSubsystems()._pEngineSubsystems );
        if ( pSound != nullptr )
            (void)pSound->postEventAt( sound, point );
    }
} // namespace sw
