#include "pch.h"

#include "GameFramework/Base/AI/Director/AiDirector.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "GameFramework/Base/AI/Director/AiDirectorProfile.h"
#include "GameFramework/Base/World/WorldClock.h"

namespace sw
{
    SW_LOG_CALLER( "AiDirector" );

    namespace
    {
        struct AiDirectorInternal
        {
            static constexpr const utf8* kArrEventKindName[] = { "PhaseChanged", "Spawned", "Despawned", "Encounter", "Reward" };
            static constexpr const utf8* kArrBlockName[]     = { "weight", "pacing", "cooldown", "maxCount", "cycle", "minTime", "intensity", "area", "condition", "cost" };
            /** @brief 스폰 감독의 씨앗을 감독 씨앗에서 떼어 낼 때 섞는 값입니다(같은 씨앗이 두 수열에서 같은 수를 내지 않게). */
            static constexpr uint32 kSpawnSeedSalt = 0x5bd1e995u;

            static bool contains( const vector<hashed_string>& listName, const hashed_string& name )
            {
                for ( const hashed_string& entry : listName )
                {
                    if ( entry == name )
                        return true;
                }
                return false;
            }

            static bool containsAny( const vector<hashed_string>& listName, const vector<hashed_string>& listOther )
            {
                for ( const hashed_string& entry : listName )
                {
                    if ( contains( listOther, entry ) )
                        return true;
                }
                return false;
            }

            static uint64 hashBytes( uint64 hash, const void* pData, size_t size )
            {
                return StringUtil::computeHash64( static_cast<const utf8*>( pData ), size, false, hash );
            }

            template <typename T>
            static uint64 hashValue( uint64 hash, const T& value )
            {
                return hashBytes( hash, &value, sizeof( T ) );
            }

            static float32 identityWeight( float32 weight ) { return weight; }
        };
    } // namespace

    /** @brief `-gv_aiDirectorTrace=1` — 감독이 낸 일(단계 · 스폰 · 조우 · 보상)을 낼 때마다 로그에 한 줄씩 남깁니다(시험용 — 배포본에는 없다). */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_aiDirectorTrace, 0, "AiDirector: log every phase change, spawn, encounter and reward (1=on)" );
} // namespace sw

namespace sw
{
    void AiDirectorContext::fillFromClock( const WorldClock& clock )
    {
        _condition._day         = clock.getDay();
        _condition._dayOfSeason = clock.getDayOfSeason();
        _condition._season      = clock.getSeasonName();
        _condition._phase       = clock.getDayPhase();
    }

    const utf8* toString( AiDirectorEventKind kind )
    {
        return AiDirectorInternal::kArrEventKindName[static_cast<uint32>( kind )];
    }

    const utf8* AiDirectorBlock::getName( uint32 bitIndex )
    {
        return bitIndex < kCount ? AiDirectorInternal::kArrBlockName[bitIndex] : "?";
    }

    AiDirector::AiDirector()
        : _context{}
        , _builtinModel{}
        , _spawnDirector{}
        , _listPoolState{}
        , _eventBuffer{}
        , _listTrace{}
        , _listScratchWeight{}
        , _listScratchSpawnEvent{}
        , _pProfile{ nullptr }
        , _pSpawnTable{ nullptr }
        , _pCustomModel{ nullptr }
        , _random{}
        , _time{ 0.0f }
        , _phaseTime{ 0.0f }
        , _intensity{ 0.0f }
        , _seed{ GameRandom::kDefaultSeed }
        , _pickSerial{ 0 }
        , _phaseIndex{ -1 }
        , _cycle{ 0 }
        , _traceHead{ 0 }
    {
    }

    void AiDirector::initialize( const AiDirectorProfile* pProfile, const SpawnTable* pSpawnTable, uint32 seed )
    {
        _pProfile    = pProfile;
        _pSpawnTable = pSpawnTable;
        _seed        = seed;
        _builtinModel.initialize( pProfile != nullptr ? &pProfile->getIntensity() : nullptr );
        restart();
    }

    void AiDirector::restart()
    {
        _random.setSeed( _seed );
        _builtinModel.reset();
        _spawnDirector.initialize( _pSpawnTable, _seed ^ AiDirectorInternal::kSpawnSeedSalt );
        _eventBuffer.clear();
        _listTrace.clear();
        _traceHead  = 0;
        _time       = 0.0f;
        _phaseTime  = 0.0f;
        _intensity  = 0.0f;
        _pickSerial = 0;
        _phaseIndex = -1;
        _cycle      = 0;
        _listPoolState.clear();
        if ( _pProfile == nullptr || _pProfile->getPhases().empty() )
            return;
        for ( const AiDirectorPoolDef& pool : _pProfile->getPools() )
        {
            PoolState state;
            state._listEncounter.resize( pool._listEncounter.size() );
            _listPoolState.push_back( state );
        }
        enterPhase( MathUtil::max( 0, _pProfile->getStartPhaseIndex() ), -1 );
    }

    IAiDirectorIntensityModel& AiDirector::getModel()
    {
        if ( _pCustomModel != nullptr )
            return *_pCustomModel;
        return _builtinModel;
    }

    const IAiDirectorIntensityModel& AiDirector::getIntensityModel() const
    {
        if ( _pCustomModel != nullptr )
            return *_pCustomModel;
        return _builtinModel;
    }

    hashed_string AiDirector::getPhase() const
    {
        return _pProfile != nullptr && _phaseIndex >= 0 ? _pProfile->getPhases()[static_cast<size_t>( _phaseIndex )]._id : hashed_string{};
    }

    float32 AiDirector::getPoolBudget( int32 poolIndex ) const
    {
        return 0 <= poolIndex && poolIndex < static_cast<int32>( _listPoolState.size() ) ? _listPoolState[static_cast<size_t>( poolIndex )]._budget : 0.0f;
    }

    int32 AiDirector::findPoolIndex( const hashed_string& poolId ) const
    {
        if ( _pProfile == nullptr )
            return -1;
        const vector<AiDirectorPoolDef>& listPool = _pProfile->getPools();
        for ( size_t index = 0; index < listPool.size(); ++index )
        {
            if ( listPool[index]._id == poolId )
                return static_cast<int32>( index );
        }
        return -1;
    }

    void AiDirector::update( float32 deltaTime )
    {
        if ( _pProfile == nullptr || _phaseIndex < 0 || deltaTime < 0.0f )
            return;
        IAiDirectorIntensityModel& model = getModel();
        model.update( deltaTime );
        _time += deltaTime;
        _phaseTime += deltaTime;
        _intensity = model.getIntensity();

        for ( int32 transition = 0; transition < kMaxTransitionsPerUpdate; ++transition )
        {
            const int32 exitIndex = findSatisfiedExit();
            if ( exitIndex < 0 )
                break;
            const AiDirectorPhaseDef& phase = _pProfile->getPhases()[static_cast<size_t>( _phaseIndex )];
            enterPhase( phase._listExit[static_cast<size_t>( exitIndex )]._toIndex, exitIndex );
        }
        updateSpawns( deltaTime );
        updatePools( deltaTime );
    }

    int32 AiDirector::findSatisfiedExit() const
    {
        const AiDirectorPhaseDef&        phase       = _pProfile->getPhases()[static_cast<size_t>( _phaseIndex )];
        const float32                    calmSeconds = getIntensityModel().getCalmSeconds();
        const vector<AiDirectorExitDef>& listExit    = phase._listExit;
        for ( size_t exitIndex = 0; exitIndex < listExit.size(); ++exitIndex )
        {
            const AiDirectorExitDef& exit    = listExit[exitIndex];
            const bool               bTime   = exit._minTime <= _phaseTime;
            const bool               bAbove  = exit._intensityAbove < 0.0f || _intensity >= exit._intensityAbove;
            const bool               bBelow  = exit._intensityBelow < 0.0f || _intensity <= exit._intensityBelow;
            const bool               bCalm   = exit._calmFor <= calmSeconds;
            const bool               bTarget = exit._toIndex >= 0;
            if ( bTime && bAbove && bBelow && bCalm && bTarget )
                return static_cast<int32>( exitIndex );
        }
        return -1;
    }

    bool AiDirector::forcePhase( const hashed_string& phaseId )
    {
        const int32 phaseIndex = _pProfile != nullptr ? _pProfile->findPhaseIndex( phaseId ) : -1;
        if ( phaseIndex < 0 )
            return false;
        enterPhase( phaseIndex, -1 );
        return true;
    }

    void AiDirector::enterPhase( int32 phaseIndex, int32 exitIndex )
    {
        const vector<AiDirectorPhaseDef>& listPhase     = _pProfile->getPhases();
        const hashed_string               previousPhase = _phaseIndex >= 0 ? listPhase[static_cast<size_t>( _phaseIndex )]._id : hashed_string{};
        const bool                        bLoopedBack   = _phaseIndex >= 0 && phaseIndex == _pProfile->getStartPhaseIndex();
        _phaseIndex                                     = phaseIndex;
        _phaseTime                                      = 0.0f;
        _cycle += bLoopedBack ? 1 : 0;
        const AiDirectorPhaseDef& phase = listPhase[static_cast<size_t>( phaseIndex )];
        _spawnDirector.setAllowedTags( phase._listSpawnTag );

        AiDirectorEvent event;
        event._kind      = AiDirectorEventKind::PhaseChanged;
        event._id        = phase._id;
        event._source    = previousPhase;
        event._time      = _time;
        event._intensity = _intensity;
        event._detail    = exitIndex;
        event._count     = _cycle;
        pushEvent( event );

        // 들어선 단계를 기다리는 풀(호드 · 보급)은 이 자리에서 고른다.
        const vector<AiDirectorPoolDef>& listPool = _pProfile->getPools();
        for ( size_t poolIndex = 0; poolIndex < listPool.size(); ++poolIndex )
        {
            const AiDirectorPoolDef& pool = listPool[poolIndex];
            if ( pool._trigger != AiDirectorPoolTrigger::PhaseEnter || pool._phaseIndex != phaseIndex || isPoolActive( pool ) == false )
                continue;
            for ( int32 pick = 0; pick < pool._picks; ++pick )
            {
                if ( isPoolCooledDown( pool, _listPoolState[poolIndex] ) == false )
                    break;
                const int32 encounterIndex = pickEncounter( static_cast<int32>( poolIndex ) );
                if ( encounterIndex < 0 )
                    break;
                emitPick( static_cast<int32>( poolIndex ), encounterIndex );
            }
        }
    }

    void AiDirector::updateSpawns( float32 deltaTime )
    {
        if ( _pSpawnTable == nullptr )
            return;
        const AiDirectorPhaseDef& phase = _pProfile->getPhases()[static_cast<size_t>( _phaseIndex )];
        _spawnDirector.setBudgetScale( phase._spawnScale * phase._spawnCurve.evaluate( _phaseTime, 1.0f ) );
        (void)_spawnDirector.update( deltaTime );
        drainSpawnEvents();
    }

    bool AiDirector::notifyDespawned( uint32 spawnId )
    {
        const bool bKnown = _spawnDirector.notifyDespawned( spawnId );
        drainSpawnEvents();
        return bKnown;
    }

    void AiDirector::drainSpawnEvents()
    {
        _listScratchSpawnEvent.clear();
        _spawnDirector.drainEvents( _listScratchSpawnEvent );
        for ( const SpawnEvent& spawn : _listScratchSpawnEvent )
        {
            AiDirectorEvent event;
            event._kind      = spawn._kind == SpawnEvent::Kind::Spawned ? AiDirectorEventKind::Spawned : AiDirectorEventKind::Despawned;
            event._id        = spawn._entryId;
            event._source    = getPhase();
            event._time      = _time;
            event._intensity = _intensity;
            event._cost      = spawn._cost;
            event._spawnId   = spawn._spawnId;
            event._count     = 1;
            pushEvent( event );
        }
    }

    bool AiDirector::isPoolActive( const AiDirectorPoolDef& pool ) const
    {
        return pool._listPacing.empty() || AiDirectorInternal::contains( pool._listPacing, getPhase() );
    }

    bool AiDirector::isPoolCooledDown( const AiDirectorPoolDef& pool, const PoolState& state ) const
    {
        return state._lastPickTime < 0.0f || _time - state._lastPickTime >= pool._cooldown;
    }

    void AiDirector::updatePools( float32 deltaTime )
    {
        const vector<AiDirectorPoolDef>& listPool = _pProfile->getPools();
        for ( size_t poolIndex = 0; poolIndex < listPool.size(); ++poolIndex )
        {
            const AiDirectorPoolDef& pool = listPool[poolIndex];
            // 단계 밖의 풀은 시계 · 예산이 멈춘다 — 쉬는 동안 쌓인 것이 다음 쌓기 단계 첫 프레임에 몰려 나오지 않게.
            if ( isPoolActive( pool ) == false )
                continue;
            if ( pool._trigger == AiDirectorPoolTrigger::Budget )
            {
                updateBudgetPool( static_cast<int32>( poolIndex ), deltaTime );
                continue;
            }
            if ( pool._trigger != AiDirectorPoolTrigger::Interval )
                continue;
            PoolState& state = _listPoolState[poolIndex];
            state._timer += deltaTime;
            for ( int32 tick = 0; tick < kMaxPicksPerUpdate && state._timer >= pool._interval; ++tick )
            {
                state._timer -= pool._interval;
                if ( isPoolCooledDown( pool, state ) == false || _random.nextChance( pool._chance ) == false )
                    continue;
                const int32 encounterIndex = pickEncounter( static_cast<int32>( poolIndex ) );
                if ( encounterIndex >= 0 )
                    emitPick( static_cast<int32>( poolIndex ), encounterIndex );
            }
            state._timer = MathUtil::min( state._timer, pool._interval ); // 긴 멈춤 뒤 밀린 틱을 쌓아 두지 않는다
        }
    }

    void AiDirector::updateBudgetPool( int32 poolIndex, float32 deltaTime )
    {
        const AiDirectorPoolDef&  pool       = _pProfile->getPools()[static_cast<size_t>( poolIndex )];
        const AiDirectorPhaseDef& phase      = _pProfile->getPhases()[static_cast<size_t>( _phaseIndex )];
        PoolState&                state      = _listPoolState[static_cast<size_t>( poolIndex )];
        const float32             phaseScale = pool._kind == AiDirectorPoolKind::Reward ? phase._rewardScale : phase._spawnScale;
        const float32             need       = pool._needSignal.empty() ? 0.0f : getIntensityModel().getSignal( pool._needSignal );
        const float32             needScale  = MathUtil::max( 0.0f, 1.0f + pool._needScale * need );
        state._budget                        = MathUtil::min( pool._maxBudget, state._budget + pool._perMinute / 60.0f * phaseScale * needScale * deltaTime );

        for ( int32 pick = 0; pick < kMaxPicksPerUpdate; ++pick )
        {
            if ( isPoolCooledDown( pool, state ) == false )
                break;
            // 다음 것을 미리 골라 두고 예산이 닿으면 낸다 — 싼 것만 계속 나와 비싼 것이 영영 못 나오는 일이 없다(`SpawnDirector` 와 같은 규칙).
            if ( state._pendingIndex >= 0 && computeBlockMask( poolIndex, state._pendingIndex ) != 0 )
                state._pendingIndex = -1;
            if ( state._pendingIndex < 0 )
                state._pendingIndex = pickEncounter( poolIndex );
            if ( state._pendingIndex < 0 )
                break;
            const AiDirectorEncounterDef& encounter = pool._listEncounter[static_cast<size_t>( state._pendingIndex )];
            if ( state._budget < encounter._cost )
                break;
            state._budget -= encounter._cost;
            const int32 encounterIndex = state._pendingIndex;
            state._pendingIndex        = -1;
            emitPick( poolIndex, encounterIndex );
        }
    }

    uint32 AiDirector::computeChanceKey( int32 poolIndex, int32 encounterIndex ) const
    {
        const uint32 poolKey = GameHash::mix32( _seed ^ static_cast<uint32>( poolIndex ) * 0x9e3779b1u );
        return GameHash::mix32( poolKey ^ static_cast<uint32>( encounterIndex ) * 0x85ebca77u ^ _pickSerial * 0xc2b2ae35u );
    }

    uint32 AiDirector::computeBlockMask( int32 poolIndex, int32 encounterIndex ) const
    {
        if ( _pProfile == nullptr || poolIndex < 0 || poolIndex >= static_cast<int32>( _listPoolState.size() ) )
            return AiDirectorBlock::kWeight;
        const AiDirectorPoolDef& pool = _pProfile->getPools()[static_cast<size_t>( poolIndex )];
        if ( encounterIndex < 0 || encounterIndex >= static_cast<int32>( pool._listEncounter.size() ) )
            return AiDirectorBlock::kWeight;
        const AiDirectorEncounterDef& encounter = pool._listEncounter[static_cast<size_t>( encounterIndex )];
        const EncounterState&         state     = _listPoolState[static_cast<size_t>( poolIndex )]._listEncounter[static_cast<size_t>( encounterIndex )];

        uint32 mask = 0;
        if ( encounter._weight <= 0.0f )
            mask |= AiDirectorBlock::kWeight;
        if ( encounter._listPacing.empty() == false && AiDirectorInternal::contains( encounter._listPacing, getPhase() ) == false )
            mask |= AiDirectorBlock::kPacing;
        if ( state._lastTime >= 0.0f && _time - state._lastTime < encounter._cooldown )
            mask |= AiDirectorBlock::kCooldown;
        if ( encounter._maxCount >= 0 && state._count >= encounter._maxCount )
            mask |= AiDirectorBlock::kMaxCount;
        if ( _cycle < encounter._minCycle )
            mask |= AiDirectorBlock::kCycle;
        if ( _time < encounter._minTime )
            mask |= AiDirectorBlock::kTime;
        const bool bBelowMin = _intensity < encounter._minIntensity;
        const bool bAboveMax = encounter._maxIntensity >= 0.0f && _intensity > encounter._maxIntensity;
        if ( bBelowMin || bAboveMax )
            mask |= AiDirectorBlock::kIntensity;
        if ( encounter._listArea.empty() == false && AiDirectorInternal::containsAny( encounter._listArea, _context._listAreaTag ) == false )
            mask |= AiDirectorBlock::kArea;
        if ( encounter._condition.isEmpty() == false )
        {
            ScheduleConditionContext conditionContext = _context._condition;
            conditionContext._chanceKey               = computeChanceKey( poolIndex, encounterIndex );
            if ( encounter._condition.matches( conditionContext ) == false )
                mask |= AiDirectorBlock::kCondition;
        }
        if ( pool._trigger == AiDirectorPoolTrigger::Budget && encounter._cost > pool._maxBudget )
            mask |= AiDirectorBlock::kCost;
        return mask;
    }

    int32 AiDirector::pickEncounter( int32 poolIndex )
    {
        const AiDirectorPoolDef& pool = _pProfile->getPools()[static_cast<size_t>( poolIndex )];
        _listScratchWeight.clear();
        for ( size_t encounterIndex = 0; encounterIndex < pool._listEncounter.size(); ++encounterIndex )
        {
            const bool bEligible = computeBlockMask( poolIndex, static_cast<int32>( encounterIndex ) ) == 0;
            _listScratchWeight.push_back( bEligible ? pool._listEncounter[encounterIndex]._weight : 0.0f );
        }
        return _random.pickWeightedIndex( _listScratchWeight, &AiDirectorInternal::identityWeight );
    }

    void AiDirector::emitPick( int32 poolIndex, int32 encounterIndex )
    {
        const AiDirectorPoolDef&      pool      = _pProfile->getPools()[static_cast<size_t>( poolIndex )];
        const AiDirectorEncounterDef& encounter = pool._listEncounter[static_cast<size_t>( encounterIndex )];
        PoolState&                    poolState = _listPoolState[static_cast<size_t>( poolIndex )];
        EncounterState&               state     = poolState._listEncounter[static_cast<size_t>( encounterIndex )];
        state._lastTime                         = _time;
        ++state._count;
        poolState._lastPickTime = _time;
        ++_pickSerial;

        AiDirectorEvent event;
        event._kind      = pool._kind == AiDirectorPoolKind::Reward ? AiDirectorEventKind::Reward : AiDirectorEventKind::Encounter;
        event._id        = encounter._id;
        event._source    = pool._id;
        event._time      = _time;
        event._intensity = _intensity;
        event._scale     = encounter._scale;
        event._cost      = pool._trigger == AiDirectorPoolTrigger::Budget ? encounter._cost : 0.0f;
        event._count     = encounter._count;
        pushEvent( event );
    }

    void AiDirector::pushEvent( const AiDirectorEvent& event )
    {
        _eventBuffer.push( event );
        if ( static_cast<int32>( _listTrace.size() ) < kMaxTraceEvent )
        {
            _listTrace.push_back( event );
        }
        else
        {
            _listTrace[static_cast<size_t>( _traceHead )] = event;
            _traceHead                                    = ( _traceHead + 1 ) % kMaxTraceEvent;
        }
        if ( gv_aiDirectorTrace != 0 )
        {
            SW_LOG_INFO( "[AiDirector] t=%.2f I=%.2f %# '%#' (%#) count %# scale %.2f", event._time, event._intensity, toString( event._kind ), event._id.c_str(),
                         event._source.c_str(), event._count, event._scale );
        }
    }

    void AiDirector::drainEvents( vector<AiDirectorEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }

    void AiDirector::explain( string& outText ) const
    {
        outText.clear();
        if ( _pProfile == nullptr || _phaseIndex < 0 )
        {
            outText = "director: not initialized\n";
            return;
        }
        const AiDirectorPhaseDef&               phase = _pProfile->getPhases()[static_cast<size_t>( _phaseIndex )];
        StringBuilder<constant::kMaxBuffer4096> text;
        text.appendFormat( "director t=%.2fs phase '%#' (%.2fs) intensity %.2f calm %.2fs cycle %#\n", _time, phase._id.c_str(), _phaseTime, _intensity,
                           getIntensityModel().getCalmSeconds(), _cycle );
        if ( _pSpawnTable != nullptr )
        {
            text.appendFormat( "  spawn: budget %.2f scale %.2f alive %# pending '%#'\n", _spawnDirector.getBudget(), _spawnDirector.getBudgetScale(),
                               _spawnDirector.getTotalAliveCount(), _spawnDirector.getPendingEntry().c_str() );
        }
        const float32 calmSeconds = getIntensityModel().getCalmSeconds();
        for ( size_t exitIndex = 0; exitIndex < phase._listExit.size(); ++exitIndex )
        {
            const AiDirectorExitDef& exit = phase._listExit[exitIndex];
            text.appendFormat( "  exit #%# -> '%#':", exitIndex, exit._to.c_str() );
            bool bBlocked = false;
            if ( _phaseTime < exit._minTime )
            {
                text.appendFormat( " [minTime %.2f]", exit._minTime );
                bBlocked = true;
            }
            if ( exit._intensityAbove >= 0.0f && _intensity < exit._intensityAbove )
            {
                text.appendFormat( " [intensityAbove %.2f]", exit._intensityAbove );
                bBlocked = true;
            }
            if ( exit._intensityBelow >= 0.0f && _intensity > exit._intensityBelow )
            {
                text.appendFormat( " [intensityBelow %.2f]", exit._intensityBelow );
                bBlocked = true;
            }
            if ( calmSeconds < exit._calmFor )
            {
                text.appendFormat( " [calmFor %.2f]", exit._calmFor );
                bBlocked = true;
            }
            text.append( bBlocked ? "\n" : " open\n" );
        }
        const vector<AiDirectorPoolDef>& listPool = _pProfile->getPools();
        for ( size_t poolIndex = 0; poolIndex < listPool.size(); ++poolIndex )
        {
            const AiDirectorPoolDef& pool  = listPool[poolIndex];
            const PoolState&         state = _listPoolState[poolIndex];
            text.appendFormat( "  pool '%#' (%#, %#)%#%# budget %.2f timer %.2f\n", pool._id.c_str(), toString( pool._kind ), toString( pool._trigger ),
                               isPoolActive( pool ) ? "" : " [pacing]", isPoolCooledDown( pool, state ) ? "" : " [cooldown]", state._budget, state._timer );
            for ( size_t encounterIndex = 0; encounterIndex < pool._listEncounter.size(); ++encounterIndex )
            {
                const AiDirectorEncounterDef& encounter = pool._listEncounter[encounterIndex];
                const uint32                  mask      = computeBlockMask( static_cast<int32>( poolIndex ), static_cast<int32>( encounterIndex ) );
                text.appendFormat( "    %# %# w%.2f picked %#", mask == 0 ? "+" : "-", encounter._id.c_str(), encounter._weight,
                                   state._listEncounter[encounterIndex]._count );
                for ( uint32 bitIndex = 0; bitIndex < AiDirectorBlock::kCount; ++bitIndex )
                {
                    if ( ( mask & ( 1u << bitIndex ) ) != 0 )
                        text.appendFormat( " [%#]", AiDirectorBlock::getName( bitIndex ) );
                }
                text.append( "\n" );
            }
        }
        outText = text.c_str();
    }

    void AiDirector::dumpTrace( string& outText ) const
    {
        outText.clear();
        const size_t count = _listTrace.size();
        for ( size_t offset = 0; offset < count; ++offset )
        {
            const AiDirectorEvent&                 event = _listTrace[( static_cast<size_t>( _traceHead ) + offset ) % count];
            StringBuilder<constant::kMaxBuffer256> line;
            line.appendFormat( "[%.2fs] I=%.2f %# '%#'", event._time, event._intensity, toString( event._kind ), event._id.c_str() );
            if ( event._source.empty() == false )
                line.appendFormat( " from '%#'", event._source.c_str() );
            if ( event._kind == AiDirectorEventKind::PhaseChanged )
                line.appendFormat( " exit %# cycle %#", event._detail, event._count );
            else
                line.appendFormat( " count %# scale %.2f", event._count, event._scale );
            if ( event._spawnId != 0 )
                line.appendFormat( " spawn #%#", event._spawnId );
            line.append( "\n" );
            outText += line.c_str();
        }
    }

    uint64 AiDirector::computeStateHash() const
    {
        using Internal = AiDirectorInternal;
        uint64 hash    = StringUtil::kOffset64;
        hash           = Internal::hashValue( hash, _time );
        hash           = Internal::hashValue( hash, _phaseTime );
        hash           = Internal::hashValue( hash, _intensity );
        hash           = Internal::hashValue( hash, _phaseIndex );
        hash           = Internal::hashValue( hash, _cycle );
        hash           = Internal::hashValue( hash, _pickSerial );
        hash           = Internal::hashValue( hash, _random.getState() );
        hash           = Internal::hashValue( hash, _spawnDirector.getBudget() );
        hash           = Internal::hashValue( hash, _spawnDirector.getTotalAliveCount() );
        hash           = Internal::hashValue( hash, _spawnDirector.getPendingEntry().getHash() );
        for ( const PoolState& state : _listPoolState )
        {
            hash = Internal::hashValue( hash, state._timer );
            hash = Internal::hashValue( hash, state._budget );
            hash = Internal::hashValue( hash, state._lastPickTime );
            hash = Internal::hashValue( hash, state._pendingIndex );
            for ( const EncounterState& encounter : state._listEncounter )
            {
                hash = Internal::hashValue( hash, encounter._lastTime );
                hash = Internal::hashValue( hash, encounter._count );
            }
        }
        return hash;
    }
} // namespace sw
