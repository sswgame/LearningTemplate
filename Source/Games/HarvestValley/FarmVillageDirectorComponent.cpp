#include "pch.h"

#include "Games/HarvestValley/FarmVillageDirectorComponent.h"

#include "Core/Common/FourCcUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Base/Gameplay/SessionState/SessionStateComponent.h"

#include "Games/HarvestValley/FarmDirectorComponent.h"
#include "Games/HarvestValley/FarmVillagerComponent.h"

namespace sw
{
    SW_LOG_CALLER( "FarmVillageDirector" );

    namespace
    {
        struct FarmVillageDirectorComponentInternal
        {
            static constexpr uint32 kStateTag     = FourCcUtil::make( "FVIL" );
            static constexpr uint32 kStateVersion = 1;

            /** @brief 시계의 지금 시각을 0 일 0:00 부터 센 분으로 바꿉니다(일정 시스템의 시간 축). */
            static int32 computeAbsoluteMinute( const WorldClock& clock )
            {
                return clock.getDay() * kScheduleMinutesPerDay + clock.getHourInt() * static_cast<int32>( constant::kSecondsPerMinute ) + clock.getMinute();
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    FarmVillageDirectorComponent::FarmVillageDirectorComponent()
        : _scheduleDataPath{}
        , _villagerPrefab{}
        , _catalog{}
        , _schedule{}
        , _listPosition{}
    {
    }

    FarmVillageDirectorComponent::~FarmVillageDirectorComponent() = default;

    bool FarmVillageDirectorComponent::startGame()
    {
        const SessionStateComponent* pState = SessionStateComponent::findOnOwner( *this );
        if ( pState == nullptr || pState->isInitialized() == false )
        {
            SW_LOG_WARNING( "[Village] no open SessionStateComponent on this object (put the village director after the farm director) - the villagers stay home" );
            return false;
        }
        _catalog.clear();
        if ( _catalog.loadFromResource( _scheduleDataPath ) == false || _catalog.getNpcs().empty() )
        {
            SW_LOG_WARNING( "[Village] %# could not be loaded - the villagers stay home", _scheduleDataPath.c_str() );
            return false;
        }
        const WorldClock&      clock = pState->getClock();
        ScheduleSystemSettings settings;
        settings._clock = clock.getSettings();
        _schedule.initialize( &_catalog, settings, FarmVillageDirectorComponentInternal::computeAbsoluteMinute( clock ) );
        _listPosition.assign( static_cast<size_t>( _schedule.getNpcCount() ), float3{} );
        for ( int32 npcIndex = 0; npcIndex < _schedule.getNpcCount(); ++npcIndex )
        {
            _schedule.setNpcLOD( npcIndex, ScheduleLOD::Near );
            _listPosition[static_cast<size_t>( npcIndex )] = _schedule.getNpcView( npcIndex )._location._position;
        }
        SW_LOG_INFO( "[Village] %# villager(s) follow %# from day %# %#:%#", _schedule.getNpcCount(), _scheduleDataPath.c_str(), clock.getDay(), clock.getHourInt(),
                     clock.getMinute() );
        return true;
    }

    void FarmVillageDirectorComponent::writeState( Archive& outArchive ) const
    {
        StateArchiveUtil::writeHeader( outArchive, FarmVillageDirectorComponentInternal::kStateTag, FarmVillageDirectorComponentInternal::kStateVersion );
    }

    bool FarmVillageDirectorComponent::readState( Archive& archive )
    {
        // 일정은 시계의 함수다 — 시작할 때 그 시각의 계획을 이미 세웠다.
        return StateArchiveUtil::readHeader( archive, FarmVillageDirectorComponentInternal::kStateTag, FarmVillageDirectorComponentInternal::kStateVersion );
    }

    void FarmVillageDirectorComponent::updateGame( float32 deltaTime )
    {
        (void)deltaTime;
        const SessionStateComponent* pState = SessionStateComponent::findOnOwner( *this );
        if ( pState == nullptr )
            return;
        // 농장 디렉터는 같은 오브젝트의 앞 컴포넌트라 이번 틱의 날씨를 이미 정했다.
        const FarmDirectorComponent* pFarm = getOwner()->getComponent<FarmDirectorComponent>();
        _schedule.setWeather( hashed_string( pFarm != nullptr && pFarm->isRaining() ? "rain" : "sunny" ) );
        _schedule.update( pState->getClock() );
        for ( int32 npcIndex = 0; npcIndex < _schedule.getNpcCount(); ++npcIndex )
        {
            _listPosition[static_cast<size_t>( npcIndex )] = _schedule.getNpcView( npcIndex )._location._position;
        }
    }

    void FarmVillageDirectorComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        if ( bRespawnViews == false )
            return;
        const GameObjectHandle director = getOwner()->getHandle();
        for ( int32 npcIndex = 0; npcIndex < _schedule.getNpcCount(); ++npcIndex )
        {
            // 이름은 `Villager.<NPC id>` — 시나리오가 `Object.*` 탐침의 target 으로 쓴다.
            const string name      = "Villager." + string( _schedule.getNpcID( npcIndex ).c_str() );
            GameObject*  pVillager = spawnPrefab( manager, _villagerPrefab, name.c_str() );
            if ( pVillager == nullptr )
                continue;
            FarmVillagerComponent* pView = pVillager->getComponent<FarmVillagerComponent>();
            if ( pView != nullptr )
                pView->assignVillager( director, npcIndex );
        }
    }

    float3 FarmVillageDirectorComponent::getVillagerPosition( int32 npcIndex ) const
    {
        const bool bInRange = 0 <= npcIndex && npcIndex < static_cast<int32>( _listPosition.size() );
        return bInRange ? _listPosition[static_cast<size_t>( npcIndex )] : float3{};
    }
} // namespace sw
