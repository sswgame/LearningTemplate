#include "pch.h"

#include "GameFramework/Kits/Genre/Simulation/Farming/Component/FarmVillageGameModeComponent.h"

#include "Core/Encoding/FourCCUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/BinarySerializer.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Serialization/Serializer/SerializeContext.h"

#include "GameFramework/Kits/Genre/Simulation/Farming/Component/FarmVillagerComponent.h"
#include "GameFramework/Runtime/Character/AI/Schedule/NPCScheduleComponent.h"
#include "GameFramework/Runtime/Character/Navigation/NavGridObstacle.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterAppearanceComponent.h"
#include "GameFramework/Runtime/Gameplay/GameState/GameStateComponent.h"

namespace sw
{
    namespace
    {
        struct FarmVillageGameModeComponentInternal
        {
            static constexpr uint32 kStateTag     = FourCCUtil::make( "FVIL" );
            static constexpr uint32 kStateVersion = 2; ///< 2 판: 머리 뒤에 `ScheduleSaveState`(리플렉션 바이너리)

            /** @brief 시계의 지금 시각을 0 일 0:00 부터 센 분으로 바꿉니다(일정 시스템의 시간 축). */
            static int32 computeAbsoluteMinute( const WorldClock& clock )
            {
                return clock.getDay() * kScheduleMinutesPerDay + clock.getHourInt() * static_cast<int32>( constant::time::kSecondsPerMinute ) + clock.getMinute();
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    FarmVillageGameModeComponent::FarmVillageGameModeComponent()
        : _scheduleDataPath{}
        , _villagerPrefab{}
        , _listVillagerPreset{}
        , _navOrigin{}
        , _navWidth{ 0 }
        , _navHeight{ 0 }
        , _navCellSize{ 0.0f }
        , _navObstacleNames{}
        , _navObstacleClearance{ 0.4f }
        , _bodyCatchUpScale{ 1.5f }
        , _bodySnapDistance{ 3.0f }
        , _bodyAccelerationPerSpeed{ 10.0f }
        , _bodySlowDistance{ 1.0f }
        , _pCatalog{}
        , _navGrid{}
        , _pathfinder{}
        , _finePathing{ &_navGrid, &_pathfinder, SchedulePlane::XZ }
        , _schedule{}
        , _pendingSave{}
        , _bHasPendingSave{ false }
        , _listPosition{}
        , _listBodySpeed{}
    {
    }

    FarmVillageGameModeComponent::~FarmVillageGameModeComponent() = default;

    bool FarmVillageGameModeComponent::rebuildNavGrid()
    {
        const bool bNoGrid = _navWidth <= 0 || _navHeight <= 0 || _navCellSize <= 0.0f;
        if ( bNoGrid )
        {
            SW_LOG_ERROR_ID( "FarmVillageGameMode.VillageNavGridNeeds", "[Village] the village nav grid needs _navWidth, _navHeight and _navCellSize - the villagers stay home" );
            return false;
        }
        if ( _navGrid.initialize( _navWidth, _navHeight, _navCellSize, _navOrigin ) == false )
            return false;
        vector<string_view> listObstacleName;
        StringUtil::forEachToken( string_view{ _navObstacleNames }, ", ", [&listObstacleName]( string_view token )
        { listObstacleName.push_back( token ); } );
        const GameObjectManager*  pManager     = getOwner()->getManager();
        uint32                    blockedCount = 0;
        vector<const GameObject*> listObstacle;
        for ( const string_view obstacleName : listObstacleName )
        {
            // 장애물 이름이거나 저장소가 번호를 붙인 같은 이름(`Tree_3`)인 오브젝트 — 밑 이름 색인이라 씬을 돌지 않는다.
            pManager->findGameObjectsByBaseName( obstacleName, listObstacle );
            for ( const GameObject* pObject : listObstacle )
            {
                const RigidBodyComponent* const pBody   = pObject->getComponent<RigidBodyComponent>();
                const bool                      bStatic = pBody != nullptr && pBody->getBodyType() == PhysicsBodyType::Static;
                if ( bStatic == false )
                {
                    SW_LOG_ERROR_ID( "FarmVillageGameMode.ObstacleNoStaticBody", "[Village] nav obstacle '%#' has no static rigid body - its size is the body's shape, the villagers stay home", pObject->getName().c_str() );
                    return false;
                }
                blockedCount += NavGridObstacle::blockBody( _navGrid, pManager->getWorldOrigin(), *pBody, _navObstacleClearance );
            }
        }
        SW_LOG_INFO_ID( "FarmVillageGameMode.NavGridXM", "[Village] nav grid %#x%# (%# m cells), %# cell(s) blocked by obstacles", _navWidth, _navHeight, _navCellSize, blockedCount );
        return true;
    }

    bool FarmVillageGameModeComponent::openSchedule( const WorldClock& clock )
    {
        ScheduleSystemSettings settings;
        settings._clock = clock.getSettings();
        // 계획은 곧은 선(기본), 화면 안 걸음은 격자 길 — 도착 시각은 계획이 정하므로 격자를 바꿔도 일정이 갈리지 않는다.
        _schedule.setPathing( nullptr, &_finePathing );
        if ( _schedule.initialize( _pCatalog.get(), settings, FarmVillageGameModeComponentInternal::computeAbsoluteMinute( clock ) ) == false )
            return false;
        _listPosition.assign( static_cast<size_t>( _schedule.getNPCCount() ), float3{} );
        _listBodySpeed.assign( static_cast<size_t>( _schedule.getNPCCount() ), 0.0f );
        return true;
    }

    void FarmVillageGameModeComponent::showAllNPCs()
    {
        for ( int32 npcIndex = 0; npcIndex < _schedule.getNPCCount(); ++npcIndex )
        {
            _schedule.setNPCLOD( npcIndex, ScheduleLOD::Near );
            _listPosition[static_cast<size_t>( npcIndex )] = _schedule.getNPCView( npcIndex )._location._position;
        }
    }

    bool FarmVillageGameModeComponent::startGame()
    {
        const GameStateComponent* pState = GameStateComponent::findOnOwner( *this );
        if ( pState == nullptr || pState->isInitialized() == false )
        {
            SW_LOG_ERROR_ID( "FarmVillageGameMode.NoOpenSessionStateComponentObject", "[Village] no open GameStateComponent on this object (put the village game mode after the farm game mode) - the villagers stay home" );
            return false;
        }
        if ( acquireRequired( _scheduleDataPath, _pCatalog ) == false )
            return false;
        if ( _pCatalog->getNPCs().empty() )
        {
            SW_LOG_ERROR_ID( "FarmVillageGameMode.ReadButListsNo", "[Village] %# was read but lists no villager - the villagers stay home", _scheduleDataPath.c_str() );
            return false;
        }
        if ( rebuildNavGrid() == false )
            return false;
        const WorldClock& clock = pState->getClock();
        if ( openSchedule( clock ) == false )
            return false; // 범위 밖 설정은 일정 시스템이 오류를 남겼다
        if ( _bHasPendingSave )
        {
            // 일정이 서기 전에 읽은 저장(세이브 불러오기)을 지금 건다 — 끼어들기 · 호감도 · 약속 결과가 시계만으로는 나오지 않는다.
            _schedule.restoreSaveState( _pendingSave );
            _pendingSave     = ScheduleSaveState{};
            _bHasPendingSave = false;
        }
        showAllNPCs();
        SW_LOG_INFO_ID( "FarmVillageGameMode.VillagerFollowDay", "[Village] %# villager(s) follow %# from day %# %#:%#", _schedule.getNPCCount(), _scheduleDataPath.c_str(), clock.getDay(), clock.getHourInt(),
                        clock.getMinute() );
        return true;
    }

    void FarmVillageGameModeComponent::serializeGameState( StateIO& io )
    {
        using Internal = FarmVillageGameModeComponentInternal;
        // 판이 정확히 같을 때만 읽는다(첫 출시 전이라 옛 판 리더가 없다).
        io.header( Internal::kStateTag, Internal::kStateVersion );
        if ( io.isOk() == false )
            return;
        ScheduleSaveState state;
        if ( io.isWriting() )
        {
            if ( _bHasPendingSave )
                state = _pendingSave; // 아직 걸지 않은 저장은 그대로 다시 싣는다(일정이 서기 전의 핫 리로드)
            else
                _schedule.fillSaveState( state );
            // 실을 수 없으면 직렬화기가 아카이브를 오류 상태로 두어 저장 전체가 실패한다(이유는 직렬화기가 알린다).
            if ( BinarySerializer::serialize( &state, *ScheduleSaveState::StaticType(), io.getArchive(), SerializeContext::getDefault( SerializePurpose::SaveGame ) ) == false )
                SW_LOG_ERROR_ID( "FarmVillageGameMode.ScheduleSaveStateCould", "[Village] the schedule save state could not be written" );
            return;
        }
        // 임시에 끝까지 읽은 뒤에만 바꾼다(반쯤 읽은 상태를 남기지 않는다).
        if ( io.check( BinarySerializer::deserialize( &state, *ScheduleSaveState::StaticType(), io.getArchive(), SerializeContext::getDefault( SerializePurpose::SaveGame ) ) ) == false )
        {
            SW_LOG_ERROR_ID( "FarmVillageGameMode.ScheduleSaveStateCould2", "[Village] the schedule save state could not be read" );
            return;
        }
        if ( _schedule.getNPCCount() > 0 )
        {
            // 일정표가 다시 읽혔으면 새 판으로 일정을 다시 세운 뒤 저장을 건다(NPC 는 id 로 다시 찾는다). 빈 일정표는 받지 않는다 — 지금 일정이 그대로다.
            const bool bCatalogReloaded = _pCatalog != nullptr && _schedule.getCatalog() != _pCatalog.get();
            if ( bCatalogReloaded )
            {
                const GameStateComponent* const pState = GameStateComponent::findOnOwner( *this );
                if ( io.check( _pCatalog->getNPCs().empty() == false && pState != nullptr && openSchedule( pState->getClock() ) ) == false )
                    return;
            }
            _schedule.restoreSaveState( state );
            if ( bCatalogReloaded )
                showAllNPCs();
            return;
        }
        _pendingSave     = std::move( state );
        _bHasPendingSave = true;
    }

    void FarmVillageGameModeComponent::updateGame( float32 deltaTime )
    {
        (void)deltaTime;
        const GameStateComponent* pState = GameStateComponent::findOnOwner( *this );
        if ( pState == nullptr )
            return;
        // 일정의 날씨는 판 상태 날씨의 id 다(farm.weather.xml 의 sunny · rain — 일정 달력 villagers.schedules.xml 의 weathers 와 같은 이름).
        _schedule.setWeather( pState->getWeather().getCurrent() );
        const WorldClock& clock = pState->getClock();
        _schedule.update( clock );
        // 일정의 걸음(게임 분당 거리)을 실제 초당 거리로 — 시계가 빨라지면(빨리 감기) 몸도 그만큼 빨리 걷는다.
        const float32 minutesPerSecond = static_cast<float32>( kScheduleMinutesPerDay ) / MathUtil::max( clock.getSettings()._secondsPerDay, 1.0f ) * clock.getTimeScale();
        for ( int32 npcIndex = 0; npcIndex < _schedule.getNPCCount(); ++npcIndex )
        {
            const NPCScheduleDef* pDef                      = _pCatalog->findNPC( _schedule.getNPCID( npcIndex ) );
            _listPosition[static_cast<size_t>( npcIndex )]  = _schedule.getNPCView( npcIndex )._location._position;
            _listBodySpeed[static_cast<size_t>( npcIndex )] = pDef != nullptr ? pDef->_unitsPerMinute * minutesPerSecond * _bodyCatchUpScale : 0.0f;
        }
    }

    void FarmVillageGameModeComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        if ( bRespawnViews == false )
            return;
        const GameObjectHandle gameMode = getOwner()->getHandle();
        for ( int32 npcIndex = 0; npcIndex < _schedule.getNPCCount(); ++npcIndex )
        {
            // 이름은 `Villager.<NPC id>` — 시나리오가 `Object.*` 감시 값의 target 으로 쓴다.
            fixed_string<constant::buffer::kMax128> name;
            formatstring( name.data(), name.capacity(), "Villager.%#", _schedule.getNPCID( npcIndex ).c_str() );
            GameObject* pVillager = spawnPrefab( manager, _villagerPrefab, name.c_str() );
            if ( pVillager == nullptr )
                continue;
            // 일정 NPC 정체는 이름이 아니라 칸으로 잇는다(디버그 HUD · 상호작용 끼어들기가 읽는다).
            NPCScheduleComponent* pNPC = pVillager->getComponent<NPCScheduleComponent>();
            if ( pNPC == nullptr )
                pNPC = pVillager->addComponent<NPCScheduleComponent>();
            if ( pNPC != nullptr )
                pNPC->setNPCID( _schedule.getNPCID( npcIndex ) );
            FarmVillagerComponent* pView = pVillager->getComponent<FarmVillagerComponent>();
            if ( pView != nullptr )
                pView->assignVillager( gameMode, npcIndex );
            // 외형은 NPC 마다 다르게 — 프리셋을 번호로 돌려 고르고 번호를 씨앗으로 준다(같은 저장이면 같은 사람이 같은 차림이다).
            CharacterAppearanceComponent* pAppearance = pVillager->getComponent<CharacterAppearanceComponent>();
            if ( pAppearance != nullptr && _listVillagerPreset.empty() == false )
                pAppearance->setPreset( _listVillagerPreset[static_cast<size_t>( npcIndex ) % _listVillagerPreset.size()].key(), static_cast<uint32>( npcIndex + 1 ) );
        }
    }

    AbsolutePosition FarmVillageGameModeComponent::getVillagerPosition( int32 npcIndex ) const
    {
        const bool bInRange = 0 <= npcIndex && npcIndex < static_cast<int32>( _listPosition.size() );
        return AbsolutePosition{ bInRange ? _listPosition[static_cast<size_t>( npcIndex )] : float3{} };
    }

    NavAgentSettings FarmVillageGameModeComponent::makeBodySettings( int32 npcIndex ) const
    {
        const bool       bInRange = 0 <= npcIndex && npcIndex < static_cast<int32>( _listBodySpeed.size() );
        NavAgentSettings settings;
        settings._maxSpeed        = bInRange ? _listBodySpeed[static_cast<size_t>( npcIndex )] : 0.0f;
        settings._maxAcceleration = settings._maxSpeed * _bodyAccelerationPerSpeed;
        settings._slowDistance    = _bodySlowDistance;
        return settings;
    }

    void FarmVillageGameModeComponent::collectNeighborPositions( int32 npcIndex, vector<float3>& outListPosition ) const
    {
        outListPosition.clear();
        for ( int32 otherIndex = 0; otherIndex < static_cast<int32>( _listPosition.size() ); ++otherIndex )
        {
            if ( otherIndex != npcIndex )
                outListPosition.push_back( _listPosition[static_cast<size_t>( otherIndex )] );
        }
    }
} // namespace sw
