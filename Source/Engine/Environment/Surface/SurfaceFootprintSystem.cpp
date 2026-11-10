#include "pch.h"

#include "Engine/Environment/Surface/SurfaceFootprintSystem.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Environment/Surface/SurfaceDeformSystem.h"
#include "Engine/Environment/Surface/SurfaceKind.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"

namespace sw
{
    namespace
    {
        struct SurfaceFootprintSystemInternal
        {
            /** @brief 감시 값 `Surface.FootprintPlaced` 입니다. 이 씬에서 찍은 발자국 수(누적)입니다. */
            [[nodiscard]] static bool readFootprintPlaced( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                if ( query._pManager == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( query._pManager->getSubsystem<SurfaceFootprintSystem>().getPlacedCount() ) );
                return true;
            }

            /** @brief 감시 값 `Surface.FootstepCount` 입니다. 종류 `key` 를 밟은 발걸음 수(누적)입니다. */
            [[nodiscard]] static bool readFootstepCount( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                if ( query._pManager == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( query._pManager->getSubsystem<SurfaceFootprintSystem>().getFootstepCount( hashed_string( query._key ) ) ) );
                return true;
            }

            /** @brief 감시 값 `Surface.LastFootstep` 입니다. 마지막 발걸음의 표면 종류 이름입니다(없으면 값이 없다). */
            [[nodiscard]] static bool readLastFootstep( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                if ( query._pManager == nullptr )
                    return false;
                const hashed_string& kind = query._pManager->getSubsystem<SurfaceFootprintSystem>().getLastKind();
                if ( kind.empty() )
                {
                    outValue.setText( "no footstep yet" );
                    return false;
                }
                outValue.setText( kind.c_str() );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( surfaceFootprintPlaced, "Surface.FootprintPlaced", "Footprints stamped on the surface deform layer in this scene (cumulative)",
                               &SurfaceFootprintSystemInternal::readFootprintPlaced, false, false );
    SW_AUTOMATION_QUERY_WATCH( surfaceFootstepCount, "Surface.FootstepCount", "Footsteps on the surface kind named key (cumulative)", &SurfaceFootprintSystemInternal::readFootstepCount, false, true );
    SW_AUTOMATION_QUERY_WATCH( surfaceLastFootstep, "Surface.LastFootstep", "Surface kind of the last footstep", &SurfaceFootprintSystemInternal::readLastFootstep, false, false );
} // namespace sw

namespace sw
{
    SW_SCENE_SERVICE( SurfaceFootprintSystem, FlushEnd, makeSceneFrameAccess( SceneDataKindSet{}, SceneDataKindSet{}, SceneDataKindSet{ SceneDataKind::Structure, SceneDataKind::Transform } ) )

    SurfaceFootprintSystem::SurfaceFootprintSystem( GameObjectManager& manager )
        : _pManager{ &manager }
        , _mapFootstepCount{}
        , _lastKind{}
        , _placedCount{ 0 }
    {
    }

    void SurfaceFootprintSystem::onSceneCleared()
    {
        _mapFootstepCount.clear();
        _lastKind    = hashed_string{};
        _placedCount = 0;
    }

    void SurfaceFootprintSystem::addFootstep( const SurfaceKindDef& kind, const float3& point, float32 yaw )
    {
        ++_mapFootstepCount[kind._name];
        _lastKind = kind._name;
        if ( kind.hasFootprints() == false )
            return;
        SurfaceDeformSystem& deform    = _pManager->getSubsystem<SurfaceDeformSystem>();
        const uint32         stampedBy = deform.getStampCount();
        deform.stamp( kind._footprint, point, yaw );
        // 찍지 못한 발걸음(전용 서버 · 표를 읽지 못함)은 세지 않는다 — 감시 값이 보이는 자국 수와 같다.
        if ( deform.getStampCount() != stampedBy )
            ++_placedCount;
    }

    uint32 SurfaceFootprintSystem::getFootstepCount( const hashed_string& kind ) const
    {
        const auto found = _mapFootstepCount.find( kind );
        return found != _mapFootstepCount.end() ? found->second : 0;
    }
} // namespace sw
