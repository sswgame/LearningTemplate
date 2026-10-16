#include "pch.h"

#include "Games/HarvestValley/FarmVillageDirectorComponent.h"

#include "Core/Common/FourCcUtil.h"
#include "Core/Container/StringUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/SceneComponent.h"
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

            /** @brief @p objectName 이 장애물 이름 @p baseName 이거나 저장소가 번호를 붙인 같은 이름(`Tree_3`)이면 true 입니다. */
            static bool matchesObstacleName( string_view objectName, string_view baseName )
            {
                const bool bSamePrefix =
                    baseName.empty() == false && objectName.size() >= baseName.size() && StringUtil::equals( objectName.substr( 0, baseName.size() ), baseName, true );
                if ( bSamePrefix == false )
                    return false;
                if ( objectName.size() == baseName.size() )
                    return true;
                const bool bNumbered = objectName.size() > baseName.size() + 1 && objectName[baseName.size()] == '_';
                if ( bNumbered == false )
                    return false;
                for ( size_t index = baseName.size() + 1; index < objectName.size(); ++index )
                {
                    if ( objectName[index] < '0' || objectName[index] > '9' )
                        return false;
                }
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    FarmVillageDirectorComponent::FarmVillageDirectorComponent()
        : _scheduleDataPath{}
        , _villagerPrefab{}
        , _navOrigin{}
        , _navWidth{ 0 }
        , _navHeight{ 0 }
        , _navCellSize{ 0.0f }
        , _navObstacleNames{}
        , _navObstacleRadius{ 0.8f }
        , _bodyCatchUpScale{ 1.5f }
        , _bodySnapDistance{ 3.0f }
        , _bodyAccelerationPerSpeed{ 10.0f }
        , _bodySlowDistance{ 1.0f }
        , _catalog{}
        , _navGrid{}
        , _pathfinder{}
        , _finePathing{ &_navGrid, &_pathfinder, SchedulePlane::XZ }
        , _schedule{}
        , _listPosition{}
        , _listBodySpeed{}
    {
    }

    FarmVillageDirectorComponent::~FarmVillageDirectorComponent() = default;

    bool FarmVillageDirectorComponent::rebuildNavGrid()
    {
        const bool bNoGrid = _navWidth <= 0 || _navHeight <= 0 || _navCellSize <= 0.0f;
        if ( bNoGrid )
        {
            SW_LOG_WARNING( "[Village] the village nav grid needs _navWidth, _navHeight and _navCellSize - the villagers stay home" );
            return false;
        }
        _navGrid.initialize( _navWidth, _navHeight, _navCellSize, _navOrigin );
        vector<string_view> listObstacleName;
        StringUtil::forEachToken( string_view{ _navObstacleNames }, ", ", [&listObstacleName]( string_view token )
        { listObstacleName.push_back( token ); } );
        const GameObjectManager* pManager     = getOwner()->getManager();
        const int32              radiusCells  = static_cast<int32>( MathUtil::ceil( _navObstacleRadius / _navCellSize ) );
        uint32                   blockedCount = 0;
        pManager->forEachGameObject( [&]( GameObject* pObject )
        {
            const SceneComponent* pScene     = pObject->getPrimarySceneComponent();
            bool                  bObstacle  = false;
            const string_view     objectName = pObject->getName().c_str();
            for ( const string_view obstacleName : listObstacleName )
            {
                bObstacle = bObstacle || FarmVillageDirectorComponentInternal::matchesObstacleName( objectName, obstacleName );
            }
            if ( bObstacle == false || pScene == nullptr )
                return;
            const float3 position = pScene->getWorldPosition();
            const int2   center   = _navGrid.computeCell( position );
            for ( int32 offsetY = -radiusCells; offsetY <= radiusCells; ++offsetY )
            {
                for ( int32 offsetX = -radiusCells; offsetX <= radiusCells; ++offsetX )
                {
                    const int2 cell{ center._x + offsetX, center._y + offsetY };
                    if ( _navGrid.isInside( cell ) && float3::getHorizontalDistance( _navGrid.computeCellCenter( cell ), position ) <= _navObstacleRadius )
                    {
                        _navGrid.setBlocked( cell._x, cell._y, true );
                        ++blockedCount;
                    }
                }
            }
        } );
        SW_LOG_INFO( "[Village] nav grid %#x%# (%# m cells), %# cell(s) blocked by obstacles", _navWidth, _navHeight, _navCellSize, blockedCount );
        return true;
    }

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
        if ( rebuildNavGrid() == false )
            return false;
        const WorldClock&      clock = pState->getClock();
        ScheduleSystemSettings settings;
        settings._clock = clock.getSettings();
        // 계획은 곧은 선(기본), 화면 안 걸음은 격자 길 — 도착 시각은 계획이 정하므로 격자를 바꿔도 일정이 갈리지 않는다.
        _schedule.setPathing( nullptr, &_finePathing );
        _schedule.initialize( &_catalog, settings, FarmVillageDirectorComponentInternal::computeAbsoluteMinute( clock ) );
        _listPosition.assign( static_cast<size_t>( _schedule.getNpcCount() ), float3{} );
        _listBodySpeed.assign( static_cast<size_t>( _schedule.getNpcCount() ), 0.0f );
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
        const WorldClock& clock = pState->getClock();
        _schedule.update( clock );
        // 일정의 걸음(게임 분당 거리)을 실제 초당 거리로 — 시계가 빨라지면(빨리 감기) 몸도 그만큼 빨리 걷는다.
        const float32 minutesPerSecond = static_cast<float32>( kScheduleMinutesPerDay ) / MathUtil::max( clock.getSettings()._secondsPerDay, 1.0f ) * clock.getTimeScale();
        for ( int32 npcIndex = 0; npcIndex < _schedule.getNpcCount(); ++npcIndex )
        {
            const ScheduleNpcDef* pDef                      = _catalog.findNpc( _schedule.getNpcID( npcIndex ) );
            _listPosition[static_cast<size_t>( npcIndex )]  = _schedule.getNpcView( npcIndex )._location._position;
            _listBodySpeed[static_cast<size_t>( npcIndex )] = pDef != nullptr ? pDef->_unitsPerMinute * minutesPerSecond * _bodyCatchUpScale : 0.0f;
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

    NavAgentSettings FarmVillageDirectorComponent::makeBodySettings( int32 npcIndex ) const
    {
        const bool       bInRange = 0 <= npcIndex && npcIndex < static_cast<int32>( _listBodySpeed.size() );
        NavAgentSettings settings;
        settings._maxSpeed        = bInRange ? _listBodySpeed[static_cast<size_t>( npcIndex )] : 0.0f;
        settings._maxAcceleration = settings._maxSpeed * _bodyAccelerationPerSpeed;
        settings._slowDistance    = _bodySlowDistance;
        return settings;
    }

    void FarmVillageDirectorComponent::collectNeighborPositions( int32 npcIndex, vector<float3>& outListPosition ) const
    {
        outListPosition.clear();
        for ( int32 otherIndex = 0; otherIndex < static_cast<int32>( _listPosition.size() ); ++otherIndex )
        {
            if ( otherIndex != npcIndex )
                outListPosition.push_back( _listPosition[static_cast<size_t>( otherIndex )] );
        }
    }
} // namespace sw
