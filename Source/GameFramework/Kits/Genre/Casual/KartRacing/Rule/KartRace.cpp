#include "pch.h"

#include "GameFramework/Kits/Genre/Casual/KartRacing/Rule/KartRace.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Casual/KartRacing/Rule/KartGhost.h"
#include "GameFramework/Kits/Genre/Casual/KartRacing/Rule/KartTrack.h"
#include "GameFramework/Runtime/Character/Combat/LockOnSelector.h"
#include "GameFramework/Runtime/Character/Combat/Projectile/ProjectileDef.h"
#include "GameFramework/Runtime/Character/Combat/Projectile/ResolvedShot.h"

namespace sw
{
    namespace
    {
        struct KartRaceInternal
        {
            static constexpr float32 kAheadPriority = 100.0f; ///< 빨간 껍질은 바로 앞 순위의 차를 먼저 잡는다

            static void serializeRacer( StateIO& io, int32 racerCount, const KartTrack* pTrack, KartRacer& kart )
            {
                int32 driftSide = kart._ai.getDriftSide();
                io.value( kart._motor );
                io.value( driftSide );
                io.value( kart._input._vehicle._throttle );
                io.value( kart._input._vehicle._steer );
                io.flag( kart._input._vehicle._bDriftHeld );
                io.flag( kart._input._vehicle._bBoostPressed );
                io.flag( kart._input._vehicle._bJumpPressed );
                io.flag( kart._input._bUseItem );
                if ( io.check( -1 <= driftSide && driftSide <= 1 ) == false )
                    return;
                if ( io.isReading() )
                {
                    kart._ai.setDriftSide( driftSide );
                }
                io.list( kart._listLapTime );
                io.value( kart._itemID );
                io.value( kart._previousPosition );
                io.value( kart._distance );
                io.value( kart._progress );
                io.value( kart._lapStartTime );
                io.value( kart._bestLapTime );
                io.value( kart._finishTime );
                io.value( kart._spinTime );
                io.value( kart._shieldTime );
                io.value( kart._wrongWayTime );
                io.value( kart._itemHeldTime );
                io.value( kart._speedScale );
                io.value( kart._lap );
                io.value( kart._nextCheckpoint );
                io.value( kart._place );
                io.value( kart._finishPlace );
                io.value( kart._lastBoostPad );
                io.flag( kart._bFinished );
                io.flag( kart._bWrongWay );
                if ( io.isWriting() )
                    return;
                io.check( 0 <= kart._place && kart._place <= racerCount && 0 <= kart._finishPlace && kart._finishPlace <= racerCount );
                // 체크포인트 수는 문 번호로 바로 쓰인다(`computeProgress`). 트랙 데이터가 바뀐 세이브면 문 밖을 읽는다.
                io.check( 0 <= kart._nextCheckpoint && kart._nextCheckpoint <= pTrack->getCheckpointCount() && 0 <= kart._lap && kart._lap <= pTrack->getLapCount() + 1 );
                // 비교는 NaN 에서 거짓이다. 시간이 NaN 이면 랩 타임과 최고 기록이 줄줄이 깨진다.
                io.check( 0.0f <= kart._lapStartTime && 0.0f <= kart._bestLapTime && 0.0f <= kart._finishTime && 0.0f <= kart._wrongWayTime && 0.0f <= kart._itemHeldTime && kart._speedScale > 0.0f );
            }
        };
    } // namespace

    /**
     * @class KartProjectileQuery
     * @brief 경기의 아이템이 묻는 세계입니다. 유도 대상은 그 차의 자리이고, 맞는 것은 선분 끝에서 수평 거리가 (아이템 반지름 + 차 반지름) 안인 첫 차입니다.
     * @details 들어온 차, 쏜 차의 유예 시간, 1 등 공격의 대상이 아닌 차는 맞지 않는다. 벽은 없다(트랙 밖 판정은 차의 일이다).
     */
    class KartProjectileQuery : public IProjectileWorld
    {
    public:
        explicit KartProjectileQuery( const KartRace& race )
            : _race{ race }
        {
        }

        bool findTargetPoint( const ProjectileState& projectile, float3& outPoint ) const override
        {
            if ( _race.isValidRacer( projectile._target ) == false )
                return false;
            outPoint = _race._listRacer[static_cast<size_t>( projectile._target )]._motor.getPosition();
            return true;
        }

        bool sweep( const ProjectileState& projectile, const float3& from, const float3& to, ProjectileHit& outHit ) const override
        {
            (void)from; // 수평 거리로 끝점만 본다(껍질은 차보다 느리지 않아 끝점이면 충분하다)
            const KartItemDef* pItem = _race._pItemCatalog != nullptr ? _race._pItemCatalog->find( projectile._defID ) : nullptr;
            if ( pItem == nullptr )
                return false;
            const float32 reach = projectile._radius + _race._settings._kartRadius;
            for ( size_t racerIndex = 0; racerIndex < _race._listRacer.size(); ++racerIndex )
            {
                const int32      racer         = static_cast<int32>( racerIndex );
                const KartRacer& kart          = _race._listRacer[racerIndex];
                const bool       bOwnerGrace   = racer == projectile._owner && projectile._age < _race._settings._ownerGrace;
                const bool       bNotTarget    = pItem->_kind == KartItemKind::LeaderShell && racer != projectile._target;
                const bool       bSkipHitCheck = kart._bFinished == SW_TRUE || bOwnerGrace || bNotTarget || racer == projectile._lastHit;
                if ( bSkipHitCheck )
                    continue;
                if ( float3::getHorizontalDistanceSquared( kart._motor.getPosition(), to ) > reach * reach )
                    continue;
                outHit._position = to;
                outHit._other    = racer;
                outHit._kind     = ProjectileHitKind::Target;
                return true;
            }
            return false;
        }

    private:
        const KartRace& _race;
    };
} // namespace sw

namespace sw
{
    KartRace::KartRace()
        : _listRacer{}
        , _projectiles{}
        , _eventBuffer{}
        , _listItemBoxTimer{}
        , _listPlaceOrder{}
        , _settings{}
        , _random{}
        , _timer{}
        , _pTrack{ nullptr }
        , _pItemCatalog{ nullptr }
        , _pProjectileCatalog{ nullptr }
        , _pGhost{ nullptr }
        , _raceTime{ 0.0f }
        , _countdown{}
        , _bestLapTime{ 0.0f }
        , _firstFinishTime{ -1.0f }
        , _ghostRacer{ constant::invalid_index::kInt32 }
        , _finishedCount{ 0 }
        , _phase{ MatchPhase::Waiting }
    {
    }

    bool KartRace::initialize( const KartRaceSettings& settings, const KartTrack* pTrack, const KartItemCatalog* pItemCatalog, const ProjectileCatalog* pProjectileCatalog )
    {
        _settings           = settings;
        _pTrack             = pTrack;
        _pItemCatalog       = pItemCatalog;
        _pProjectileCatalog = pProjectileCatalog;
        _random.setSeed( settings._seed );
        _timer = FixedStepTimer{ settings._step, constant::time::kDefaultMaxFrameSeconds };
        _listRacer.clear();
        _projectiles = ProjectileSimulation{};
        _projectiles.setPlaneNormal( float3{ 0.0f, 1.0f, 0.0f } ); // 아이템은 수평으로 날고 높이는 트랙을 따른다
        _eventBuffer.clear();
        _listPlaceOrder.clear();
        _listItemBoxTimer.clear();
        if ( pTrack != nullptr )
            _listItemBoxTimer.resize( pTrack->getItemBoxPositions().size() );
        _pGhost     = nullptr;
        _ghostRacer = constant::invalid_index::kInt32;
        _raceTime   = 0.0f;
        _countdown.clear();
        _bestLapTime     = 0.0f;
        _firstFinishTime = -1.0f;
        _finishedCount   = 0;
        _phase           = MatchPhase::Waiting;
        if ( pItemCatalog == nullptr )
            return true;
        // 날거나 놓이는 아이템은 탄 정의가 운동을 정한다. 없는 탄은 여기서 한 번 알리고, 그 아이템은 써도 나가지 않는다(`useItem`).
        bool bComplete = true;
        for ( const KartItemDef& item : pItemCatalog->getAll() )
        {
            if ( item.isProjectile() == false || findItemProjectile( item ) != nullptr )
                continue;
            SW_LOG_ERROR_ID( "KartRace.ItemProjectileMissing", "Kart item '%#' points at projectile '%#' that is not in the projectile catalog - the item does nothing", item._id.c_str(),
                             item._projectileID.c_str() );
            bComplete = false;
        }
        return bComplete;
    }

    const ProjectileDef* KartRace::findItemProjectile( const KartItemDef& item ) const
    {
        return _pProjectileCatalog != nullptr && item.isProjectile() ? _pProjectileCatalog->find( item._projectileID ) : nullptr;
    }

    expected<int32> KartRace::addRacer( const KinematicVehicleSettings& vehicleSettings, bool bAI )
    {
        const bool bCannotAddRacer = _phase != MatchPhase::Waiting || _pTrack == nullptr || _pTrack->isValid() == false;
        if ( bCannotAddRacer )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the race is not waiting or the track is missing" } );
        _listRacer.emplace_back();
        KartRacer& kart    = _listRacer.back();
        kart._baseSettings = vehicleSettings;
        kart._bAI          = bAI ? SW_TRUE : SW_FALSE;
        kart._motor.setSettings( vehicleSettings );
        kart._motor.setGround( _pTrack );
        return static_cast<int32>( _listRacer.size() ) - 1;
    }

    void KartRace::setRacerAI( int32 racer, const KartAISettings& settings )
    {
        if ( isValidRacer( racer ) )
            _listRacer[static_cast<size_t>( racer )]._ai.setSettings( settings );
    }

    void KartRace::start()
    {
        const bool bCannotStartRace = _phase != MatchPhase::Waiting || _listRacer.empty() || _pTrack == nullptr;
        if ( bCannotStartRace )
            return;
        // 격자: 결승선 뒤로 두 줄씩, 왼쪽과 오른쪽을 번갈아 세운다.
        const float32 laneOffset = _pTrack->getWidth() * 0.25f;
        for ( size_t racerIndex = 0; racerIndex < _listRacer.size(); ++racerIndex )
        {
            KartRacer&           kart     = _listRacer[racerIndex];
            const int32          row      = static_cast<int32>( racerIndex / 2 );
            const float32        side     = ( racerIndex % 2 ) == 0 ? -laneOffset : laneOffset;
            const float32        distance = -( _settings._gridFirstDistance + _settings._gridRowSpacing * static_cast<float32>( row ) );
            const KartTrackFrame frame    = _pTrack->sample( distance );
            const float3         position{ frame._position._x + frame._right._x * side, frame._position._y, frame._position._z + frame._right._z * side };
            kart._motor.reset( position, MathUtil::atan2( frame._tangent._x, frame._tangent._z ) );
            kart._previousPosition = position;
            kart._distance         = _pTrack->project( position )._distance;
            kart._lap              = 0;
            kart._nextCheckpoint   = _pTrack->getCheckpointCount(); // 출발선 전에는 "체크포인트를 다 지난" 것으로 본다
            kart._ai.reset();
            kart._progress = computeProgress( kart );
        }
        // 격자에 서기 전에 기록을 걸었으면 출발 위치가 틀려 재생이 다른 길로 간다 — 선 자리로 다시 시작한다.
        if ( _pGhost != nullptr )
            startGhostRecording( _ghostRacer, _pGhost );
        _phase = MatchPhase::Warmup;
        _countdown.start( _settings._countdownTime );
        _raceTime = 0.0f;
        updatePlaces();
        if ( _countdown.isActive() == false )
        {
            _phase = MatchPhase::InProgress;
            pushEvent( KartRaceEvent::Kind::RaceStarted, nullopt, nullopt, 0, 0.0f );
        }
    }

    void KartRace::setInput( int32 racer, const KartRacerInput& input )
    {
        if ( isValidRacer( racer ) == false )
            return;
        // 눌림은 다음 걸음까지 남긴다(프레임 안에 걸음이 없어도 사라지지 않게 하려는 것이다).
        KartRacerInput& stored         = _listRacer[static_cast<size_t>( racer )]._input;
        const uint8     bBoostPressed  = stored._vehicle._bBoostPressed;
        const uint8     bJumpPressed   = stored._vehicle._bJumpPressed;
        const uint8     bUseItem       = stored._bUseItem;
        stored                         = input;
        stored._vehicle._bBoostPressed = input._vehicle._bBoostPressed != SW_FALSE || bBoostPressed != SW_FALSE ? SW_TRUE : SW_FALSE;
        stored._vehicle._bJumpPressed  = input._vehicle._bJumpPressed != SW_FALSE || bJumpPressed != SW_FALSE ? SW_TRUE : SW_FALSE;
        stored._bUseItem               = input._bUseItem != SW_FALSE || bUseItem != SW_FALSE ? SW_TRUE : SW_FALSE;
    }

    int32 KartRace::update( float32 frameTime )
    {
        const int32 stepCount = _timer.consume( frameTime );
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            step();
        }
        return stepCount;
    }

    void KartRace::step()
    {
        const float32 deltaTime = _settings._step;
        if ( _phase == MatchPhase::Warmup )
        {
            _countdown.update( deltaTime );
            if ( _countdown.isActive() == false )
            {
                _phase = MatchPhase::InProgress;
                pushEvent( KartRaceEvent::Kind::RaceStarted, nullopt, nullopt, 0, 0.0f );
            }
            return;
        }
        if ( _phase != MatchPhase::InProgress && _phase != MatchPhase::Ended )
            return;
        if ( _phase == MatchPhase::InProgress )
            _raceTime += deltaTime;

        // 1) 차: 입력, 아이템, 러버밴딩, 걸음, 부스트 패드, 진행 순서로 처리한다.
        const int32 racerCount = static_cast<int32>( _listRacer.size() );
        for ( int32 racer = 0; racer < racerCount; ++racer )
        {
            KartRacer&                  kart     = _listRacer[static_cast<size_t>( racer )];
            bool                        bUseItem = false;
            const KinematicVehicleInput input    = resolveInput( racer, bUseItem );
            const bool                  bRecord  = _pGhost != nullptr && racer == _ghostRacer && kart._bFinished == SW_FALSE;
            if ( bRecord )
                _pGhost->record( input, bUseItem );
            if ( bUseItem )
                (void)useItem( racer );
            updateRubberBand( racer );

            kart._previousPosition = kart._motor.getPosition();
            kart._motor.update( input, deltaTime );
            if ( _pTrack->applyBoostPad( kart._motor, kart._lastBoostPad ) )
                pushEvent( KartRaceEvent::Kind::BoostPad, racer, nullopt, kart._lastBoostPad, _raceTime );
            kart._motor.discardEvents(); // 경기는 차 이벤트를 쓰지 않으므로 쌓이지 않게 비운다

            kart._spinTime.update( deltaTime );
            kart._shieldTime.update( deltaTime );
            if ( kart._itemID.empty() == false )
                kart._itemHeldTime += deltaTime;
            updateProgress( racer );
        }

        // 2) 상자, 아이템, 순위, 종료 순서로 처리한다.
        updateItemBoxes( deltaTime );
        updateProjectiles( deltaTime );
        updatePlaces();
        resolveRaceEnd();
    }

    KinematicVehicleInput KartRace::resolveInput( int32 racer, bool& outUseItem )
    {
        KartRacer& kart = _listRacer[static_cast<size_t>( racer )];
        outUseItem      = false;
        if ( kart._spinTime.isActive() )
        {
            kart._input = KartRacerInput{}; // 도는 동안의 눌림은 버린다
            return KinematicVehicleInput{};
        }
        if ( kart._bAI != SW_FALSE )
        {
            KartAIContext context;
            context._bHasItem        = kart._itemID.empty() ? SW_FALSE : SW_TRUE;
            context._itemHeldTime    = kart._itemHeldTime;
            context._bShielded       = kart._shieldTime.isActive() ? SW_TRUE : SW_FALSE;
            const KartItemDef* pItem = _pItemCatalog != nullptr && kart._itemID.empty() == false ? _pItemCatalog->find( kart._itemID ) : nullptr;
            if ( pItem != nullptr )
                context._itemKind = pItem->_kind;
            const optional<int32> ahead  = findRacerAtPlace( kart._place - 1 );
            const optional<int32> behind = findRacerAtPlace( kart._place + 1 );
            if ( ahead.has_value() )
                context._gapAhead = _listRacer[static_cast<size_t>( *ahead )]._progress - kart._progress;
            if ( behind.has_value() )
                context._gapBehind = kart._progress - _listRacer[static_cast<size_t>( *behind )]._progress;
            outUseItem = kart._ai.shouldUseItem( context );
            return kart._ai.computeInput( *_pTrack, kart._motor, kart._distance );
        }
        // 사람은 양자화한 입력으로 달린다(고스트와 락스텝이 같은 경로를 내게 하려는 것이다).
        const KinematicVehicleInput input   = KartGhost::quantizeInput( kart._input._vehicle );
        outUseItem                          = kart._input._bUseItem != SW_FALSE;
        kart._input._vehicle._bBoostPressed = SW_FALSE;
        kart._input._vehicle._bJumpPressed  = SW_FALSE;
        kart._input._bUseItem               = SW_FALSE;
        return input;
    }

    void KartRace::updateProgress( int32 racer )
    {
        KartRacer&                kart       = _listRacer[static_cast<size_t>( racer )];
        const float3&             position   = kart._motor.getPosition();
        const KartTrackProjection projection = _pTrack->project( position, kart._distance, _settings._projectionRange );
        kart._distance                       = projection._distance;

        // 문: 다음 체크포인트(앞), 지난 체크포인트(뒤), 결승선 순서로 확인한다.
        const int32 checkpointCount = _pTrack->getCheckpointCount();
        // 한 걸음에 여러 문을 지날 수 있다(가까이 놓인 문, 같은 자리의 문). 하나만 세면 뒤의 문은 다시 지날 길이 없어 그 바퀴를 끝낼 수 없다.
        bool bPassedForward = false;
        while ( kart._nextCheckpoint < checkpointCount && _pTrack->computeGateCrossing( kart._nextCheckpoint + 1, kart._previousPosition, position ) > 0 )
        {
            ++kart._nextCheckpoint;
            bPassedForward = true;
            pushEvent( KartRaceEvent::Kind::CheckpointPassed, racer, nullopt, kart._nextCheckpoint, _raceTime );
        }
        while ( bPassedForward == false && kart._nextCheckpoint > 0 && _pTrack->computeGateCrossing( kart._nextCheckpoint, kart._previousPosition, position ) < 0 )
        {
            --kart._nextCheckpoint;
        }
        const int32 finishCrossing         = _pTrack->computeGateCrossing( 0, kart._previousPosition, position );
        const bool  bCrossedFinishBackward = finishCrossing < 0 && kart._nextCheckpoint == 0 && kart._lap > 0 && kart._bFinished == SW_FALSE;
        if ( finishCrossing > 0 )
        {
            if ( kart._nextCheckpoint >= checkpointCount )
                completeLap( racer );
            else
                pushEvent( KartRaceEvent::Kind::CheckpointMissed, racer, nullopt, kart._nextCheckpoint, _raceTime );
        }
        else if ( bCrossedFinishBackward )
        {
            // 결승선을 뒤로 넘었다. 바퀴를 되돌리고 그 바퀴 기록을 지운다.
            --kart._lap;
            kart._nextCheckpoint = checkpointCount;
            if ( kart._listLapTime.empty() == false && kart._lap >= 1 )
            {
                kart._lapStartTime -= kart._listLapTime.back();
                kart._listLapTime.pop_back();
                kart._bestLapTime = 0.0f;
                for ( const float32 lapTime : kart._listLapTime )
                {
                    kart._bestLapTime = kart._bestLapTime <= 0.0f ? lapTime : MathUtil::min( kart._bestLapTime, lapTime );
                }
                // 경기 최고 기록도 다시 센다. 지운 바퀴가 최고 기록으로 남으면 차 기록과 어긋난다.
                _bestLapTime = 0.0f;
                for ( const KartRacer& other : _listRacer )
                {
                    _bestLapTime = other._bestLapTime > 0.0f && ( _bestLapTime <= 0.0f || other._bestLapTime < _bestLapTime ) ? other._bestLapTime : _bestLapTime;
                }
            }
            pushEvent( KartRaceEvent::Kind::LapRevoked, racer, nullopt, kart._lap, _raceTime );
        }

        // 역주행: 트랙 방향 속도가 계속 음수이면 역주행이다.
        const float3& velocity   = kart._motor.getVelocity();
        const float32 alongSpeed = velocity._x * projection._tangent._x + velocity._z * projection._tangent._z;
        if ( alongSpeed < -_settings._wrongWaySpeed && kart._spinTime.isActive() == false )
        {
            kart._wrongWayTime += _settings._step;
            if ( kart._bWrongWay == SW_FALSE && kart._wrongWayTime >= _settings._wrongWayDelay )
            {
                kart._bWrongWay = SW_TRUE;
                pushEvent( KartRaceEvent::Kind::WrongWay, racer, nullopt, 1, _raceTime );
            }
        }
        else
        {
            kart._wrongWayTime = 0.0f; // 이어서 거꾸로 달린 시간이다 - 벽에 튕겨 잠깐씩 뒤로 밀린 것이 쌓이면 안 된다
            if ( alongSpeed > _settings._wrongWaySpeed && kart._bWrongWay == SW_TRUE )
            {
                kart._bWrongWay = SW_FALSE;
                pushEvent( KartRaceEvent::Kind::WrongWay, racer, nullopt, 0, _raceTime );
            }
        }
        kart._progress = computeProgress( kart );
    }

    void KartRace::completeLap( int32 racer )
    {
        KartRacer& kart = _listRacer[static_cast<size_t>( racer )];
        if ( kart._bFinished == SW_TRUE )
            return;
        if ( kart._lap >= 1 )
        {
            const float32 lapTime = _raceTime - kart._lapStartTime;
            kart._listLapTime.push_back( lapTime );
            kart._bestLapTime = kart._bestLapTime <= 0.0f ? lapTime : MathUtil::min( kart._bestLapTime, lapTime );
            pushEvent( KartRaceEvent::Kind::LapCompleted, racer, nullopt, kart._lap, lapTime );
            if ( _bestLapTime <= 0.0f || lapTime < _bestLapTime )
            {
                _bestLapTime = lapTime;
                pushEvent( KartRaceEvent::Kind::NewBestLap, racer, nullopt, kart._lap, lapTime );
            }
        }
        ++kart._lap;
        kart._nextCheckpoint = 0;
        kart._lapStartTime   = _raceTime;
        if ( kart._lap > _pTrack->getLapCount() )
        {
            finishRacer( racer );
            return;
        }
        pushEvent( KartRaceEvent::Kind::LapStarted, racer, nullopt, kart._lap, _raceTime );
        if ( kart._lap == _pTrack->getLapCount() )
            pushEvent( KartRaceEvent::Kind::FinalLap, racer, nullopt, kart._lap, _raceTime );
    }

    void KartRace::finishRacer( int32 racer )
    {
        KartRacer& kart = _listRacer[static_cast<size_t>( racer )];
        if ( kart._bFinished == SW_TRUE )
            return;
        kart._bFinished   = SW_TRUE;
        kart._finishPlace = ++_finishedCount;
        kart._finishTime  = _raceTime;
        if ( _firstFinishTime < 0.0f )
            _firstFinishTime = _raceTime;
        if ( _pGhost != nullptr && racer == _ghostRacer )
            _pGhost->setFinishTime( _raceTime );
        pushEvent( KartRaceEvent::Kind::Finished, racer, nullopt, kart._finishPlace, _raceTime );
    }

    float32 KartRace::computeProgress( const KartRacer& kart ) const
    {
        const float32 length          = _pTrack->getLength();
        const int32   checkpointCount = _pTrack->getCheckpointCount();
        // 지금 있어야 할 두 문 사이로 자른다. 문을 건너뛴 차는 다음 문보다 앞설 수 없다.
        const float32 lower = kart._nextCheckpoint <= 0 ? 0.0f : _pTrack->getGate( kart._nextCheckpoint )._distance;
        const float32 upper = kart._nextCheckpoint >= checkpointCount ? length : _pTrack->getGate( kart._nextCheckpoint + 1 )._distance;
        const float32 along = MathUtil::clamp( kart._distance, lower, upper );
        return static_cast<float32>( kart._lap - 1 ) * length + along;
    }

    void KartRace::updateItemBoxes( float32 deltaTime )
    {
        const vector<float3>& listBox = _pTrack->getItemBoxPositions();
        for ( size_t boxIndex = 0; boxIndex < _listItemBoxTimer.size(); ++boxIndex )
        {
            if ( _listItemBoxTimer[boxIndex].isActive() )
            {
                _listItemBoxTimer[boxIndex].update( deltaTime );
                continue;
            }
            if ( _settings._bItems == SW_FALSE || _pItemCatalog == nullptr )
                continue;
            const float32 reach = _settings._itemBoxRadius + _settings._kartRadius;
            for ( size_t racerIndex = 0; racerIndex < _listRacer.size(); ++racerIndex )
            {
                KartRacer& kart = _listRacer[racerIndex];
                if ( kart._bFinished == SW_TRUE || float3::getHorizontalDistanceSquared( kart._motor.getPosition(), listBox[boxIndex] ) > reach * reach )
                    continue;
                // 상자는 들고 있어도 깨진다. 빈 손이면 순위 테이블로 뽑는다.
                _listItemBoxTimer[boxIndex].start( _settings._itemBoxRespawn );
                hashed_string itemID;
                if ( kart._itemID.empty() )
                {
                    const KartItemDef* pItem = _pItemCatalog->rollItem( kart._place, static_cast<int32>( _listRacer.size() ), _random );
                    if ( pItem != nullptr )
                    {
                        kart._itemID       = pItem->_id;
                        kart._itemHeldTime = 0.0f;
                        itemID             = pItem->_id;
                    }
                }
                pushEvent( KartRaceEvent::Kind::ItemBoxTaken, static_cast<int32>( racerIndex ), nullopt, static_cast<int32>( boxIndex ), _raceTime );
                _eventBuffer.getLast()._itemID = itemID;
                break;
            }
        }
    }

    void KartRace::giveItem( int32 racer, const hashed_string& itemID )
    {
        if ( isValidRacer( racer ) == false )
            return;
        KartRacer& kart    = _listRacer[static_cast<size_t>( racer )];
        kart._itemID       = itemID;
        kart._itemHeldTime = 0.0f;
    }

    bool KartRace::useItem( int32 racer )
    {
        if ( isValidRacer( racer ) == false || _pItemCatalog == nullptr || _phase != MatchPhase::InProgress )
            return false;
        KartRacer& kart = _listRacer[static_cast<size_t>( racer )];
        if ( kart._bFinished == SW_TRUE ) // 들어온 차는 경기 밖이다 — 남은 차를 맞히지 않는다
            return false;
        const KartItemDef* pItem = kart._itemID.empty() ? nullptr : _pItemCatalog->find( kart._itemID );
        if ( pItem == nullptr )
            return false;
        const ProjectileDef* pProjectile = findItemProjectile( *pItem );
        if ( pItem->isProjectile() && pProjectile == nullptr )
            return false; // 없는 탄은 `initialize` 가 알렸다. 아이템은 그대로 든다
        kart._itemID       = hashed_string{};
        kart._itemHeldTime = 0.0f;
        pushEvent( KartRaceEvent::Kind::ItemUsed, racer, nullopt, static_cast<int32>( pItem->_kind ), _raceTime );
        _eventBuffer.getLast()._itemID = pItem->_id;

        // 아이템은 공통 발사체다. 운동(속력 · 유도 · 반지름 · 수명 · 폭발 반지름)은 아이템의 탄 정의이고, 정의 id 는 반응을 찾는 아이템 id 다.
        // 수정자가 없어 탄 값 그대로라 쓸 때 푼다. 평면(법선 Y)에 묶여 −Y 중력은 의미가 없으므로 넘기지 않는다.
        const float3    forward    = kart._motor.computeForward();
        const float3&   position   = kart._motor.getPosition();
        const float3    flat       = float3{ forward._x, 0.0f, forward._z };
        ProjectileState projectile = pProjectile != nullptr ? ProjectileState::launch( ResolvedShot::resolve( *pProjectile, {}, {} ), position, flat, 0.0f ) : ProjectileState{};
        projectile._defID          = pItem->_id;
        projectile._owner          = racer;
        const float32 launch       = _settings._kartRadius + projectile._radius + 0.5f;
        projectile._position       = float3{ position._x + forward._x * launch, position._y, position._z + forward._z * launch };
        switch ( pItem->_kind )
        {
            case KartItemKind::Banana:
            {
                projectile._velocity = float3{};
                projectile._turnRate = 0.0f;
                projectile._position = float3{ position._x - forward._x * _settings._dropBehindDistance, position._y,
                                               position._z - forward._z * _settings._dropBehindDistance };
                (void)_projectiles.spawn( projectile ); // 탄 id 는 쓰지 않는다
                break;
            }
            case KartItemKind::GreenShell:
            {
                (void)_projectiles.spawn( projectile );
                break;
            }
            case KartItemKind::RedShell:
            {
                projectile._target = pickRedShellTarget( racer ).value_or( constant::invalid_index::kInt32 ); // 탄 상태 칸의 "대상 없음"(저장 형식)
                (void)_projectiles.spawn( projectile );
                break;
            }
            case KartItemKind::LeaderShell:
            {
                projectile._target = findLeaderInRace().value_or( constant::invalid_index::kInt32 );
                (void)_projectiles.spawn( projectile );
                break;
            }
            case KartItemKind::Booster:
            {
                kart._motor.startBoost( pItem->_boostTime, -1 );
                break;
            }
            case KartItemKind::Shield:
            {
                kart._shieldTime.start( pItem->_shieldTime );
                break;
            }
        }
        return true;
    }

    optional<int32> KartRace::findLeaderInRace() const
    {
        // 들어온 차는 맞지 않는다(`applyHit`). 1 등을 그대로 쫓으면 첫 결승 뒤의 공격은 아무도 맞히지 못하고 떠돈다.
        for ( const int32 racer : _listPlaceOrder )
        {
            if ( _listRacer[static_cast<size_t>( racer )]._bFinished == SW_FALSE )
                return racer;
        }
        return nullopt;
    }

    optional<int32> KartRace::pickRedShellTarget( int32 owner ) const
    {
        // 기반 록온: 앞쪽 원뿔 안의 차를 고르되, 바로 앞 순위의 차에 큰 우선도를 준다(없으면 원뿔 안에서 가장 좋은 차, 그것도 없으면 곧게 나간다).
        const KartRacer&        shooter = _listRacer[static_cast<size_t>( owner )];
        const optional<int32>   ahead   = findRacerAtPlace( shooter._place - 1 );
        vector<LockOnCandidate> listCandidate;
        for ( size_t racerIndex = 0; racerIndex < _listRacer.size(); ++racerIndex )
        {
            if ( static_cast<int32>( racerIndex ) == owner || _listRacer[racerIndex]._bFinished == SW_TRUE )
                continue;
            LockOnCandidate candidate;
            candidate._position = _listRacer[racerIndex]._motor.getPosition();
            candidate._id       = static_cast<uint64>( racerIndex ) + 1;
            candidate._priority = ahead == static_cast<int32>( racerIndex ) ? KartRaceInternal::kAheadPriority : 0.0f;
            listCandidate.push_back( candidate );
        }
        LockOnSettings lockSettings;
        lockSettings._maxDistance   = _settings._redShellLockDistance;
        lockSettings._breakDistance = _settings._redShellLockDistance;
        lockSettings._maxAngle      = _settings._redShellLockAngle;
        LockOnSelector selector;
        selector.setSettings( lockSettings );
        const uint64 picked = selector.pickBest( shooter._motor.getPosition(), shooter._motor.computeForward(), listCandidate );
        return picked == 0 ? nullopt : optional<int32>( static_cast<int32>( picked - 1 ) );
    }

    void KartRace::updateProjectiles( float32 deltaTime )
    {
        if ( _pItemCatalog == nullptr )
            return;
        // 걸음 전: 카탈로그에서 빠진 아이템은 끝내고, 1등 공격은 항상 지금의 1등을 쫓는다.
        for ( ProjectileState& projectile : _projectiles.accessProjectiles() )
        {
            const KartItemDef* pItem = _pItemCatalog->find( projectile._defID );
            if ( pItem == nullptr )
                projectile._bAlive = SW_FALSE;
            else if ( pItem->_kind == KartItemKind::LeaderShell )
                projectile._target = findLeaderInRace().value_or( constant::invalid_index::kInt32 );
        }
        _projectiles.removeDead();

        // 걸음: 수명 · 유도 · 이동 · 맞음(쏜 차의 유예 · 1등 공격의 대상만)은 공통 시뮬레이션이 한다.
        const KartProjectileQuery      query( *this );
        const vector<ProjectileEvent>& listEvent = _projectiles.step( deltaTime, query );

        // 걸음 뒤: 날아가는 아이템은 트랙 높이를 따르고(바나나는 놓인 자리), 맞은 아이템은 효과를 준다.
        for ( ProjectileState& projectile : _projectiles.accessProjectiles() )
        {
            if ( projectile._velocity.getLengthSquared() > 0.0f )
                projectile._position._y = _pTrack->sampleHeight( projectile._position._x, projectile._position._z );
        }
        for ( const ProjectileEvent& event : listEvent )
        {
            const KartItemDef* pItem = _pItemCatalog->find( event._projectile._defID );
            if ( event._kind != ProjectileEvent::Kind::Hit || pItem == nullptr )
                continue;
            if ( pItem->_kind == KartItemKind::LeaderShell )
                explode( event._projectile, *pItem );
            else
                (void)applyHit( event._hit._other, event._projectile._owner, *pItem ); // 방어막이 막으면 false 이며, 맞지 않았을 뿐이다
        }
    }

    void KartRace::explode( const ProjectileState& projectile, const KartItemDef& def )
    {
        const float32 reach = projectile._blastRadius + _settings._kartRadius;
        for ( size_t racerIndex = 0; racerIndex < _listRacer.size(); ++racerIndex )
        {
            const int32 racer    = static_cast<int32>( racerIndex );
            const bool  bTarget  = racer == projectile._target;
            const bool  bInBlast = float3::getHorizontalDistanceSquared( _listRacer[racerIndex]._motor.getPosition(), projectile._position ) <= reach * reach;
            if ( bTarget || bInBlast )
                (void)applyHit( racer, projectile._owner, def ); // 방어막이 막으면 false 이며, 맞지 않았을 뿐이다
        }
    }

    bool KartRace::applyHit( int32 victim, int32 attacker, const KartItemDef& def )
    {
        if ( isValidRacer( victim ) == false )
            return false;
        KartRacer& kart = _listRacer[static_cast<size_t>( victim )];
        if ( kart._bFinished == SW_TRUE )
            return false;
        if ( kart._shieldTime.isActive() && def._bIgnoresShield == SW_FALSE )
        {
            kart._shieldTime.clear();
            pushEvent( KartRaceEvent::Kind::ShieldBlocked, victim, attacker, 0, _raceTime );
            _eventBuffer.getLast()._itemID = def._id;
            return false;
        }
        // 감속은 충격으로 처리하므로 차의 드리프트가 보상 없이 끊긴다.
        const float3& velocity = kart._motor.getVelocity();
        const float32 cut      = 1.0f - MathUtil::saturate( def._hitSpeedScale );
        kart._motor.addImpulse( float3{ -velocity._x * cut, 0.0f, -velocity._z * cut } );
        kart._spinTime.extendTo( def._spinTime );
        pushEvent( KartRaceEvent::Kind::Hit, victim, attacker, static_cast<int32>( def._kind ), _raceTime );
        _eventBuffer.getLast()._itemID = def._id;
        return true;
    }

    void KartRace::updatePlaces()
    {
        const int32 racerCount = static_cast<int32>( _listRacer.size() );
        _listPlaceOrder.resize( static_cast<size_t>( racerCount ) );
        for ( int32 racer = 0; racer < racerCount; ++racer )
        {
            _listPlaceOrder[static_cast<size_t>( racer )] = racer;
        }
        // 들어온 차는 들어온 순서로, 나머지는 진행값 순으로 정렬한다. 같으면 번호 순이다(결정적).
        VectorUtil::stableSort( _listPlaceOrder, [this]( int32 lhs, int32 rhs )
        {
            const KartRacer& left  = _listRacer[static_cast<size_t>( lhs )];
            const KartRacer& right = _listRacer[static_cast<size_t>( rhs )];
            if ( left._finishPlace > 0 || right._finishPlace > 0 )
            {
                if ( left._finishPlace > 0 && right._finishPlace > 0 )
                    return left._finishPlace < right._finishPlace;
                return left._finishPlace > 0;
            }
            return left._progress > right._progress;
        } );
        for ( int32 place = 0; place < racerCount; ++place )
        {
            KartRacer&  kart     = _listRacer[static_cast<size_t>( _listPlaceOrder[static_cast<size_t>( place )] )];
            const int32 newPlace = place + 1;
            if ( kart._place != newPlace )
            {
                const bool bAnnounce = kart._place != 0 && _phase == MatchPhase::InProgress;
                kart._place          = newPlace;
                if ( bAnnounce )
                    pushEvent( KartRaceEvent::Kind::PlaceChanged, _listPlaceOrder[static_cast<size_t>( place )], nullopt, newPlace, _raceTime );
            }
        }
    }

    float32 KartRace::computeRubberBandScale( int32 racer ) const
    {
        const bool bRubberBandDisabled = isValidRacer( racer ) == false || _settings._bRubberBand == SW_FALSE || _settings._rubberBandDistance <= 0.0f;
        if ( bRubberBandDisabled )
            return 1.0f;
        const KartRacer& kart = _listRacer[static_cast<size_t>( racer )];
        if ( kart._bAI == SW_FALSE || kart._bFinished == SW_TRUE )
            return 1.0f;
        float32 leaderProgress    = kart._progress;
        float32 bestHumanProgress = MathUtil::kMinFloat;
        for ( const KartRacer& other : _listRacer )
        {
            leaderProgress = MathUtil::max( leaderProgress, other._progress );
            if ( other._bAI == SW_FALSE )
                bestHumanProgress = MathUtil::max( bestHumanProgress, other._progress );
        }
        // 뒤처진 AI 는 1등과 벌어진 만큼 빨라지고, 사람보다 앞선 AI 1등은 조금 느려진다.
        const float32 behind = leaderProgress - kart._progress;
        if ( behind > 0.0f )
            return 1.0f + _settings._rubberBandMaxBonus * MathUtil::saturate( behind / _settings._rubberBandDistance );
        const bool bHumanBehind = bestHumanProgress > MathUtil::kMinFloat && bestHumanProgress < kart._progress;
        if ( bHumanBehind )
            return 1.0f - _settings._rubberBandLeadPenalty * MathUtil::saturate( ( kart._progress - bestHumanProgress ) / _settings._rubberBandDistance );
        return 1.0f;
    }

    void KartRace::updateRubberBand( int32 racer )
    {
        KartRacer&    kart  = _listRacer[static_cast<size_t>( racer )];
        const float32 scale = computeRubberBandScale( racer );
        if ( scale == kart._speedScale )
            return;
        kart._speedScale                  = scale;
        KinematicVehicleSettings settings = kart._baseSettings;
        settings._maxSpeed *= scale;
        kart._motor.setSettings( settings );
    }

    void KartRace::resolveRaceEnd()
    {
        if ( _phase != MatchPhase::InProgress )
            return;
        const bool bAllFinished = _finishedCount >= static_cast<int32>( _listRacer.size() );
        const bool bTimedOut    = _firstFinishTime >= 0.0f && _raceTime - _firstFinishTime >= _settings._finishTimeout;
        if ( bAllFinished == false && bTimedOut == false )
            return;
        // 남은 차는 지금 순위대로 확정한다(기록 없음).
        for ( const int32 racer : _listPlaceOrder )
        {
            KartRacer& kart = _listRacer[static_cast<size_t>( racer )];
            if ( kart._finishPlace > 0 )
                continue;
            kart._bFinished   = SW_TRUE;
            kart._finishPlace = ++_finishedCount;
            pushEvent( KartRaceEvent::Kind::Finished, racer, nullopt, kart._finishPlace, 0.0f );
        }
        _phase = MatchPhase::Ended;
        updatePlaces();
        pushEvent( KartRaceEvent::Kind::RaceEnded, findRacerAtPlace( 1 ), nullopt, 0, _raceTime );
    }

    const KartRacer* KartRace::findRacer( int32 racer ) const
    {
        return isValidRacer( racer ) ? &_listRacer[static_cast<size_t>( racer )] : nullptr;
    }

    optional<int32> KartRace::findRacerAtPlace( int32 place ) const
    {
        if ( place < 1 || place > static_cast<int32>( _listPlaceOrder.size() ) )
            return nullopt;
        return _listPlaceOrder[static_cast<size_t>( place - 1 )];
    }

    bool KartRace::isItemBoxActive( int32 boxIndex ) const
    {
        return 0 <= boxIndex && boxIndex < static_cast<int32>( _listItemBoxTimer.size() ) && _listItemBoxTimer[static_cast<size_t>( boxIndex )].isActive() == false;
    }

    void KartRace::placeRacer( int32 racer, const float3& position, float32 yaw )
    {
        if ( isValidRacer( racer ) == false )
            return;
        KartRacer& kart = _listRacer[static_cast<size_t>( racer )];
        // 남은 속도를 충격으로 지워 드리프트를 보상 없이 끊는다.
        const float3& velocity = kart._motor.getVelocity();
        kart._motor.addImpulse( float3{ -velocity._x, -velocity._y, -velocity._z } );
        kart._motor.setPosition( position );
        kart._motor.setYaw( yaw );
        kart._previousPosition = position;
        kart._distance         = _pTrack->project( position )._distance;
        kart._progress         = computeProgress( kart );
    }

    void KartRace::startGhostRecording( int32 racer, KartGhost* pGhost )
    {
        if ( isValidRacer( racer ) == false || pGhost == nullptr )
            return;
        const KartRacer& kart = _listRacer[static_cast<size_t>( racer )];
        _pGhost               = pGhost;
        _ghostRacer           = racer;
        pGhost->begin( kart._motor.getPosition(), kart._motor.getYaw(), _settings._step );
    }

    void KartRace::pollEvents( vector<KartRaceEvent>& outListEvent )
    {
        _eventBuffer.moveAllTo( outListEvent );
    }

    void KartRace::serializeState( StateIO& io )
    {
        io.expect( static_cast<uint32>( _listRacer.size() ) );
        if ( io.isOk() == false )
            return;
        const int32 racerCount = static_cast<int32>( _listRacer.size() );
        for ( KartRacer& kart : _listRacer )
        {
            KartRaceInternal::serializeRacer( io, racerCount, _pTrack, kart );
            if ( io.isWriting() )
                continue;
            if ( io.isOk() == false || io.check( isKnownItem( kart._itemID ) ) == false )
                return;
            // 러버밴딩 배율이 바꾼 최고 속도를 기본 설정에서 다시 계산한다(`updateRubberBand` 와 같은 식이다).
            KinematicVehicleSettings settings = kart._baseSettings;
            settings._maxSpeed *= kart._speedScale;
            kart._motor.setSettings( settings );
        }
        io.value( _projectiles );
        if ( io.isReading() && io.isOk() )
        {
            for ( const ProjectileState& projectile : _projectiles.getProjectiles() )
            {
                // 아이템 종류는 정의 id 가 정한다(빈 id 는 아이템이 아니다).
                io.check( projectile._defID.empty() == false && isKnownItem( projectile._defID ) && -1 <= projectile._owner && projectile._owner < racerCount && -1 <= projectile._target && projectile._target < racerCount );
            }
        }
        io.expect( static_cast<uint32>( _listItemBoxTimer.size() ) );
        if ( io.isOk() == false )
            return;
        for ( Countdown& boxTimer : _listItemBoxTimer )
        {
            io.value( boxTimer );
        }
        uint32 placeCount = static_cast<uint32>( _listPlaceOrder.size() );
        io.value( placeCount );
        if ( io.check( placeCount == 0 || placeCount == static_cast<uint32>( racerCount ) ) == false )
            return;
        if ( io.isReading() )
        {
            _listPlaceOrder.assign( placeCount, constant::invalid_index::kInt32 );
        }
        for ( int32& racer : _listPlaceOrder )
        {
            io.value( racer );
            io.check( 0 <= racer && racer < racerCount );
        }
        io.value( _random );
        io.value( _timer );
        io.value( _raceTime );
        io.value( _countdown );
        io.value( _bestLapTime );
        io.value( _firstFinishTime );
        io.value( _finishedCount );
        io.enumValue( _phase, MatchPhase::Ended );
        // 실수의 NaN 은 아카이브 읽기가 거절한다(시한이 영영 끝나지 않는 값).
        io.check( 0 <= _finishedCount && _finishedCount <= racerCount && 0.0f <= _raceTime && 0.0f <= _bestLapTime );
        if ( io.isReading() )
        {
            _eventBuffer.clear();
        }
    }

    bool KartRace::isKnownItem( const hashed_string& itemID ) const
    {
        return itemID.empty() || ( _pItemCatalog != nullptr && _pItemCatalog->find( itemID ) != nullptr );
    }

    void KartRace::pushEvent( KartRaceEvent::Kind kind, optional<int32> racer, optional<int32> other, int32 value, float32 time )
    {
        KartRaceEvent event;
        event._kind  = kind;
        event._racer = racer;
        event._other = other;
        event._value = value;
        event._time  = time;
        _eventBuffer.push( event );
    }
} // namespace sw
