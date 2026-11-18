#include "pch.h"

#include "Engine/Environment/Surface/SurfaceFootprintSystem.h"

#include "Core/Foundation/BuildInfo.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Environment/Surface/SurfaceDeformSystem.h"
#include "Engine/Environment/Surface/SurfaceKind.h"
#include "Engine/Environment/Surface/SurfaceSample.h"
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

            /** @brief 감시 값 `Surface.FootstepCount` 입니다. 종류 `key` 를 밟은 발걸음 수(누적)입니다. */
            [[nodiscard]] static bool readFootstepCount( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SurfaceFootprintSystem* pSystem = findSystem( query );
                if ( pSystem == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pSystem->getFootstepCount( hashed_string( query._key ) ) ) );
                return true;
            }

            /** @brief 마지막 발걸음이 있으면 true 입니다. 없으면 @p outValue 에 까닭을 적는다. */
            [[nodiscard]] static bool hasLastFootstep( const SurfaceFootprintSystem* pSystem, AutomationWatchValue& outValue )
            {
                if ( pSystem == nullptr )
                    return false;
                if ( pSystem->getLastKind().empty() == false )
                    return true;
                outValue.setText( "no footstep yet" );
                return false;
            }

            /** @brief 감시 값 `Surface.LastFootstep` 입니다. 마지막 발걸음의 표면 종류 이름입니다(없으면 값이 없다). */
            [[nodiscard]] static bool readLastFootstep( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SurfaceFootprintSystem* pSystem = findSystem( query );
                if ( hasLastFootstep( pSystem, outValue ) == false )
                    return false;
                outValue.setText( pSystem->getLastKind().c_str() );
                return true;
            }

            /** @brief 감시 값 `Surface.LastFootstepState` 입니다. 마지막 발걸음이 고른 반응("dry" · "wet")입니다. */
            [[nodiscard]] static bool readLastState( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SurfaceFootprintSystem* pSystem = findSystem( query );
                if ( hasLastFootstep( pSystem, outValue ) == false )
                    return false;
                outValue.setText( pSystem->isLastWet() ? "wet" : "dry" );
                return true;
            }

            /** @brief 감시 값 `Surface.LastFootstepSound` 입니다. 마지막 발걸음 반응의 소리 이벤트("none" = 조용함)입니다. */
            [[nodiscard]] static bool readLastSound( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SurfaceFootprintSystem* pSystem = findSystem( query );
                if ( hasLastFootstep( pSystem, outValue ) == false )
                    return false;
                outValue.setText( pSystem->getLastSound().empty() ? "none" : pSystem->getLastSound().c_str() );
                return true;
            }

            /** @brief 감시 값 `Surface.LastFootstepEffect` 입니다. 마지막 발걸음 반응의 효과 경로("none" = 없음)입니다. */
            [[nodiscard]] static bool readLastEffect( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SurfaceFootprintSystem* pSystem = findSystem( query );
                if ( hasLastFootstep( pSystem, outValue ) == false )
                    return false;
                outValue.setText( pSystem->getLastEffect().empty() ? "none" : pSystem->getLastEffect().c_str() );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( surfaceFootprintPlaced, "Surface.FootprintPlaced", "Footprints stamped on the surface deform layer in this scene (cumulative)",
                               &SurfaceFootprintSystemInternal::readFootprintPlaced, false, false );
    SW_AUTOMATION_QUERY_WATCH( surfaceWetPrintPlaced, "Surface.WetPrintPlaced", "Wet prints left by wet feet on dry ground in this scene (cumulative)",
                               &SurfaceFootprintSystemInternal::readWetPrintPlaced, false, false );
    SW_AUTOMATION_QUERY_WATCH( surfaceFootstepCount, "Surface.FootstepCount", "Footsteps on the surface kind named key (cumulative)", &SurfaceFootprintSystemInternal::readFootstepCount, false, true );
    SW_AUTOMATION_QUERY_WATCH( surfaceLastFootstep, "Surface.LastFootstep", "Surface kind of the last footstep", &SurfaceFootprintSystemInternal::readLastFootstep, false, false );
    SW_AUTOMATION_QUERY_WATCH( surfaceLastFootstepState, "Surface.LastFootstepState", "Response the last footstep took: dry or wet", &SurfaceFootprintSystemInternal::readLastState, false,
                               false );
    SW_AUTOMATION_QUERY_WATCH( surfaceLastFootstepSound, "Surface.LastFootstepSound", "Audio event of the last footstep response (none: silent)", &SurfaceFootprintSystemInternal::readLastSound,
                               false, false );
    SW_AUTOMATION_QUERY_WATCH( surfaceLastFootstepEffect, "Surface.LastFootstepEffect", "Effect of the last footstep response (none: no effect)", &SurfaceFootprintSystemInternal::readLastEffect,
                               false, false );
} // namespace sw

namespace sw
{
    SW_SCENE_SERVICE( SurfaceFootprintSystem, FlushEnd, makeSceneFrameAccess( SceneDataKindSet{}, SceneDataKindSet{}, SceneDataKindSet{ SceneDataKind::Structure, SceneDataKind::Transform } ) )

    SurfaceFootprintSystem::SurfaceFootprintSystem( GameObjectManager& manager )
        : _pManager{ &manager }
        , _mapFootstepCount{}
        , _mapWetFeet{}
        , _lastKind{}
        , _lastSound{}
        , _lastEffect{}
        , _placedCount{ 0 }
        , _wetPrintCount{ 0 }
        , _bLastWet{ SW_FALSE }
    {
    }

    void SurfaceFootprintSystem::onSceneCleared()
    {
        _mapFootstepCount.clear();
        _mapWetFeet.clear();
        _lastKind  = hashed_string{};
        _lastSound = hashed_string{};
        _lastEffect.clear();
        _placedCount   = 0;
        _wetPrintCount = 0;
        _bLastWet      = SW_FALSE;
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
        ++_mapFootstepCount[kind._name];
        _lastKind   = kind._name;
        _bLastWet   = bWet ? SW_TRUE : SW_FALSE;
        _lastSound  = step._sound;
        _lastEffect = step._effect.isSet() ? step._effect.get() : string{};

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

    uint32 SurfaceFootprintSystem::getFootstepCount( const hashed_string& kind ) const
    {
        const auto found = _mapFootstepCount.find( kind );
        return found != _mapFootstepCount.end() ? found->second : 0;
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
