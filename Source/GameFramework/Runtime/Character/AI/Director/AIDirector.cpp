#include "pch.h"

#include "GameFramework/Runtime/Character/AI/Director/AIDirector.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Reflection/EnumName.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Character/AI/Director/AIDirectorProfile.h"
#include "GameFramework/Runtime/World/Environment/WorldClock.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct AIDirectorInternal
        {
            static constexpr const utf8* kArrBlockName[] = { "weight", "pacing", "cooldown", "maxCount", "cycle", "minTime", "intensity", "area", "condition", "cost" };
            /** @brief 스폰 감독의 씨앗을 감독 씨앗에서 떼어 낼 때 섞는 값입니다(같은 씨앗이 두 수열에서 같은 수를 내지 않게). */
            static constexpr uint32 kSpawnSeedSalt  = 0x5bd1e995u;
            static constexpr uint32 kStateTag       = FourCCUtil::make( "AIDR" );
            static constexpr uint32 kStateVersion   = 1;
            static constexpr uint32 kPoolMinBytes   = 20; ///< 풀 하나의 최소 바이트(타이머 · 예산 · 마지막 고른 시각 · 골라 둔 것 · 항목 수)
            static constexpr uint32 kEncounterBytes = 8;  ///< 항목 하나(마지막 시각 · 횟수)

            template <typename T>
            static uint64 hashValue( uint64 hash, const T& value )
            {
                return HashUtil::appendBytes64( hash, &value, sizeof( T ) );
            }

            static float32 identityWeight( float32 weight ) { return weight; }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void AIDirectorContext::fillFromClock( const WorldClock& clock )
    {
        _condition._day         = clock.getDay();
        _condition._dayOfSeason = clock.getDayOfSeason();
        _condition._season      = clock.getSeasonName();
        _condition._phase       = clock.getDayPhase();
    }

    const utf8* AIDirectorBlock::getName( uint32 bitIndex )
    {
        return bitIndex < kCount ? AIDirectorInternal::kArrBlockName[bitIndex] : "?";
    }

    AIDirector::AIDirector()
        : _context{}
        , _builtinModel{}
        , _spawnDirector{}
        , _listPoolState{}
        , _eventBuffer{}
        , _listTrace{}
        , _listScratchWeight{}
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

    {
    }

    void AIDirector::initialize( const AIDirectorProfile* pProfile, const SpawnTable* pSpawnTable, uint32 seed )
    {
        _pProfile    = pProfile;
        _pSpawnTable = pSpawnTable;
        _seed        = seed;
        _builtinModel.initialize( pProfile != nullptr ? &pProfile->getIntensity() : nullptr );
        restart();
    }

    void AIDirector::restart()
    {
        _random.setSeed( _seed );
        _builtinModel.reset();
        _spawnDirector.initialize( _pSpawnTable, _seed ^ AIDirectorInternal::kSpawnSeedSalt );
        _eventBuffer.clear();
        _listTrace.clear();
        _time       = 0.0f;
        _phaseTime  = 0.0f;
        _intensity  = 0.0f;
        _pickSerial = 0;
        _phaseIndex = -1;
        _cycle      = 0;
        _listPoolState.clear();
        if ( _pProfile == nullptr || _pProfile->getPhases().empty() )
            return;
        for ( const AIDirectorPoolDef& pool : _pProfile->getPools() )
        {
            PoolState state;
            state._listEncounter.resize( pool._listEncounter.size() );
            _listPoolState.push_back( state );
        }
        enterPhase( MathUtil::max( 0, _pProfile->getStartPhaseIndex() ), -1 );
    }

    IAIDirectorIntensityModel& AIDirector::getModel()
    {
        if ( _pCustomModel != nullptr )
            return *_pCustomModel;
        return _builtinModel;
    }

    const IAIDirectorIntensityModel& AIDirector::getIntensityModel() const
    {
        if ( _pCustomModel != nullptr )
            return *_pCustomModel;
        return _builtinModel;
    }

    hashed_string AIDirector::getPhase() const
    {
        return _pProfile != nullptr && _phaseIndex >= 0 ? _pProfile->getPhases()[static_cast<size_t>( _phaseIndex )]._id : hashed_string{};
    }

    float32 AIDirector::getPoolBudget( int32 poolIndex ) const
    {
        return 0 <= poolIndex && poolIndex < static_cast<int32>( _listPoolState.size() ) ? _listPoolState[static_cast<size_t>( poolIndex )]._budget : 0.0f;
    }

    optional<int32> AIDirector::findPoolIndex( const hashed_string& poolID ) const
    {
        if ( _pProfile == nullptr )
            return nullopt;
        const vector<AIDirectorPoolDef>& listPool = _pProfile->getPools();
        for ( size_t index = 0; index < listPool.size(); ++index )
        {
            if ( listPool[index]._id == poolID )
                return static_cast<int32>( index );
        }
        return nullopt;
    }

    void AIDirector::update( float32 deltaTime )
    {
        const bool bCannotUpdate = _pProfile == nullptr || _phaseIndex < 0 || deltaTime < 0.0f;
        if ( bCannotUpdate )
            return;
        IAIDirectorIntensityModel& model = getModel();
        model.update( deltaTime );
        _time += deltaTime;
        _phaseTime += deltaTime;
        _intensity = model.getIntensity();

        for ( int32 transition = 0; transition < kMaxTransitionsPerUpdate; ++transition )
        {
            const int32 exitIndex = findSatisfiedExit();
            if ( exitIndex < 0 )
                break;
            const AIDirectorPhaseDef& phase = _pProfile->getPhases()[static_cast<size_t>( _phaseIndex )];
            enterPhase( phase._listExit[static_cast<size_t>( exitIndex )]._toIndex, exitIndex );
        }
        updateSpawns( deltaTime );
        updatePools( deltaTime );
    }

    int32 AIDirector::findSatisfiedExit() const
    {
        const AIDirectorPhaseDef&        phase       = _pProfile->getPhases()[static_cast<size_t>( _phaseIndex )];
        const float32                    calmSeconds = getIntensityModel().getCalmSeconds();
        const vector<AIDirectorExitDef>& listExit    = phase._listExit;
        for ( size_t exitIndex = 0; exitIndex < listExit.size(); ++exitIndex )
        {
            const AIDirectorExitDef& exit               = listExit[exitIndex];
            const bool               bTime              = exit._minTime <= _phaseTime;
            const bool               bAbove             = exit._intensityAbove < 0.0f || _intensity >= exit._intensityAbove;
            const bool               bBelow             = exit._intensityBelow < 0.0f || _intensity <= exit._intensityBelow;
            const bool               bCalm              = exit._calmFor <= calmSeconds;
            const bool               bTarget            = exit._toIndex >= 0;
            const bool               bExitConditionsMet = bTime && bAbove && bBelow && bCalm && bTarget;
            if ( bExitConditionsMet )
                return static_cast<int32>( exitIndex );
        }
        return -1;
    }

    bool AIDirector::forcePhase( const hashed_string& phaseID )
    {
        const optional<int32> phaseIndex = _pProfile != nullptr ? _pProfile->findPhaseIndex( phaseID ) : optional<int32>{};
        if ( phaseIndex.has_value() == false )
            return false;
        enterPhase( *phaseIndex, -1 );
        return true;
    }

    void AIDirector::enterPhase( int32 phaseIndex, int32 exitIndex )
    {
        const vector<AIDirectorPhaseDef>& listPhase     = _pProfile->getPhases();
        const hashed_string               previousPhase = _phaseIndex >= 0 ? listPhase[static_cast<size_t>( _phaseIndex )]._id : hashed_string{};
        const bool                        bLoopedBack   = _phaseIndex >= 0 && phaseIndex == _pProfile->getStartPhaseIndex();
        _phaseIndex                                     = phaseIndex;
        _phaseTime                                      = 0.0f;
        _cycle += bLoopedBack ? 1 : 0;
        const AIDirectorPhaseDef& phase = listPhase[static_cast<size_t>( phaseIndex )];
        _spawnDirector.setAllowedTags( phase._listSpawnTag );

        AIDirectorEvent event;
        event._kind      = AIDirectorEventKind::PhaseChanged;
        event._id        = phase._id;
        event._source    = previousPhase;
        event._time      = _time;
        event._intensity = _intensity;
        event._detail    = exitIndex;
        event._count     = _cycle;
        pushEvent( event );

        // 들어선 단계를 기다리는 풀(호드 · 보급)은 이 자리에서 고른다.
        const vector<AIDirectorPoolDef>& listPool = _pProfile->getPools();
        for ( size_t poolIndex = 0; poolIndex < listPool.size(); ++poolIndex )
        {
            const AIDirectorPoolDef& pool               = listPool[poolIndex];
            const bool               bNotPhaseEnterPool = pool._trigger != AIDirectorPoolTrigger::PhaseEnter || pool._phaseIndex != phaseIndex || isPoolActive( pool ) == false;
            if ( bNotPhaseEnterPool )
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

    void AIDirector::updateSpawns( float32 deltaTime )
    {
        if ( _pSpawnTable == nullptr )
            return;
        const AIDirectorPhaseDef& phase = _pProfile->getPhases()[static_cast<size_t>( _phaseIndex )];
        // 단계 곡선이 없으면 단계의 배율만 쓴다(`AIDirectorPhaseDef::_spawnCurve` 의 계약 — 곡선은 배율에 곱하는 선택 항이다).
        const auto curveScale = phase._spawnCurve.evaluate( _phaseTime );
        _spawnDirector.setBudgetScale( curveScale.has_value() ? phase._spawnScale * *curveScale : phase._spawnScale );
        (void)_spawnDirector.update( deltaTime );
        pollSpawnEvents();
    }

    bool AIDirector::notifyDespawned( uint32 spawnID )
    {
        const bool bKnown = _spawnDirector.notifyDespawned( spawnID );
        pollSpawnEvents();
        return bKnown;
    }

    void AIDirector::pollSpawnEvents()
    {
        for ( const SpawnEvent& spawn : _spawnDirector.takeEvents() )
        {
            AIDirectorEvent event;
            event._kind      = spawn._kind == SpawnEvent::Kind::Spawned ? AIDirectorEventKind::Spawned : AIDirectorEventKind::Despawned;
            event._id        = spawn._entryID;
            event._source    = getPhase();
            event._time      = _time;
            event._intensity = _intensity;
            event._cost      = spawn._cost;
            event._spawnID   = spawn._spawnID;
            event._count     = 1;
            pushEvent( event );
        }
    }

    bool AIDirector::isPoolActive( const AIDirectorPoolDef& pool ) const
    {
        return pool._listPacing.empty() || VectorUtil::contains( pool._listPacing, getPhase() );
    }

    bool AIDirector::isPoolCooledDown( const AIDirectorPoolDef& pool, const PoolState& state ) const
    {
        return state._lastPickTime < 0.0f || _time - state._lastPickTime >= pool._cooldown;
    }

    void AIDirector::updatePools( float32 deltaTime )
    {
        const vector<AIDirectorPoolDef>& listPool = _pProfile->getPools();
        for ( size_t poolIndex = 0; poolIndex < listPool.size(); ++poolIndex )
        {
            const AIDirectorPoolDef& pool = listPool[poolIndex];
            // 단계 밖의 풀은 시계 · 예산이 멈춘다 — 쉬는 동안 쌓인 것이 다음 쌓기 단계 첫 프레임에 몰려 나오지 않게.
            if ( isPoolActive( pool ) == false )
                continue;
            if ( pool._trigger == AIDirectorPoolTrigger::Budget )
            {
                updateBudgetPool( static_cast<int32>( poolIndex ), deltaTime );
                continue;
            }
            if ( pool._trigger != AIDirectorPoolTrigger::Interval )
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

    void AIDirector::updateBudgetPool( int32 poolIndex, float32 deltaTime )
    {
        const AIDirectorPoolDef&  pool       = _pProfile->getPools()[static_cast<size_t>( poolIndex )];
        const AIDirectorPhaseDef& phase      = _pProfile->getPhases()[static_cast<size_t>( _phaseIndex )];
        PoolState&                state      = _listPoolState[static_cast<size_t>( poolIndex )];
        const float32             phaseScale = pool._kind == AIDirectorPoolKind::Reward ? phase._rewardScale : phase._spawnScale;
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
            const AIDirectorEncounterDef& encounter = pool._listEncounter[static_cast<size_t>( state._pendingIndex )];
            if ( state._budget < encounter._cost )
                break;
            state._budget -= encounter._cost;
            const int32 encounterIndex = state._pendingIndex;
            state._pendingIndex        = -1;
            emitPick( poolIndex, encounterIndex );
        }
    }

    uint32 AIDirector::computeChanceKey( int32 poolIndex, int32 encounterIndex ) const
    {
        const uint32 poolKey = HashUtil::mix32( _seed ^ static_cast<uint32>( poolIndex ) * 0x9e3779b1u );
        return HashUtil::mix32( poolKey ^ static_cast<uint32>( encounterIndex ) * 0x85ebca77u ^ _pickSerial * 0xc2b2ae35u );
    }

    uint32 AIDirector::computeBlockMask( int32 poolIndex, int32 encounterIndex ) const
    {
        const bool bInvalidPoolIndex = _pProfile == nullptr || poolIndex < 0 || poolIndex >= static_cast<int32>( _listPoolState.size() );
        if ( bInvalidPoolIndex )
            return AIDirectorBlock::kWeight;
        const AIDirectorPoolDef& pool = _pProfile->getPools()[static_cast<size_t>( poolIndex )];
        if ( encounterIndex < 0 || encounterIndex >= static_cast<int32>( pool._listEncounter.size() ) )
            return AIDirectorBlock::kWeight;
        const AIDirectorEncounterDef& encounter = pool._listEncounter[static_cast<size_t>( encounterIndex )];
        const EncounterState&         state     = _listPoolState[static_cast<size_t>( poolIndex )]._listEncounter[static_cast<size_t>( encounterIndex )];

        uint32 mask = 0;
        if ( encounter._weight <= 0.0f )
            mask |= AIDirectorBlock::kWeight;
        if ( encounter._listPacing.empty() == false && VectorUtil::contains( encounter._listPacing, getPhase() ) == false )
            mask |= AIDirectorBlock::kPacing;
        if ( state._lastTime >= 0.0f && _time - state._lastTime < encounter._cooldown )
            mask |= AIDirectorBlock::kCooldown;
        if ( encounter.canTriggerAgain( state._count ) == false )
            mask |= AIDirectorBlock::kMaxCount;
        if ( _cycle < encounter._minCycle )
            mask |= AIDirectorBlock::kCycle;
        if ( _time < encounter._minTime )
            mask |= AIDirectorBlock::kTime;
        const bool bBelowMin = _intensity < encounter._minIntensity;
        const bool bAboveMax = encounter.isAboveMaxIntensity( _intensity );
        if ( bBelowMin || bAboveMax )
            mask |= AIDirectorBlock::kIntensity;
        if ( encounter._listArea.empty() == false && VectorUtil::containsIf( encounter._listArea, [this]( const hashed_string& area )
        { return VectorUtil::contains( _context._listAreaTag, area ); } ) == false )
            mask |= AIDirectorBlock::kArea;
        if ( encounter._condition.isEmpty() == false )
        {
            ScheduleConditionContext conditionContext = _context._condition;
            conditionContext._chanceKey               = computeChanceKey( poolIndex, encounterIndex );
            if ( encounter._condition.matches( conditionContext ) == false )
                mask |= AIDirectorBlock::kCondition;
        }
        if ( pool._trigger == AIDirectorPoolTrigger::Budget && encounter._cost > pool._maxBudget )
            mask |= AIDirectorBlock::kCost;
        return mask;
    }

    int32 AIDirector::pickEncounter( int32 poolIndex )
    {
        const AIDirectorPoolDef& pool = _pProfile->getPools()[static_cast<size_t>( poolIndex )];
        _listScratchWeight.clear();
        for ( size_t encounterIndex = 0; encounterIndex < pool._listEncounter.size(); ++encounterIndex )
        {
            const bool bEligible = computeBlockMask( poolIndex, static_cast<int32>( encounterIndex ) ) == 0;
            _listScratchWeight.push_back( bEligible ? pool._listEncounter[encounterIndex]._weight : 0.0f );
        }
        return _random.pickWeightedIndex( _listScratchWeight, &AIDirectorInternal::identityWeight );
    }

    void AIDirector::emitPick( int32 poolIndex, int32 encounterIndex )
    {
        const AIDirectorPoolDef&      pool      = _pProfile->getPools()[static_cast<size_t>( poolIndex )];
        const AIDirectorEncounterDef& encounter = pool._listEncounter[static_cast<size_t>( encounterIndex )];
        PoolState&                    poolState = _listPoolState[static_cast<size_t>( poolIndex )];
        EncounterState&               state     = poolState._listEncounter[static_cast<size_t>( encounterIndex )];
        state._lastTime                         = _time;
        ++state._count;
        poolState._lastPickTime = _time;
        ++_pickSerial;

        AIDirectorEvent event;
        event._kind      = pool._kind == AIDirectorPoolKind::Reward ? AIDirectorEventKind::Reward : AIDirectorEventKind::Encounter;
        event._id        = encounter._id;
        event._source    = pool._id;
        event._time      = _time;
        event._intensity = _intensity;
        event._scale     = encounter._scale;
        event._cost      = pool._trigger == AIDirectorPoolTrigger::Budget ? encounter._cost : 0.0f;
        event._count     = encounter._count;
        pushEvent( event );
    }

    void AIDirector::pushEvent( const AIDirectorEvent& event )
    {
        _eventBuffer.push( event );
        _listTrace.push( event ); // 최근 사건은 `explain` 이 보여 준다
    }

    void AIDirector::pollEvents( vector<AIDirectorEvent>& outListEvent )
    {
        _eventBuffer.moveAllTo( outListEvent );
    }

    void AIDirector::explain( string& outText ) const
    {
        outText.clear();
        if ( _pProfile == nullptr || _phaseIndex < 0 )
        {
            outText = "director: not initialized\n";
            return;
        }
        const AIDirectorPhaseDef&                 phase = _pProfile->getPhases()[static_cast<size_t>( _phaseIndex )];
        StringBuilder<constant::buffer::kMax4096> text;
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
            const AIDirectorExitDef& exit = phase._listExit[exitIndex];
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
        const vector<AIDirectorPoolDef>& listPool = _pProfile->getPools();
        for ( size_t poolIndex = 0; poolIndex < listPool.size(); ++poolIndex )
        {
            const AIDirectorPoolDef& pool  = listPool[poolIndex];
            const PoolState&         state = _listPoolState[poolIndex];
            text.appendFormat( "  pool '%#' (%#, %#)%#%# budget %.2f timer %.2f\n", pool._id.c_str(), getEnumName( pool._kind ), getEnumName( pool._trigger ),
                               isPoolActive( pool ) ? "" : " [pacing]", isPoolCooledDown( pool, state ) ? "" : " [cooldown]", state._budget, state._timer );
            for ( size_t encounterIndex = 0; encounterIndex < pool._listEncounter.size(); ++encounterIndex )
            {
                const AIDirectorEncounterDef& encounter = pool._listEncounter[encounterIndex];
                const uint32                  mask      = computeBlockMask( static_cast<int32>( poolIndex ), static_cast<int32>( encounterIndex ) );
                text.appendFormat( "    %# %# w%.2f picked %#", mask == 0 ? "+" : "-", encounter._id.c_str(), encounter._weight,
                                   state._listEncounter[encounterIndex]._count );
                for ( uint32 bitIndex = 0; bitIndex < AIDirectorBlock::kCount; ++bitIndex )
                {
                    if ( ( mask & ( 1u << bitIndex ) ) != 0 )
                        text.appendFormat( " [%#]", AIDirectorBlock::getName( bitIndex ) );
                }
                text.append( "\n" );
            }
        }
        outText = text.c_str();
    }

    void AIDirector::dumpTrace( string& outText ) const
    {
        outText.clear();
        for ( const AIDirectorEvent& event : _listTrace )
        {
            StringBuilder<constant::buffer::kMax256> line;
            line.appendFormat( "[%.2fs] I=%.2f %# '%#'", event._time, event._intensity, getEnumName( event._kind ), event._id.c_str() );
            if ( event._source.empty() == false )
                line.appendFormat( " from '%#'", event._source.c_str() );
            if ( event._kind == AIDirectorEventKind::PhaseChanged )
                line.appendFormat( " exit %# cycle %#", event._detail, event._count );
            else
                line.appendFormat( " count %# scale %.2f", event._count, event._scale );
            if ( event._spawnID != 0 )
                line.appendFormat( " spawn #%#", event._spawnID );
            line.append( "\n" );
            outText += line.c_str();
        }
    }

    uint64 AIDirector::computeStateHash() const
    {
        using Internal = AIDirectorInternal;
        uint64 hash    = HashUtil::kFNVOffset64;
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

    void AIDirector::serializeState( StateIO& io )
    {
        using Internal = AIDirectorInternal;
        if ( io.check( _pProfile != nullptr ) == false )
            return;
        io.header( Internal::kStateTag, Internal::kStateVersion );
        if ( io.isOk() == false )
            return;
        int32 phaseCount = _pProfile != nullptr ? static_cast<int32>( _pProfile->getPhases().size() ) : 0;
        io.value( phaseCount );
        io.value( _time );
        io.value( _phaseTime );
        io.value( _intensity );
        io.value( _phaseIndex );
        io.value( _cycle );
        io.value( _pickSerial );
        io.value( _random );
        uint32 poolCount = static_cast<uint32>( _listPoolState.size() );
        io.count( Internal::kPoolMinBytes, poolCount );
        if ( io.isOk() == false )
            return;
        if ( io.isReading() )
        {
            const bool bShape = phaseCount == static_cast<int32>( _pProfile->getPhases().size() ) && 0 <= _phaseIndex && _phaseIndex < phaseCount && poolCount == _pProfile->getPools().size();
            const bool bClock = _time >= 0.0f && _phaseTime >= 0.0f && _intensity >= 0.0f && _cycle >= 0;
            if ( io.check( bShape && bClock ) == false )
                return;
            _listPoolState.clear();
            _listPoolState.resize( poolCount );
        }
        for ( size_t poolIndex = 0; poolIndex < _listPoolState.size(); ++poolIndex )
        {
            PoolState& state = _listPoolState[poolIndex];
            io.value( state._timer );
            io.value( state._budget );
            io.value( state._lastPickTime );
            io.value( state._pendingIndex );
            uint32 encounterCount = static_cast<uint32>( state._listEncounter.size() );
            io.count( Internal::kEncounterBytes, encounterCount );
            if ( io.isOk() == false )
                return;
            if ( io.isReading() )
            {
                if ( io.check( encounterCount == _pProfile->getPools()[poolIndex]._listEncounter.size() ) == false )
                    return;
                state._listEncounter.resize( encounterCount );
            }
            for ( EncounterState& encounter : state._listEncounter )
            {
                io.value( encounter._lastTime );
                io.value( encounter._count );
                io.check( 0 <= encounter._count );
            }
            if ( io.isReading() && io.check( state._timer >= 0.0f && state._budget >= 0.0f && state._budget <= _pProfile->getPools()[poolIndex]._maxBudget && state._pendingIndex >= -1 && state._pendingIndex < static_cast<int32>( encounterCount ) ) == false )
                return;
        }
        // 태그 거르기는 단계가 정한다(싣지 않았다) — 읽기 전에 걸어야 실린 골라 둔 것을 그대로 받는다(뒤에 걸면 상한에 걸린 것을 비워 원본과 갈린다).
        if ( io.isReading() )
        {
            _spawnDirector.setAllowedTags( _pProfile->getPhases()[static_cast<size_t>( _phaseIndex )]._listSpawnTag );
            _eventBuffer.clear();
            _listTrace.clear();
        }
        io.value( _builtinModel );
        io.value( _spawnDirector );
    }

    bool AIDirector::rebindProfile( const AIDirectorProfile* pProfile )
    {
        if ( pProfile == nullptr || _pProfile == nullptr )
            return false;
        Archive saved;
        writeState( saved );
        const AIDirectorProfile* pOldProfile = _pProfile;
        initialize( pProfile, _pSpawnTable, _seed );
        Archive reading( saved.getData(), saved.getSize() );
        if ( readState( reading ) )
            return true;
        // 구조가 달라 번호를 이을 수 없다 — 옛 프로필로 되돌린다. 같은 프로필이 쓴 바이트라 다시 읽힌다.
        initialize( pOldProfile, _pSpawnTable, _seed );
        Archive    restoring( saved.getData(), saved.getSize() );
        const bool bRestored = readState( restoring );
        SW_ASSERT( bRestored );
        (void)bRestored;
        return false;
    }

} // namespace sw
