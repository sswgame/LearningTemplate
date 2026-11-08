#include "pch.h"

#include "GameFramework/Kits/Genre/Action/MechArena/MechArenaWorld.h"

#include "Core/Foundation/Constants.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/Ray.h"
#include "Core/Serialization/BitStream.h"

#include "Engine/Net/NetMessageChannel.h"
#include "Engine/Serialization/Fixed/FixedLayout.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Action/MechArena/MechArenaSnapshot.h"
#include "GameFramework/Runtime/Character/Combat/Projectile/ResolvedShot.h"
#include "GameFramework/Runtime/Foundation/GameData/ConfigRangeUtil.h"
#include "GameFramework/Runtime/Foundation/Random/GameRandom.h"
#include "GameFramework/Runtime/Gameplay/Match/TeamAttitude.h"

namespace sw
{
    namespace
    {
        uint32 mixMechArenaSeed( uint32 seed, int32 pilot, int32 slot )
        {
            return HashUtil::mix32( seed ^ HashUtil::mix32( static_cast<uint32>( pilot ) * 0x9E3779B1u + static_cast<uint32>( slot ) ) );
        }

        /** @brief 기체 정의에서 기반 `HealthStateMachine` 설정을 만듭니다(범위는 `HealthStateMachine::initialize` 가 본다). */
        HealthSettings makeMechHealthMachine( const MechDef& mech )
        {
            HealthSettings healthMachine;
            healthMachine._maxHealth          = mech._maxHealth;
            healthMachine._poiseMax           = mech._downMax;
            healthMachine._poiseRegenDelay    = mech._downRecoveryDelay;
            healthMachine._poiseRegenRate     = mech._downRecovery;
            healthMachine._poiseBreakDuration = mech._downTime;
            healthMachine._bDownedEnabled     = SW_FALSE;
            return healthMachine;
        }

        /** @brief 기체 정의에서 부스트(과열형 `ResourceGauge`) 설정을 만듭니다(범위는 `ResourceGauge::initialize` 가 본다). */
        ResourceGaugeSettings makeMechBoost( const MechDef& mech )
        {
            ResourceGaugeSettings boost;
            boost._max                  = mech._boostMax;
            boost._regenRate            = mech._boostRegen;
            boost._regenDelay           = mech._boostRegenDelay;
            boost._drainPerSecond       = mech._hoverPerSecond;
            boost._overheatCooldown     = mech._overheatPenalty;
            boost._overheatRecoverLevel = mech._overheatRecover;
            boost._bOverheatMode        = SW_TRUE;
            return boost;
        }

        struct MechArenaWorldInternal
        {
            static constexpr float32 kTiny = 1.0e-5f;

            /** @brief XZ 평면에서 길이 1로 만듭니다. 너무 짧으면 @p fallback 을 반환합니다. */
            static float3 flattenDirection( const float3& direction, const float3& fallback )
            {
                const float3  flat{ direction._x, 0.0f, direction._z };
                const float32 length = flat.getLength();
                return length > kTiny ? flat / length : fallback;
            }

            static const hashed_string& getMeleeName()
            {
                static const hashed_string name( "melee" );
                return name;
            }

            static const hashed_string& getShotName()
            {
                static const hashed_string name( "shot" );
                return name;
            }

            static const hashed_string& getDownName()
            {
                static const hashed_string name( "down" );
                return name;
            }
            static const hashed_string& getAttackName()
            {
                static const hashed_string name( "attack" );
                return name;
            }
            static const hashed_string& getDefenseName()
            {
                static const hashed_string name( "defense" );
                return name;
            }
            static const hashed_string& getDownResistName()
            {
                static const hashed_string name( "downResist" );
                return name;
            }

            static void serializeInput( StateIO& io, MechInput& input )
            {
                io.value( input._moveX );
                io.value( input._moveZ );
                io.value( input._skillSlot );
                io.flag( input._bDash );
                io.flag( input._bJump );
                io.flag( input._bHover );
                io.flag( input._bFire );
                io.flag( input._bMelee );
                io.flag( input._bSpecial );
                io.flag( input._bTransform );
                io.flag( input._bLockOn );
            }

            /** @brief 덱과 스킬 목록입니다. 정의는 이름으로 카탈로그에서 다시 찾습니다. */
            static void serializeLoadout( StateIO& io, const MechCatalog* pCatalog, MechPilot& pilot )
            {
                uint32 deckCount = static_cast<uint32>( pilot._listDeckMech.size() );
                io.count( 4, deckCount );
                if ( io.check( deckCount != 0 ) == false )
                    return;
                for ( uint32 index = 0; index < deckCount; ++index )
                {
                    hashed_string mechID = io.isWriting() ? pilot._listDeckMech[index]->_id : hashed_string{};
                    io.value( mechID );
                    if ( io.isWriting() )
                        continue;
                    const MechDef* pMech = pCatalog->findMech( mechID );
                    if ( io.check( pMech != nullptr && pMech->_listMode.empty() == false ) == false )
                        return;
                    pilot._listDeckMech.push_back( pMech );
                }
                uint32 skillCount = static_cast<uint32>( pilot._listSkill.size() );
                io.count( 4, skillCount );
                if ( io.isOk() == false )
                    return;
                for ( uint32 index = 0; index < skillCount; ++index )
                {
                    hashed_string skillID = io.isWriting() ? pilot._listSkill[index]->_id : hashed_string{};
                    io.value( skillID );
                    if ( io.isWriting() )
                        continue;
                    const MechSkillDef* pSkill = pCatalog->findSkill( skillID );
                    if ( io.check( pSkill != nullptr ) == false )
                        return;
                    pilot._listSkill.push_back( pSkill );
                }
                if ( io.isReading() )
                {
                    pilot._listSkillRemaining.resize( skillCount );
                    pilot._listSkillCooldown.resize( skillCount );
                    pilot._listSkillSpent.resize( skillCount, SW_FALSE );
                }
            }

            /**
             * @brief 조종사의 덱(기체 id)과 스킬 id 를 카탈로그에서 찾아 채우고, 스킬 수만큼 타이머 슬롯을 잡습니다.
             * @return 덱이 비었거나 카탈로그에 없는 기체 · 스킬이면 false 입니다.
             */
            /**
             * @brief 읽은 근접 상태가 지금 기체와 맞는지 봅니다. 콤보 중이면 근접 타임라인이 돌고, 슬롯이 지금 형태의 근접 슬롯이고, 단이 그 슬롯의 기술 수 안이어야 합니다.
             * @details `stepMelee` 가 `_meleeSlot` 으로 지금 형태의 무기 목록을 그대로 찾는다. 이 값이 어긋나면 목록 밖을 읽는다.
             */
            [[nodiscard]] static bool isMeleeStateValid( const MechPilot& pilot )
            {
                if ( pilot._comboStage < 0 )
                    return pilot._meleeSlot == constant::invalid_index::kInt32 && pilot._melee.isPlaying() == false;
                const MechDef*                   pMech      = pilot.getMech();
                const vector<MechWeaponSlotDef>& listWeapon = pMech->_listMode[static_cast<size_t>( pilot._mode )]._listWeapon;
                const int32                      slotInMode = pilot._meleeSlot - pMech->computeSlotOffset( pilot._mode );
                if ( pilot._melee.isPlaying() == false || slotInMode < 0 || slotInMode >= static_cast<int32>( listWeapon.size() ) )
                    return false;
                const MechWeaponSlotDef& slot = listWeapon[static_cast<size_t>( slotInMode )];
                return slot._kind == MechWeaponKind::Melee && pilot._comboStage < static_cast<int32>( slot._listMoveID.size() );
            }

            /** @brief 읽은 조종사 상태와 체력 상태가 맞는지 봅니다. 나와 있으면 살아 있고, 격추됐으면 죽어 있습니다(판이 끝나 내려간 조종사와 출격 전은 어느 쪽이든 됩니다). */
            [[nodiscard]] static bool isHealthMachineFittingState( const MechPilot& pilot )
            {
                if ( pilot._state == MechPilotState::Active || pilot._state == MechPilotState::Down )
                    return pilot._healthMachine.isAlive();
                return pilot._state != MechPilotState::Destroyed || pilot._healthMachine.isDead();
            }

            static bool isPressed( uint8 current, uint8 previous ) { return current == SW_TRUE && previous == SW_FALSE; }

            static const hashed_string& getBoostRegenName()
            {
                static const hashed_string name( "boostRegen" );
                return name;
            }
            static const hashed_string& getSpeedName()
            {
                static const hashed_string name( "speed" );
                return name;
            }
        };
    } // namespace

    /**
     * @class MechArenaProjectileQuery
     * @brief 판의 탄이 묻는 세계입니다. 유도 대상은 그 조종사의 가운데이고, 맞는 것은 쏜 쪽의 적 조종사(구)와 판 밖(표면)입니다.
     */
    class MechArenaProjectileQuery : public IProjectileWorld
    {
    public:
        explicit MechArenaProjectileQuery( const MechArenaWorld& world )
            : _world{ world }
        {
        }

        bool findTargetPoint( const ProjectileState& projectile, float3& outPoint ) const override
        {
            if ( _world.isTargetable( projectile._target ) == false )
                return false;
            outPoint = _world.computeCenter( _world._listPilot[static_cast<size_t>( projectile._target )] );
            return true;
        }

        bool sweep( const ProjectileState& projectile, const float3& from, const float3& to, ProjectileHit& outHit ) const override
        {
            const float3  segment = to - from;
            const float32 length  = segment.getLength();
            if ( length > MechArenaWorldInternal::kTiny )
            {
                const int32 ownerTeam = _world.isValidPilot( projectile._owner ) ? _world._listPilot[static_cast<size_t>( projectile._owner )]._team : TeamAttitudeUtil::kNoTeam;
                const Ray   ray{ from, segment / length };
                int32       hitPilot    = constant::invalid_index::kInt32;
                float32     hitDistance = length;
                for ( int32 other = 0; other < _world.getPilotCount(); ++other )
                {
                    const MechPilot& candidate = _world._listPilot[static_cast<size_t>( other )];
                    float32          distance  = 0.0f;
                    if ( other == projectile._lastHit || _world.isEnemyTarget( ownerTeam, other ) == false )
                        continue;
                    if ( Ray::intersectSphere( ray, _world.computeCenter( candidate ), candidate.getMech()->_radius, hitDistance, distance ) && distance <= hitDistance )
                    {
                        hitDistance = distance;
                        hitPilot    = other;
                    }
                }
                if ( hitPilot >= 0 )
                {
                    outHit._position = from + ray._direction * hitDistance;
                    outHit._other    = hitPilot;
                    outHit._kind     = ProjectileHitKind::Target;
                    return true;
                }
            }
            const MechArenaSettings& settings = _world._settings;
            const bool               bOutside = MathUtil::abs( to._x ) > settings._arenaHalfSize || MathUtil::abs( to._z ) > settings._arenaHalfSize || to._y < 0.0f || to._y > settings._ceiling;
            if ( bOutside == false )
                return false;
            // 판 밖은 표면이다 — 튕기는 탄이 아니면 여기서 끝난다(판 밖 탄은 스냅샷 양자화 범위 밖이다).
            outHit._position = to;
            outHit._kind     = ProjectileHitKind::Surface;
            return true;
        }

    private:
        const MechArenaWorld& _world;
    };

    const MechModeDef* MechPilot::getMode() const
    {
        const MechDef* pMech = getMech();
        if ( pMech == nullptr || pMech->_listMode.empty() )
            return nullptr;
        return &pMech->_listMode[static_cast<size_t>( MathUtil::clamp( _mode, 0, static_cast<int32>( pMech->_listMode.size() ) - 1 ) )];
    }

    MechArenaWorld::MechArenaWorld()
        : _settings{}
        , _listPilot{}
        , _projectiles{}
        , _listTeamName{}
        , _eventBuffer{}
        , _listCandidateScratch{}
        , _match{}
        , _timer{}
        , _pMechCatalog{ nullptr }
        , _pWeaponCatalog{ nullptr }
        , _pProjectileCatalog{ nullptr }
        , _pMoveCatalog{ nullptr }
        , _tick{ 0 }
        , _bStarted{ SW_FALSE }
        , _bEndReported{ SW_FALSE }
    {
    }

    bool MechArenaWorld::initialize( const MechArenaSettings& settings, const MechCatalog* pMechCatalog, const WeaponCatalog* pWeaponCatalog,
                                     const ProjectileCatalog* pProjectileCatalog, const MoveCatalog* pMoveCatalog )
    {
        const string rangeError = ConfigRangeUtil::describeRangeErrors( settings );
        if ( rangeError.empty() == false )
        {
            SW_LOG_ERROR_ID( "MechArenaWorld.MechArenaWorldRefusedSettings", "MechArenaWorld: refused settings - %#", rangeError.c_str() );
            return false;
        }
        // 무기 → 탄은 다른 파일의 연결이라 참조 표가 보지 못한다 — 둘을 함께 쥐는 이 월드가 시작할 때 본다(Shooter3DGame 과 같은 정책, 깨지면 서지 않는다).
        if ( pWeaponCatalog != nullptr && pProjectileCatalog != nullptr && pWeaponCatalog->checkProjectiles( *pProjectileCatalog ) == false )
            return false; // 오류는 checkProjectiles 가 남겼다
        _settings           = settings;
        _pMechCatalog       = pMechCatalog;
        _pWeaponCatalog     = pWeaponCatalog;
        _pProjectileCatalog = pProjectileCatalog;
        _pMoveCatalog       = pMoveCatalog;
        _listPilot.clear();
        _projectiles = ProjectileSimulation{};
        _listTeamName.clear();
        _eventBuffer.clear();
        _match.initialize( MatchSettings{} );
        _timer        = FixedStepTimer( _settings._fixedStep, constant::time::kDefaultMaxFrameSeconds );
        _tick         = 0;
        _bStarted     = SW_FALSE;
        _bEndReported = SW_FALSE;
        return true;
    }

    int32 MechArenaWorld::addTeam( const hashed_string& name )
    {
        _listTeamName.push_back( name );
        return static_cast<int32>( _listTeamName.size() ) - 1;
    }

    int32 MechArenaWorld::addPilot( const MechPilotConfig& config )
    {
        const bool bPilotRejected = _bStarted == SW_TRUE || _pMechCatalog == nullptr || config._listMechID.empty() || config._team < 0 ||
                                    config._team >= static_cast<int32>( _listTeamName.size() );
        if ( bPilotRejected )
            return constant::invalid_index::kInt32;
        MechPilot pilot;
        int32     deckCost = 0;
        for ( const hashed_string& mechID : config._listMechID )
        {
            const MechDef* pMech = _pMechCatalog->findMech( mechID );
            if ( pMech == nullptr || pMech->_listMode.empty() || isMechDataComplete( *pMech ) == false )
                return constant::invalid_index::kInt32;
            // 기체를 바꿀 때마다 체력과 부스트를 다시 짜므로(equipMech) 덱의 모든 기체가 지금 범위 안이어야 한다(거절 사유는 각 initialize 가 남긴다).
            HealthStateMachine probeHealthMachine;
            ResourceGauge      probeBoost;
            if ( probeHealthMachine.initialize( makeMechHealthMachine( *pMech ) ) == false || probeBoost.initialize( makeMechBoost( *pMech ) ) == false )
                return constant::invalid_index::kInt32;
            deckCost += pMech->_cost;
            pilot._listDeckMech.push_back( pMech );
        }
        if ( _pMechCatalog->getDeckCostLimit() > 0 && deckCost > _pMechCatalog->getDeckCostLimit() )
            return constant::invalid_index::kInt32;
        for ( const hashed_string& skillID : config._listSkillID )
        {
            const MechSkillDef* pSkill = _pMechCatalog->findSkill( skillID );
            if ( pSkill == nullptr )
                return constant::invalid_index::kInt32; // 오타 하나로 스킬 슬롯이 말없이 당겨지지 않게 거절한다(상태 읽기도 없는 스킬을 거절한다)
            pilot._listSkill.push_back( pSkill );
        }
        pilot._listSkillRemaining.resize( pilot._listSkill.size() );
        pilot._listSkillCooldown.resize( pilot._listSkill.size() );
        pilot._listSkillSpent.resize( pilot._listSkill.size(), SW_FALSE );
        pilot._team          = config._team;
        pilot._spawnPosition = config._spawnPosition;
        pilot._position      = config._spawnPosition;
        _listPilot.push_back( pilot );
        const int32 index = static_cast<int32>( _listPilot.size() ) - 1;
        if ( equipMech( _listPilot.back(), index ) == false )
        {
            _listPilot.pop_back();
            return constant::invalid_index::kInt32;
        }
        return index;
    }

    void MechArenaWorld::start()
    {
        if ( _bStarted == SW_TRUE )
            return;
        _match.initialize( makeMatchSettings() );
        for ( size_t team = 0; team < _listTeamName.size(); ++team )
        {
            int32 gauge = _settings._teamGauge;
            if ( gauge <= 0 )
            {
                int32 costSum = 0;
                for ( const MechPilot& pilot : _listPilot )
                {
                    costSum += pilot._team == static_cast<int32>( team ) ? pilot._listDeckMech.front()->_cost : 0;
                }
                gauge = static_cast<int32>( MathUtil::round( static_cast<float32>( costSum ) * _settings._gaugeScale ) );
            }
            (void)_match.addTeam( _listTeamName[team], gauge );
        }
        for ( MechPilot& pilot : _listPilot )
        {
            pilot._listParticipant.clear();
            for ( const MechDef* pMech : pilot._listDeckMech )
            {
                pilot._listParticipant.push_back( _match.addParticipant( pilot._team, pMech->_id, pMech->_cost ) );
            }
        }
        _match.start();
        _bStarted = SW_TRUE;
        for ( int32 pilot = 0; pilot < getPilotCount(); ++pilot )
        {
            spawnPilot( pilot, false );
        }
    }

    void MechArenaWorld::update( float32 deltaTime )
    {
        if ( _bStarted == SW_FALSE )
            return;
        const int32 stepCount = _timer.consume( deltaTime );
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            step( _timer.getStep() );
        }
    }

    void MechArenaWorld::setInput( int32 pilot, const MechInput& input )
    {
        if ( isValidPilot( pilot ) )
            _listPilot[static_cast<size_t>( pilot )]._input = input;
    }

    MechSwapResult MechArenaWorld::requestMechSwap( int32 pilot, int32 deckIndex )
    {
        if ( isValidPilot( pilot ) == false )
            return MechSwapResult::InvalidSlot;
        MechPilot& target = _listPilot[static_cast<size_t>( pilot )];
        if ( target._state != MechPilotState::Destroyed )
            return MechSwapResult::NotDestroyed;
        if ( deckIndex < 0 || deckIndex >= static_cast<int32>( target._listDeckMech.size() ) )
            return MechSwapResult::InvalidSlot;
        const MatchTeam* pTeam         = _match.findTeam( target._team );
        const bool       bSwapOverCost = pTeam != nullptr && pTeam->_bIgnoreCost == SW_FALSE && target._listDeckMech[static_cast<size_t>( deckIndex )]->_cost > pTeam->_costPool;
        if ( bSwapOverCost )
            return MechSwapResult::OverCost;
        // 장착은 출격할 때 바뀐다. 여기서 `_deckIndex` 를 바꾸면 스냅샷이 새 기체의 슬롯 번호로 옛 기체의 무기 목록을 읽어 목록 밖을 읽었다.
        target._nextDeckIndex = deckIndex;
        pushEvent( MechArenaEvent::Kind::MechSwapped, pilot, constant::invalid_index::kInt32, static_cast<float32>( deckIndex ), target._listDeckMech[static_cast<size_t>( deckIndex )]->_id );
        return MechSwapResult::Ok;
    }

    void MechArenaWorld::applyDamage( int32 attacker, int32 victim, float32 damage, float32 downValue, float32 staggerTime )
    {
        resolveHit( attacker, victim, damage, downValue, 0.0f, staggerTime, false, MechWeaponKind::Shot );
    }

    void MechArenaWorld::teleport( int32 pilot, const float3& position )
    {
        if ( isValidPilot( pilot ) == false )
            return;
        MechPilot& target = _listPilot[static_cast<size_t>( pilot )];
        target._position  = position;
        target._velocity  = float3{};
        target._bGrounded = position._y <= 0.0f ? SW_TRUE : SW_FALSE;
    }

    int32 MechArenaWorld::getTeamGauge( int32 team ) const
    {
        const MatchTeam* pTeam = _match.findTeam( team );
        if ( pTeam == nullptr )
            return 0;
        return pTeam->_bIgnoreCost == SW_TRUE ? kUnlimitedGauge : pTeam->_costPool;
    }

    float32 MechArenaWorld::computeModifier( int32 pilot, const hashed_string& name ) const
    {
        if ( isValidPilot( pilot ) == false )
            return 1.0f;
        const MechPilot& target = _listPilot[static_cast<size_t>( pilot )];
        float32          scale  = 1.0f;
        const int32      count  = countActiveSkills( target );
        for ( int32 index = 0; index < count; ++index )
        {
            if ( target._listSkillRemaining[static_cast<size_t>( index )].isActive() )
                scale *= target._listSkill[static_cast<size_t>( index )]->_modifier.getScale( name );
        }
        return scale;
    }

    void MechArenaWorld::pollEvents( vector<MechArenaEvent>& outListEvent )
    {
        _eventBuffer.moveAllTo( outListEvent );
    }

    // --- 걸음 -------------------------------------------------------------------------------------

    void MechArenaWorld::step( float32 deltaTime )
    {
        ++_tick;
        if ( isEnded() == false )
        {
            for ( int32 pilot = 0; pilot < getPilotCount(); ++pilot )
            {
                stepPilot( pilot, deltaTime );
            }
            stepProjectiles( deltaTime );
        }
        stepMatch( deltaTime );
    }

    void MechArenaWorld::stepMatch( float32 deltaTime )
    {
        _match.update( deltaTime );
        for ( const MatchEvent& event : _match.takeEvents() )
        {
            if ( event._kind == MatchEvent::Kind::Respawned )
            {
                for ( int32 pilot = 0; pilot < getPilotCount(); ++pilot )
                {
                    const MechPilot& target         = _listPilot[static_cast<size_t>( pilot )];
                    const bool       bWaitingOnThis = target._state == MechPilotState::Destroyed && target._listParticipant[static_cast<size_t>( target._deckIndex )] == event._participant;
                    if ( bWaitingOnThis )
                        spawnPilot( pilot, true );
                }
            }
            else if ( event._kind == MatchEvent::Kind::TeamEliminated )
            {
                // 셋 이상의 판에서 게이지가 바닥난 팀은 판이 끝나지 않아도 진 것이다. 내리지 않으면 격추된 조종사가 오지 않을 부활을 기다리고 남은 조종사는 계속 싸웠다.
                for ( MechPilot& pilot : _listPilot )
                {
                    if ( pilot._team == event._team )
                        retirePilot( pilot );
                }
            }
        }
        if ( isEnded() && _bEndReported == SW_FALSE )
        {
            _bEndReported = SW_TRUE;
            for ( MechPilot& pilot : _listPilot )
            {
                retirePilot( pilot );
            }
            _projectiles.clear();
            pushEvent( MechArenaEvent::Kind::MatchEnded, constant::invalid_index::kInt32, _match.getWinningTeam(), 0.0f, hashed_string{} );
        }
    }

    void MechArenaWorld::retirePilot( MechPilot& pilot )
    {
        pilot._state = MechPilotState::Retired;
        pilot._melee.cancel();
        pilot._comboStage   = constant::invalid_index::kInt32;
        pilot._meleeSlot    = constant::invalid_index::kInt32;
        pilot._bMeleeQueued = SW_FALSE;
    }

    bool MechArenaWorld::isMechDataComplete( const MechDef& mech ) const
    {
        if ( mech.computeSlotCount() >= static_cast<int32>( kSlotStride ) )
        {
            SW_LOG_ERROR_ID( "MechArenaWorld.MechTooManySlots", "Mech '%#' has %# weapon slots - at most %# fit the projectile payload", mech._id.c_str(), mech.computeSlotCount(),
                             kSlotStride - 1 );
            return false;
        }
        for ( const MechModeDef& mode : mech._listMode )
        {
            for ( const MechWeaponSlotDef& slot : mode._listWeapon )
            {
                bool bComplete = true;
                if ( slot._kind == MechWeaponKind::Shot )
                {
                    // 주무기는 무기와 그 기본 탄이 둘 다 있고, 무기가 그 탄을 받아야 쏠 수 있다.
                    const WeaponDef*     pWeapon     = _pWeaponCatalog != nullptr ? _pWeaponCatalog->find( slot._weaponID ) : nullptr;
                    const ProjectileDef* pProjectile = pWeapon != nullptr && _pProjectileCatalog != nullptr ? _pProjectileCatalog->find( pWeapon->_projectileID ) : nullptr;
                    bComplete                        = pProjectile != nullptr && pWeapon->accepts( *pProjectile );
                }
                if ( slot._kind == MechWeaponKind::Special )
                {
                    // 특수기의 탄은 사거리가 있어야 한다 — 광선은 사거리 안을 맞히고, 날아가는 탄은 판 밖에 닿기 전에 끝난다(상태 읽기도 사거리 없는 탄을 받지 않는다).
                    const ProjectileDef* pProjectile = _pProjectileCatalog != nullptr ? _pProjectileCatalog->find( slot._projectileID ) : nullptr;
                    bComplete                        = pProjectile != nullptr && pProjectile->_range > 0.0f;
                }
                if ( slot._kind == MechWeaponKind::Melee )
                {
                    bComplete = _pMoveCatalog != nullptr && slot._listMoveID.empty() == false;
                    for ( size_t stage = 0; bComplete && stage < slot._listMoveID.size(); ++stage )
                    {
                        bComplete = _pMoveCatalog->find( slot._listMoveID[stage] ) != nullptr;
                    }
                }
                if ( bComplete )
                    continue;
                SW_LOG_ERROR_ID( "MechArenaWorld.MechSlotMissingData", "Mech '%#' slot '%#' points at a weapon, projectile or move that is not in the catalogs", mech._id.c_str(), slot._id.c_str() );
                return false;
            }
        }
        return true;
    }

    bool MechArenaWorld::equipMech( MechPilot& pilot, int32 pilotIndex )
    {
        const MechDef* pMech = pilot.getMech();
        if ( pMech == nullptr )
            return true;
        if ( isMechDataComplete( *pMech ) == false )
            return false;
        if ( pilot._healthMachine.initialize( makeMechHealthMachine( *pMech ) ) == false )
            return false;

        if ( pilot._boost.initialize( makeMechBoost( *pMech ) ) == false )
            return false;
        pilot._bWasOverheated = SW_FALSE;

        LockOnSettings lockOn;
        lockOn._maxDistance   = pMech->_lockOnRange;
        lockOn._breakDistance = pMech->_lockOnRange * 1.2f;
        lockOn._maxAngle      = MathUtil::toDegree( pMech->_lockOnAngle ); // 기반 LockOnSelector 는 도 단위다
        pilot._lockOn.setSettings( lockOn );
        pilot._lockOn.release();

        const int32 slotCount = pMech->computeSlotCount();
        pilot._listWeaponState.clear();
        pilot._listWeaponState.resize( static_cast<size_t>( slotCount ) );
        pilot._listSpecialCooldown.assign( static_cast<size_t>( slotCount ), Countdown{} );
        int32 slotIndex = 0;
        for ( const MechModeDef& mode : pMech->_listMode )
        {
            for ( const MechWeaponSlotDef& slot : mode._listWeapon )
            {
                // 탄과 받음은 `isMechDataComplete` 가 위에서 봤다.
                const WeaponDef*     pWeapon     = _pWeaponCatalog != nullptr && slot._kind == MechWeaponKind::Shot ? _pWeaponCatalog->find( slot._weaponID ) : nullptr;
                const ProjectileDef* pProjectile = pWeapon != nullptr ? _pProjectileCatalog->find( pWeapon->_projectileID ) : nullptr;
                const bool           bEquipped   = pProjectile == nullptr || pilot._listWeaponState[static_cast<size_t>( slotIndex )].equip(
                                                                     *pWeapon, *pProjectile, pWeapon->_maxReserveAmmo, mixMechArenaSeed( _settings._seed, pilotIndex, slotIndex ) );
                if ( bEquipped == false )
                    return false;
                ++slotIndex;
            }
        }
        pilot._mode = 0;
        for ( size_t index = 0; index < pilot._listSkill.size(); ++index )
        {
            pilot._listSkillRemaining[index].clear();
            pilot._listSkillSpent[index] = SW_FALSE;
        }
        return true;
    }

    void MechArenaWorld::spawnPilot( int32 pilotIndex, bool bRespawn )
    {
        MechPilot& pilot = _listPilot[static_cast<size_t>( pilotIndex )];
        pilot._deckIndex = pilot._nextDeckIndex;
        if ( equipMech( pilot, pilotIndex ) == false )
        {
            // 덱의 모든 기체를 `addPilot` 이 범위 안인지 봤으므로 여기에 닿지 않는다. 닿으면 그 조종사는 출격시키지 않는다(옛 기체로 대신 뜨지 않는다).
            pilot._state = MechPilotState::Retired;
            return;
        }
        pilot._position = pilot._spawnPosition;
        pilot._velocity = float3{};
        pilot._forward  = float3{ 0.0f, 0.0f, 1.0f };
        pilot._dashRemaining.clear();
        pilot._staggerRemaining.clear();
        pilot._transformCooldown.clear();
        pilot._comboStage   = constant::invalid_index::kInt32;
        pilot._meleeSlot    = constant::invalid_index::kInt32;
        pilot._bMeleeQueued = SW_FALSE;
        pilot._bGrounded    = pilot._position._y <= 0.0f ? SW_TRUE : SW_FALSE;
        pilot._melee.cancel();
        pilot._state = MechPilotState::Active;
        if ( bRespawn == false )
            return;
        pilot._healthMachine.setInvulnerable( _settings._respawnInvulnerable );
        pushEvent( MechArenaEvent::Kind::Respawned, pilotIndex, constant::invalid_index::kInt32, static_cast<float32>( pilot._deckIndex ), pilot.getMech()->_id );
        triggerSkills( pilotIndex, MechSkillTrigger::OnRespawn );
    }

    // --- 조회 도우미 ------------------------------------------------------------------------------

    void MechArenaWorld::collectCandidates( int32 pilotIndex, vector<LockOnCandidate>& outListCandidate ) const
    {
        outListCandidate.clear();
        const int32 team = _listPilot[static_cast<size_t>( pilotIndex )]._team;
        for ( int32 other = 0; other < getPilotCount(); ++other )
        {
            const MechPilot& candidate = _listPilot[static_cast<size_t>( other )];
            if ( isEnemyTarget( team, other ) == false )
                continue;
            LockOnCandidate entry;
            entry._position = computeCenter( candidate );
            entry._id       = static_cast<uint64>( other + 1 );
            outListCandidate.push_back( entry );
        }
    }

    int32 MechArenaWorld::findLockTarget( int32 pilotIndex )
    {
        MechPilot& pilot = _listPilot[static_cast<size_t>( pilotIndex )];
        if ( pilot._lockOn.hasTarget() == false )
        {
            // 록온 없이 쏘면 앞쪽의 가장 좋은 대상을 잡는다(캡슐파이터의 자동 록온).
            collectCandidates( pilotIndex, _listCandidateScratch );
            (void)pilot._lockOn.pickBest( computeCenter( pilot ), pilot._forward, _listCandidateScratch );
        }
        if ( pilot._lockOn.hasTarget() == false )
            return -1;
        const int32 target = static_cast<int32>( pilot._lockOn.getTarget() ) - 1;
        if ( isTargetable( target ) == false )
        {
            pilot._lockOn.release();
            return -1;
        }
        return target;
    }

    int32 MechArenaWorld::countActiveSkills( const MechPilot& pilot ) const
    {
        const MechDef* pMech = pilot.getMech();
        return pMech != nullptr ? MathUtil::min( static_cast<int32>( pilot._listSkill.size() ), pMech->_skillSlots ) : 0;
    }

    int32 MechArenaWorld::findSlot( const MechPilot& pilot, MechWeaponKind kind ) const
    {
        const MechModeDef* pMode = pilot.getMode();
        if ( pMode == nullptr )
            return constant::invalid_index::kInt32;
        const int32 offset = pilot.getMech()->computeSlotOffset( pilot._mode );
        for ( size_t index = 0; index < pMode->_listWeapon.size(); ++index )
        {
            if ( pMode->_listWeapon[index]._kind == kind )
                return offset + static_cast<int32>( index );
        }
        return constant::invalid_index::kInt32;
    }

    bool MechArenaWorld::isTargetable( int32 pilot ) const
    {
        if ( isValidPilot( pilot ) == false )
            return false;
        const MechPilotState state = _listPilot[static_cast<size_t>( pilot )]._state;
        return state == MechPilotState::Active || state == MechPilotState::Down;
    }

    bool MechArenaWorld::isEnemyTarget( int32 team, int32 other ) const
    {
        return isTargetable( other ) && TeamAttitudeUtil::isHostile( team, _listPilot[static_cast<size_t>( other )]._team );
    }

    float3 MechArenaWorld::computeCenter( const MechPilot& pilot ) const
    {
        const MechDef* pMech = pilot.getMech();
        return pilot._position + float3{ 0.0f, pMech != nullptr ? pMech->_radius : 1.0f, 0.0f };
    }

    void MechArenaWorld::pushEvent( MechArenaEvent::Kind kind, int32 pilot, int32 other, float32 value, const hashed_string& id )
    {
        MechArenaEvent event;
        event._kind  = kind;
        event._pilot = pilot;
        event._other = other;
        event._value = value;
        event._id    = id;
        _eventBuffer.push( event );
    }

    // --- 무기와 피해 ------------------------------------------------------------------------------

    void MechArenaWorld::fireShot( int32 pilotIndex, int32 slotIndex, const MechWeaponSlotDef& slot, bool bPressed )
    {
        MechPilot&   pilot  = _listPilot[static_cast<size_t>( pilotIndex )];
        WeaponState& weapon = pilot._listWeaponState[static_cast<size_t>( slotIndex )];
        if ( weapon.getDef()._id.empty() )
            return;
        const int32  target    = findLockTarget( pilotIndex );
        const float3 origin    = computeCenter( pilot );
        const float3 direction = target >= 0 ? ( computeCenter( _listPilot[static_cast<size_t>( target )] ) - origin ).normalizeOr( pilot._forward, MechArenaWorldInternal::kTiny )
                                             : pilot._forward;
        WeaponShot   shot;
        if ( weapon.pullTrigger( Ray{ origin, direction }, bPressed, shot ) != WeaponFireResult::Fired )
            return;
        pushEvent( MechArenaEvent::Kind::Fired, pilotIndex, target, 0.0f, slot._id );
        // 탄 값은 장전한 조합(무기 + 탄)이고, 유도는 그 탄의 유도에 슬롯의 유도를 더한다.
        for ( const Ray& ray : shot._listRay )
        {
            ProjectileState projectile = ProjectileState::launch( weapon.getShot(), origin, ray._direction, _settings._gravity );
            projectile._turnRate += MathUtil::toRadian( slot._homing );
            projectile._owner  = pilotIndex;
            projectile._team   = pilot._team;
            projectile._target = target;
            launch( pilotIndex, slotIndex, ray._direction, projectile, slot );
        }
    }

    void MechArenaWorld::fireSpecial( int32 pilotIndex, int32 slotIndex, const MechWeaponSlotDef& slot )
    {
        MechPilot& pilot    = _listPilot[static_cast<size_t>( pilotIndex )];
        Countdown& cooldown = pilot._listSpecialCooldown[static_cast<size_t>( slotIndex )];
        if ( cooldown.isActive() )
            return;
        cooldown.start( slot._cooldown );
        const int32  target    = findLockTarget( pilotIndex );
        const float3 origin    = computeCenter( pilot );
        const float3 direction = target >= 0 ? ( computeCenter( _listPilot[static_cast<size_t>( target )] ) - origin ).normalizeOr( pilot._forward, MechArenaWorldInternal::kTiny )
                                             : pilot._forward;
        pushEvent( MechArenaEvent::Kind::Fired, pilotIndex, target, 0.0f, slot._id );
        // 탄 값은 슬롯이 가리키는 탄 정의다(주무기와 같은 탄 표). 탄이 있는지는 `isMechDataComplete` 가 장착 때 봤다.
        // 수정자가 없어 탄 값 그대로라 쏠 때 푼다(쿨다운이 있어 드물다). 유도는 주무기처럼 탄의 유도에 슬롯의 유도를 더한다.
        const ProjectileDef& def        = *_pProjectileCatalog->find( slot._projectileID );
        ProjectileState      projectile = ProjectileState::launch( ResolvedShot::resolve( def, {}, {} ), origin, direction, _settings._gravity );
        projectile._turnRate += MathUtil::toRadian( slot._homing );
        projectile._owner  = pilotIndex;
        projectile._team   = pilot._team;
        projectile._target = target;
        launch( pilotIndex, slotIndex, direction, projectile, slot );
    }

    void MechArenaWorld::launch( int32 pilotIndex, int32 slotIndex, const float3& direction, ProjectileState projectile, const MechWeaponSlotDef& slot )
    {
        const MechPilot& pilot  = _listPilot[static_cast<size_t>( pilotIndex )];
        const float3     origin = computeCenter( pilot );
        if ( projectile._velocity.getLengthSquared() <= 0.0f )
        {
            // 즉시 탄(광선)은 사거리 안에서 가장 가까운 적을 맞힌다.
            const Ray ray{ origin, direction };
            int32     hitPilot    = constant::invalid_index::kInt32;
            float32   hitDistance = projectile._range;
            for ( int32 other = 0; other < getPilotCount(); ++other )
            {
                const MechPilot& candidate = _listPilot[static_cast<size_t>( other )];
                float32          distance  = 0.0f;
                if ( isEnemyTarget( pilot._team, other ) == false )
                    continue;
                if ( Ray::intersectSphere( ray, computeCenter( candidate ), candidate.getMech()->_radius, hitDistance, distance ) && distance <= hitDistance )
                {
                    hitDistance = distance;
                    hitPilot    = other;
                }
            }
            if ( hitPilot >= 0 )
                resolveHit( pilotIndex, hitPilot, projectile._damage, slot._downValue, slot._knockback, slot._staggerTime, false, slot._kind );
            return;
        }
        // 맞은 뒤의 반응(다운치 · 경직 · 넉백 · 종류)은 탄에 베끼지 않고 슬롯을 가리킨다(`findProjectileSlot`).
        projectile._position         = origin;
        projectile._previousPosition = origin;
        projectile._payload          = static_cast<uint32>( _pMechCatalog->findMechIndex( pilot.getMech()->_id ).value() ) * kSlotStride + static_cast<uint32>( slotIndex ); // 조종사의 기체는 카탈로그에서 왔다
        (void)_projectiles.spawn( projectile );                                                                                                                              // 탄 id 는 쓰지 않는다
    }

    const MechWeaponSlotDef* MechArenaWorld::findProjectileSlot( uint32 payload ) const
    {
        if ( _pMechCatalog == nullptr )
            return nullptr;
        const vector<MechDef>& listMech  = _pMechCatalog->getMechs();
        const uint32           mechIndex = payload / kSlotStride;
        if ( mechIndex >= static_cast<uint32>( listMech.size() ) )
            return nullptr;
        const MechWeaponSlotDef* pSlot = listMech[mechIndex].findSlot( static_cast<int32>( payload % kSlotStride ) );
        return pSlot != nullptr && pSlot->_kind != MechWeaponKind::Melee ? pSlot : nullptr;
    }

    void MechArenaWorld::resolveHit( int32 attacker, int32 victim, float32 damage, float32 downValue, float32 knockback, float32 staggerTime, bool bKnockdown,
                                     MechWeaponKind kind )
    {
        using Internal = MechArenaWorldInternal;
        if ( isTargetable( victim ) == false || _pMechCatalog == nullptr )
            return;
        MechPilot&     target      = _listPilot[static_cast<size_t>( victim )];
        const MechDef* pVictimMech = target.getMech();
        const bool     bAttacker   = isValidPilot( attacker );
        float32        scale       = 1.0f;
        if ( bAttacker )
        {
            const MechDef*       pAttackerMech = _listPilot[static_cast<size_t>( attacker )].getMech();
            const hashed_string& kindName      = kind == MechWeaponKind::Melee ? Internal::getMeleeName() : Internal::getShotName();
            scale                              = _pMechCatalog->getClassModifier( pAttackerMech->_rangeClass ).getScale( kindName );
            scale *= computeModifier( attacker, MechArenaWorldInternal::getAttackName() );
        }
        const float32 finalDamage = damage * scale / MathUtil::max( 0.01f, computeModifier( victim, MechArenaWorldInternal::getDefenseName() ) );
        float32       finalDown   = downValue * _pMechCatalog->getClassModifier( pVictimMech->_rangeClass ).getScale( Internal::getDownName() ) /
                            MathUtil::max( 0.01f, computeModifier( victim, MechArenaWorldInternal::getDownResistName() ) );
        if ( bKnockdown )
            finalDown = MathUtil::max( finalDown, pVictimMech->_downMax ); // 눕히는 기술은 다운치를 한 번에 채운다

        const HealthDamageResult result = target._healthMachine.applyDamage( finalDamage, finalDown, attacker );
        target._healthMachine.discardEvents();
        if ( result._bIgnored == SW_TRUE )
            return;
        const int32 victimParticipant   = target._listParticipant.empty() ? constant::invalid_index::kInt32 : target._listParticipant[static_cast<size_t>( target._deckIndex )];
        const int32 attackerParticipant = bAttacker && _listPilot[static_cast<size_t>( attacker )]._listParticipant.empty() == false
                                            ? _listPilot[static_cast<size_t>( attacker )]._listParticipant[static_cast<size_t>( _listPilot[static_cast<size_t>( attacker )]._deckIndex )]
                                            : constant::invalid_index::kInt32;
        _match.reportDamage( attackerParticipant, victimParticipant, result._healthDamage );
        pushEvent( MechArenaEvent::Kind::Hit, victim, attacker, result._healthDamage, hashed_string{} );

        if ( result._bDied == SW_TRUE )
        {
            target._state        = MechPilotState::Destroyed;
            target._comboStage   = constant::invalid_index::kInt32;
            target._meleeSlot    = constant::invalid_index::kInt32;
            target._bMeleeQueued = SW_FALSE;
            target._velocity     = float3{};
            target._dashRemaining.clear();
            target._staggerRemaining.clear();
            target._melee.cancel();
            target._lockOn.release();
            for ( Countdown& remaining : target._listSkillRemaining )
            {
                remaining.clear();
            }
            ++target._deaths;
            if ( bAttacker && TeamAttitudeUtil::isHostile( _listPilot[static_cast<size_t>( attacker )]._team, target._team ) )
                ++_listPilot[static_cast<size_t>( attacker )]._kills;
            pushEvent( MechArenaEvent::Kind::Destroyed, victim, attacker, 0.0f, pVictimMech->_id );
            _match.reportKill( victimParticipant, attackerParticipant );
            return;
        }

        if ( target._state == MechPilotState::Active )
        {
            if ( staggerTime > 0.0f )
            {
                // 경직: 하던 근접과 대시가 끊긴다.
                target._staggerRemaining.extendTo( staggerTime );
                target._dashRemaining.clear();
                target._melee.cancel();
                target._comboStage   = constant::invalid_index::kInt32;
                target._meleeSlot    = constant::invalid_index::kInt32;
                target._bMeleeQueued = SW_FALSE;
            }
            if ( knockback > 0.0f )
            {
                const float3 away = bAttacker ? Internal::flattenDirection( target._position - _listPilot[static_cast<size_t>( attacker )]._position, -target._forward )
                                              : -target._forward;
                target._position += away * knockback;
                target._position._x = MathUtil::clamp( target._position._x, -_settings._arenaHalfSize, _settings._arenaHalfSize );
                target._position._z = MathUtil::clamp( target._position._z, -_settings._arenaHalfSize, _settings._arenaHalfSize );
            }
            if ( result._bPoiseBroken == SW_TRUE )
            {
                target._state = MechPilotState::Down;
                target._dashRemaining.clear();
                target._melee.cancel();
                target._comboStage   = constant::invalid_index::kInt32;
                target._meleeSlot    = constant::invalid_index::kInt32;
                target._bMeleeQueued = SW_FALSE;
                pushEvent( MechArenaEvent::Kind::Downed, victim, attacker, 0.0f, hashed_string{} );
                triggerSkills( victim, MechSkillTrigger::OnDown );
            }
        }
        triggerSkills( victim, MechSkillTrigger::HealthBelow );
    }

    // --- 스킬과 장비 ------------------------------------------------------------------------------

    void MechArenaWorld::activateSkill( int32 pilotIndex, int32 skillIndex )
    {
        MechPilot&          pilot             = _listPilot[static_cast<size_t>( pilotIndex )];
        const size_t        index             = static_cast<size_t>( skillIndex );
        const MechSkillDef* pSkill            = pilot._listSkill[index];
        const bool          bSkillUnavailable = pilot._listSkillRemaining[index].isActive() || pilot._listSkillCooldown[index].isActive() || pSkill->_duration <= 0.0f;
        if ( bSkillUnavailable )
            return;
        pilot._listSkillRemaining[index].start( pSkill->_duration );
        pilot._listSkillCooldown[index].start( pSkill->_cooldown );
        pilot._listSkillSpent[index] = SW_TRUE;
        pushEvent( MechArenaEvent::Kind::SkillStarted, pilotIndex, constant::invalid_index::kInt32, pSkill->_duration, pSkill->_id );
    }

    void MechArenaWorld::triggerSkills( int32 pilotIndex, MechSkillTrigger trigger )
    {
        MechPilot&  pilot = _listPilot[static_cast<size_t>( pilotIndex )];
        const int32 count = countActiveSkills( pilot );
        for ( int32 index = 0; index < count; ++index )
        {
            const MechSkillDef* pSkill = pilot._listSkill[static_cast<size_t>( index )];
            if ( pSkill->_trigger != trigger )
                continue;
            if ( trigger == MechSkillTrigger::HealthBelow )
            {
                const bool bBelow = pilot._healthMachine.isAlive() && pilot._healthMachine.getHealthRatio() < pSkill->_threshold;
                if ( bBelow == false || pilot._listSkillSpent[static_cast<size_t>( index )] == SW_TRUE )
                    continue;
            }
            activateSkill( pilotIndex, index );
        }
    }

    // --- 직렬화 -----------------------------------------------------------------------------------

    void MechArenaWorld::makeSnapshot( MechArenaSnapshot& outSnapshot ) const
    {
        outSnapshot                = MechArenaSnapshot{};
        outSnapshot._tick          = _tick;
        outSnapshot._phase         = _match.getPhase();
        outSnapshot._winningTeam   = _match.getWinningTeam();
        outSnapshot._remainingTime = _match.getRemaining();
        outSnapshot._arenaHalfSize = _settings._arenaHalfSize;
        outSnapshot._ceiling       = _settings._ceiling;
        for ( int32 team = 0; team < static_cast<int32>( _listTeamName.size() ); ++team )
        {
            outSnapshot._listTeamGauge.push_back( getTeamGauge( team ) );
        }
        for ( int32 index = 0; index < getPilotCount(); ++index )
        {
            const MechPilot&  pilot = _listPilot[static_cast<size_t>( index )];
            const MechDef*    pMech = pilot.getMech();
            MechPilotSnapshot entry;
            entry._position      = pilot._position;
            entry._health        = pilot._healthMachine.getHealth();
            entry._boostHeat     = pilot._boost.getRatio();
            entry._downRatio     = pMech->_downMax > 0.0f ? MathUtil::saturate( 1.0f - pilot._healthMachine.getPoise() / pMech->_downMax ) : 0.0f;
            entry._team          = pilot._team;
            entry._mechIndex     = _pMechCatalog != nullptr ? _pMechCatalog->findMechIndex( pMech->_id ).value() : constant::invalid_index::kInt32; // 조종사의 기체는 카탈로그에서 왔다
            entry._mode          = pilot._mode;
            entry._lockTarget    = pilot._lockOn.hasTarget() ? static_cast<int32>( pilot._lockOn.getTarget() ) - 1 : constant::invalid_index::kInt32;
            entry._comboStage    = pilot._comboStage;
            entry._state         = pilot._state;
            entry._bOverheated   = pilot._boost.isOverheated() ? SW_TRUE : SW_FALSE;
            entry._bInvulnerable = pilot._healthMachine.isInvulnerable() ? SW_TRUE : SW_FALSE;
            const int32 shot     = findSlot( pilot, MechWeaponKind::Shot );
            if ( shot >= 0 )
            {
                entry._magazineAmmo = pilot._listWeaponState[static_cast<size_t>( shot )].getMagazineAmmo();
                entry._bReloading   = pilot._listWeaponState[static_cast<size_t>( shot )].isReloading() ? SW_TRUE : SW_FALSE;
            }
            const int32 skillCount = MathUtil::min( countActiveSkills( pilot ), 32 );
            for ( int32 skill = 0; skill < skillCount; ++skill )
            {
                entry._activeSkillMask |= pilot._listSkillRemaining[static_cast<size_t>( skill )].isActive() ? ( 1u << static_cast<uint32>( skill ) ) : 0u;
            }
            outSnapshot._listPilot.push_back( entry );
        }
        for ( const ProjectileState& projectile : _projectiles.getProjectiles() )
        {
            MechProjectileSnapshot entry;
            entry._position = projectile._position;
            entry._owner    = projectile._owner;
            outSnapshot._listProjectile.push_back( entry );
        }
    }

    bool MechArenaWorld::registerSnapshotMessage( NetMessageChannel& inoutChannel, uint8 kind )
    {
        return inoutChannel.registerMessage<MechArenaSnapshot>( kind );
    }

    bool MechArenaWorld::writeSnapshotMessage( const NetMessageChannel& channel, uint8 kind, NetMessageWriter& outWriter ) const
    {
        MechArenaSnapshot snapshot;
        makeSnapshot( snapshot );
        return channel.writeMessage( kind, snapshot, outWriter );
    }

    void MechArenaWorld::serializeState( StateIO& io )
    {
        if ( io.check( _pMechCatalog != nullptr && _pMoveCatalog != nullptr ) == false )
            return;
        io.list( _listTeamName );
        io.flag( _bStarted );
        io.flag( _bEndReported );
        io.value( _tick );
        io.value( _timer );
        if ( io.isOk() == false )
            return;
        if ( io.isReading() )
        {
            _match.initialize( _bStarted == SW_TRUE ? makeMatchSettings() : MatchSettings{} );
        }
        io.value( _match );
        if ( io.check( _bStarted != SW_TRUE || _match.getTeamCount() == static_cast<int32>( _listTeamName.size() ) ) == false )
            return;
        uint32 pilotCount = static_cast<uint32>( _listPilot.size() );
        // 조종사마다 적어도 덱 개수(4) + 기체 이름(4) + 스킬 개수(4) + 위치, 정수, 상태 슬롯(60) 바이트
        io.count( 72, pilotCount );
        if ( io.isOk() == false )
            return;
        if ( io.isReading() )
        {
            _listPilot.assign( pilotCount, MechPilot{} );
        }
        for ( uint32 index = 0; index < pilotCount; ++index )
        {
            serializePilot( io, static_cast<int32>( index ), _listPilot[index] );
            if ( io.isOk() == false )
                return;
        }
        for ( const MechPilot& pilot : _listPilot )
        {
            io.check( pilot._lockOn.getTarget() <= static_cast<uint64>( pilotCount ) ); // 대상 id 는 조종사 번호 + 1 이다(0 은 대상 없음)
        }
        io.value( _projectiles );
        if ( io.isWriting() || io.isOk() == false )
            return;
        for ( const ProjectileState& projectile : _projectiles.getProjectiles() )
        {
            // 탄속 0 인 탄은 날아가지 않는 광선이라 쏠 때 탄이 되지 않는다. 사거리 없는 탄은 판 밖에 닿기 전까지 남는다 — 둘 다 이 판이 쏘지 않는 탄이다.
            io.check( projectile.getSpeed() > 0.0f && projectile._range > 0.0f && -1 <= projectile._owner && projectile._owner < static_cast<int32>( pilotCount ) &&
                      -1 <= projectile._target && projectile._target < static_cast<int32>( pilotCount ) && findProjectileSlot( projectile._payload ) != nullptr );
        }
        _eventBuffer.clear();
    }

    MatchSettings MechArenaWorld::makeMatchSettings() const
    {
        MatchSettings matchSettings;
        matchSettings._respawnDelay = _settings._respawnDelay;
        matchSettings._timeLimit    = _settings._timeLimit;
        matchSettings._scorePerKill = 1;
        return matchSettings;
    }

    void MechArenaWorld::serializePilot( StateIO& io, int32 pilotIndex, MechPilot& pilot )
    {
        MechArenaWorldInternal::serializeLoadout( io, _pMechCatalog, pilot );
        if ( io.isOk() == false )
            return;
        const uint32 deckCount      = static_cast<uint32>( pilot._listDeckMech.size() );
        const int32  deckSize       = static_cast<int32>( deckCount );
        int32        mode           = pilot._mode;
        uint8        bWasOverheated = pilot._bWasOverheated;
        io.value( pilot._spawnPosition );
        io.value( pilot._team );
        io.value( pilot._deckIndex );
        io.value( pilot._nextDeckIndex );
        io.value( mode );
        io.value( pilot._comboStage );
        io.value( pilot._meleeSlot );
        io.value( pilot._kills );
        io.value( pilot._deaths );
        io.enumValue( pilot._state, MechPilotState::Retired );
        io.flag( pilot._bGrounded );
        io.flag( pilot._bMeleeHit );
        io.flag( pilot._bMeleeQueued );
        io.flag( bWasOverheated );
        if ( io.check( 0 <= pilot._team && pilot._team < static_cast<int32>( _listTeamName.size() ) && 0 <= pilot._deckIndex && pilot._deckIndex < deckSize && 0 <= pilot._nextDeckIndex &&
                       pilot._nextDeckIndex < deckSize && -1 <= pilot._comboStage && 0 <= pilot._kills && 0 <= pilot._deaths ) == false )
            return;
        if ( io.isReading() )
        {
            // 무기, 체력, 부스트 설정은 지금 장착한 기체가 정한다.
            const MechDef* pMech = pilot.getMech();
            if ( io.check( equipMech( pilot, pilotIndex ) ) == false )
                return;
            pilot._bWasOverheated = bWasOverheated;
            pilot._mode           = mode;
            if ( io.check( 0 <= mode && mode < static_cast<int32>( pMech->_listMode.size() ) ) == false )
                return;
        }
        // 참가자는 `start` 가 덱 슬롯마다 만든다. `stepMatch` 와 `resolveHit` 가 지금 슬롯의 참가자를 그대로 찾는다.
        uint32 participantCount = static_cast<uint32>( pilot._listParticipant.size() );
        io.count( 4, participantCount );
        if ( io.check( participantCount == ( _bStarted == SW_TRUE ? deckCount : 0u ) ) == false )
            return;
        if ( io.isReading() )
        {
            pilot._listParticipant.assign( participantCount, constant::invalid_index::kInt32 );
        }
        for ( int32& participant : pilot._listParticipant )
        {
            io.value( participant );
            if ( io.isReading() )
            {
                const MatchParticipant* pParticipant = _match.findParticipant( participant );
                if ( io.check( pParticipant != nullptr && pParticipant->_team == pilot._team ) == false )
                    return;
            }
        }
        io.value( pilot._position );
        io.value( pilot._velocity );
        io.value( pilot._forward );
        io.value( pilot._dashDirection );
        io.value( pilot._dashRemaining );
        io.value( pilot._staggerRemaining );
        io.value( pilot._transformCooldown );
        for ( size_t index = 0; index < pilot._listSkill.size(); ++index )
        {
            io.value( pilot._listSkillRemaining[index] );
            io.value( pilot._listSkillCooldown[index] );
            io.flag( pilot._listSkillSpent[index] );
        }
        MechArenaWorldInternal::serializeInput( io, pilot._input );
        MechArenaWorldInternal::serializeInput( io, pilot._previousInput );
        io.value( pilot._healthMachine );
        io.value( pilot._boost );
        io.value( pilot._lockOn );
        pilot._melee.serializeState( io, _pMoveCatalog );
        io.expect( static_cast<uint32>( pilot._listWeaponState.size() ) );
        if ( io.isOk() == false )
            return;
        for ( size_t index = 0; index < pilot._listWeaponState.size(); ++index )
        {
            io.value( pilot._listWeaponState[index] );
            io.value( pilot._listSpecialCooldown[index] );
        }
        if ( io.isReading() && io.isOk() )
        {
            io.check( MechArenaWorldInternal::isMeleeStateValid( pilot ) && MechArenaWorldInternal::isHealthMachineFittingState( pilot ) );
        }
    }

    void MechArenaWorld::stepPilot( int32 pilotIndex, float32 deltaTime )
    {
        MechPilot& pilot = _listPilot[static_cast<size_t>( pilotIndex )];
        if ( pilot._state != MechPilotState::Active && pilot._state != MechPilotState::Down )
        {
            pilot._previousInput = pilot._input;
            return;
        }
        const MechInput& input    = pilot._input;
        const MechInput& previous = pilot._previousInput;
        const MechDef*   pMech    = pilot.getMech();

        pilot._healthMachine.update( deltaTime );
        pilot._healthMachine.discardEvents();
        if ( pilot._state == MechPilotState::Down && pilot._healthMachine.isPoiseBroken() == false )
        {
            // 기상: 다운치는 가득 찬 채로 일어나고 잠시 무적이다.
            pilot._state = MechPilotState::Active;
            pilot._healthMachine.setInvulnerable( pMech->_wakeInvulnerable );
            pushEvent( MechArenaEvent::Kind::WokeUp, pilotIndex, constant::invalid_index::kInt32, pMech->_wakeInvulnerable, hashed_string{} );
        }
        pilot._staggerRemaining.update( deltaTime );
        pilot._transformCooldown.update( deltaTime );
        for ( WeaponState& weapon : pilot._listWeaponState )
        {
            weapon.update( deltaTime );
        }
        for ( Countdown& cooldown : pilot._listSpecialCooldown )
        {
            cooldown.update( deltaTime );
        }
        stepSkills( pilotIndex, deltaTime );

        bool bControl = false;
        if ( pilot._state == MechPilotState::Active )
        {
            stepLockOn( pilotIndex, MechArenaWorldInternal::isPressed( input._bLockOn, previous._bLockOn ), deltaTime );
            bControl = pilot.canAct();
            if ( bControl )
            {
                const bool bPressedTransform = MechArenaWorldInternal::isPressed( input._bTransform, previous._bTransform );
                const bool bCanTransform     = bPressedTransform && pilot._transformCooldown.isActive() == false && pilot._comboStage < 0 && pMech->_listMode.size() > 1;
                if ( bCanTransform )
                {
                    pilot._mode = ( pilot._mode + 1 ) % static_cast<int32>( pMech->_listMode.size() );
                    pilot._transformCooldown.start( pMech->_transformTime );
                    pilot._dashRemaining.clear();
                    pushEvent( MechArenaEvent::Kind::Transformed, pilotIndex, constant::invalid_index::kInt32, static_cast<float32>( pilot._mode ), pilot.getMode()->_id );
                }
                stepMelee( pilotIndex, MechArenaWorldInternal::isPressed( input._bMelee, previous._bMelee ) );
                if ( pilot._comboStage < 0 )
                    stepWeapons( pilotIndex, deltaTime );
            }
        }
        stepMovement( pilotIndex, bControl, MechArenaWorldInternal::isPressed( input._bDash, previous._bDash ),
                      MechArenaWorldInternal::isPressed( input._bJump, previous._bJump ), deltaTime );

        pilot._boost.setRegenScale( computeModifier( pilotIndex, MechArenaWorldInternal::getBoostRegenName() ) );
        pilot._boost.update( deltaTime );
        const bool bOverheated = pilot._boost.isOverheated();
        if ( bOverheated && pilot._bWasOverheated == SW_FALSE )
            pushEvent( MechArenaEvent::Kind::Overheated, pilotIndex, constant::invalid_index::kInt32, 0.0f, hashed_string{} );
        pilot._bWasOverheated = bOverheated ? SW_TRUE : SW_FALSE;
        pilot._previousInput  = input;
    }

    void MechArenaWorld::stepMovement( int32 pilotIndex, bool bControl, bool bPressedDash, bool bPressedJump, float32 deltaTime )
    {
        using Internal             = MechArenaWorldInternal;
        MechPilot&         pilot   = _listPilot[static_cast<size_t>( pilotIndex )];
        const MechDef*     pMech   = pilot.getMech();
        const MechModeDef* pMode   = pilot.getMode();
        const bool         bRooted = pilot._comboStage >= 0;
        float3             horizontal{};
        bool               bHovering = false;
        if ( bControl && bRooted == false )
        {
            const float32 costScale = pMode->_boostCostScale;
            float3        move{ pilot._input._moveX, 0.0f, pilot._input._moveZ };
            const float32 moveLength = move.getLength();
            if ( moveLength > 1.0f )
                move = move / moveLength;
            if ( bPressedDash && pilot._boost.trySpend( pMech->_dashCost * costScale ) )
            {
                pilot._dashRemaining.start( pMech->_dashTime );
                pilot._dashDirection = Internal::flattenDirection( move, pilot._forward );
            }
            const bool bCanJump = bPressedJump && pilot._bGrounded == SW_TRUE && pilot._boost.trySpend( pMech->_jumpCost * costScale );
            if ( bCanJump )
            {
                pilot._velocity._y = pMech->_jumpSpeed;
                pilot._bGrounded   = SW_FALSE;
            }
            if ( pilot._dashRemaining.isActive() )
            {
                horizontal = pilot._dashDirection * pMech->_dashSpeed;
                pilot._dashRemaining.update( deltaTime );
            }
            else
            {
                horizontal = move * ( pMode->_speed * computeModifier( pilotIndex, MechArenaWorldInternal::getSpeedName() ) );
            }
            if ( pilot._lockOn.hasTarget() == false && moveLength > Internal::kTiny )
                pilot._forward = Internal::flattenDirection( move, pilot._forward );
            const bool bWantsHover = pilot._input._bHover == SW_TRUE && pilot._bGrounded == SW_FALSE && pilot._velocity._y <= 0.0f;
            if ( bWantsHover && pilot._boost.drain( pMech->_hoverPerSecond * costScale, deltaTime ) )
                bHovering = true;
        }
        else
        {
            pilot._dashRemaining.clear();
        }

        pilot._velocity._x = horizontal._x;
        pilot._velocity._z = horizontal._z;
        if ( bHovering )
            pilot._velocity._y = 0.0f;
        else if ( pilot._bGrounded == SW_FALSE )
            pilot._velocity._y -= _settings._gravity * deltaTime;
        pilot._position += pilot._velocity * deltaTime;
        pilot._position._x = MathUtil::clamp( pilot._position._x, -_settings._arenaHalfSize, _settings._arenaHalfSize );
        pilot._position._z = MathUtil::clamp( pilot._position._z, -_settings._arenaHalfSize, _settings._arenaHalfSize );
        if ( pilot._position._y >= _settings._ceiling )
        {
            pilot._position._y = _settings._ceiling;
            pilot._velocity._y = MathUtil::min( 0.0f, pilot._velocity._y );
        }
        if ( pilot._position._y <= 0.0f )
        {
            pilot._position._y = 0.0f;
            pilot._velocity._y = 0.0f;
            pilot._bGrounded   = SW_TRUE;
        }
        else
        {
            pilot._bGrounded = SW_FALSE;
        }
    }

    void MechArenaWorld::stepLockOn( int32 pilotIndex, bool bPressedLockOn, float32 deltaTime )
    {
        MechPilot&   pilot = _listPilot[static_cast<size_t>( pilotIndex )];
        const float3 eye   = computeCenter( pilot );
        collectCandidates( pilotIndex, _listCandidateScratch );
        if ( bPressedLockOn )
        {
            if ( pilot._lockOn.hasTarget() )
                (void)pilot._lockOn.cycle( eye, pilot._forward, _listCandidateScratch, 1 );
            else
                (void)pilot._lockOn.pickBest( eye, pilot._forward, _listCandidateScratch );
        }
        (void)pilot._lockOn.update( eye, _listCandidateScratch, deltaTime );
        if ( pilot._lockOn.hasTarget() )
        {
            const MechPilot& target = _listPilot[static_cast<size_t>( pilot._lockOn.getTarget() - 1 )];
            pilot._forward          = MechArenaWorldInternal::flattenDirection( target._position - pilot._position, pilot._forward );
        }
    }

    void MechArenaWorld::stepWeapons( int32 pilotIndex, float32 deltaTime )
    {
        (void)deltaTime;
        MechPilot&         pilot   = _listPilot[static_cast<size_t>( pilotIndex )];
        const MechModeDef* pMode   = pilot.getMode();
        const int32        offset  = pilot.getMech()->computeSlotOffset( pilot._mode );
        const int32        shot    = findSlot( pilot, MechWeaponKind::Shot );
        const int32        special = findSlot( pilot, MechWeaponKind::Special );
        if ( shot >= 0 && pilot._input._bFire == SW_TRUE )
            fireShot( pilotIndex, shot, pMode->_listWeapon[static_cast<size_t>( shot - offset )],
                      MechArenaWorldInternal::isPressed( pilot._input._bFire, pilot._previousInput._bFire ) );
        if ( special >= 0 && MechArenaWorldInternal::isPressed( pilot._input._bSpecial, pilot._previousInput._bSpecial ) )
            fireSpecial( pilotIndex, special, pMode->_listWeapon[static_cast<size_t>( special - offset )] );
    }

    void MechArenaWorld::stepMelee( int32 pilotIndex, bool bPressedMelee )
    {
        MechPilot& pilot = _listPilot[static_cast<size_t>( pilotIndex )];
        if ( _pMoveCatalog == nullptr )
            return;
        if ( pilot._comboStage < 0 )
        {
            const int32 slotIndex = findSlot( pilot, MechWeaponKind::Melee );
            if ( bPressedMelee == false || slotIndex < 0 )
                return;
            const MechWeaponSlotDef& slot  = pilot.getMode()->_listWeapon[static_cast<size_t>( slotIndex - pilot.getMech()->computeSlotOffset( pilot._mode ) )];
            const MoveFrameData*     pMove = slot._listMoveID.empty() ? nullptr : _pMoveCatalog->find( slot._listMoveID.front() );
            if ( pMove == nullptr )
                return;
            (void)findLockTarget( pilotIndex ); // 고르는 부수 효과만 쓴다 — 대상이 없으면 앞으로 친다
            if ( pilot._melee.start( *pMove ) == false )
                return;
            pilot._comboStage   = 0;
            pilot._meleeSlot    = slotIndex;
            pilot._bMeleeHit    = SW_FALSE;
            pilot._bMeleeQueued = SW_FALSE;
            pushEvent( MechArenaEvent::Kind::ComboAdvanced, pilotIndex, constant::invalid_index::kInt32, 0.0f, pMove->_id );
        }
        else
        {
            const MechWeaponSlotDef& slot = pilot.getMode()->_listWeapon[static_cast<size_t>( pilot._meleeSlot - pilot.getMech()->computeSlotOffset( pilot._mode ) )];
            if ( bPressedMelee )
                pilot._bMeleeQueued = SW_TRUE;
            const int32          nextStage          = pilot._comboStage + 1;
            const bool           bHasNext           = nextStage < static_cast<int32>( slot._listMoveID.size() );
            const MoveFrameData* pNext              = bHasNext ? _pMoveCatalog->find( slot._listMoveID[static_cast<size_t>( nextStage )] ) : nullptr;
            const bool           bCanCancelIntoNext = pilot._bMeleeQueued == SW_TRUE && pNext != nullptr && pilot._melee.canCancelInto( pNext->_id );
            if ( bCanCancelIntoNext )
            {
                // 캔슬 창이 열려서 다음 단이 이 프레임에 나간다.
                if ( pilot._melee.start( *pNext ) == false )
                    return;
                pilot._comboStage   = nextStage;
                pilot._bMeleeHit    = SW_FALSE;
                pilot._bMeleeQueued = SW_FALSE;
                pushEvent( MechArenaEvent::Kind::ComboAdvanced, pilotIndex, constant::invalid_index::kInt32, static_cast<float32>( nextStage ), pNext->_id );
            }
            else
            {
                (void)pilot._melee.advanceFrame();
                if ( pilot._melee.getPhase() == MovePhase::Finished )
                {
                    pilot._melee.cancel();
                    pilot._comboStage   = constant::invalid_index::kInt32;
                    pilot._meleeSlot    = constant::invalid_index::kInt32;
                    pilot._bMeleeQueued = SW_FALSE;
                    return;
                }
            }
        }

        if ( pilot._melee.getPhase() != MovePhase::Active || pilot._bMeleeHit == SW_TRUE )
            return;
        const MechWeaponSlotDef& slot   = pilot.getMode()->_listWeapon[static_cast<size_t>( pilot._meleeSlot - pilot.getMech()->computeSlotOffset( pilot._mode ) )];
        const MechDef*           pMech  = pilot.getMech();
        int32                    target = constant::invalid_index::kInt32;
        float32                  best   = MathUtil::kMaxFloat;
        for ( int32 other = 0; other < getPilotCount(); ++other )
        {
            const MechPilot& candidate = _listPilot[static_cast<size_t>( other )];
            if ( isEnemyTarget( pilot._team, other ) == false )
                continue;
            const float32 reach    = slot._range + pMech->_radius + candidate.getMech()->_radius;
            const float32 distance = float3::getDistance( computeCenter( pilot ), computeCenter( candidate ) );
            // 록온 대상이 닿으면 그쪽을, 아니면 가장 가까운 적을 고른다.
            const bool    bLocked = pilot._lockOn.getTarget() == static_cast<uint64>( other + 1 );
            const float32 score   = bLocked ? -1.0f : distance;
            if ( distance <= reach && score < best )
            {
                best   = score;
                target = other;
            }
        }
        if ( target < 0 )
            return;
        const MoveFrameData& move = pilot._melee.getMove();
        pilot._melee.registerContact( false );
        pilot._bMeleeHit = SW_TRUE;
        resolveHit( pilotIndex, target, move._damage, slot._downValue, slot._knockback, static_cast<float32>( move._hitstun ) * _settings._fixedStep,
                    move._bKnockdown == SW_TRUE, MechWeaponKind::Melee );
    }

    void MechArenaWorld::stepSkills( int32 pilotIndex, float32 deltaTime )
    {
        MechPilot&  pilot = _listPilot[static_cast<size_t>( pilotIndex )];
        const int32 count = countActiveSkills( pilot );
        for ( int32 index = 0; index < count; ++index )
        {
            Countdown& remaining = pilot._listSkillRemaining[static_cast<size_t>( index )];
            pilot._listSkillCooldown[static_cast<size_t>( index )].update( deltaTime );
            if ( remaining.update( deltaTime ) )
                pushEvent( MechArenaEvent::Kind::SkillEnded, pilotIndex, constant::invalid_index::kInt32, 0.0f, pilot._listSkill[static_cast<size_t>( index )]->_id );
        }
        const int32 slot                       = pilot._input._skillSlot;
        const bool  bPressedManual             = slot >= 0 && slot < count && slot != pilot._previousInput._skillSlot;
        const bool  bShouldActivateManualSkill = bPressedManual && pilot.canAct() && pilot._listSkill[static_cast<size_t>( slot )]->_trigger == MechSkillTrigger::Manual;
        if ( bShouldActivateManualSkill )
            activateSkill( pilotIndex, slot );
    }

    void MechArenaWorld::stepProjectiles( float32 deltaTime )
    {
        // 걸음은 공통 시뮬레이션이 하고, 키트는 맞은 조종사에게 슬롯의 반응을 준다. 반응은 모든 탄이 움직인 뒤에 준다(걸음 안에서 세계를 바꾸지 않는다).
        const MechArenaProjectileQuery query( *this );
        for ( const ProjectileEvent& event : _projectiles.step( deltaTime, query ) )
        {
            if ( event._kind != ProjectileEvent::Kind::Hit || event._hit._kind != ProjectileHitKind::Target )
                continue;
            const MechWeaponSlotDef* pSlot = findProjectileSlot( event._projectile._payload );
            if ( pSlot == nullptr )
                continue; // 쏠 때와 상태 읽기가 번호를 봤으므로 닿지 않는다
            resolveHit( event._projectile._owner, event._hit._other, event._projectile._damage, pSlot->_downValue, pSlot->_knockback, pSlot->_staggerTime, false, pSlot->_kind );
        }
    }
} // namespace sw
