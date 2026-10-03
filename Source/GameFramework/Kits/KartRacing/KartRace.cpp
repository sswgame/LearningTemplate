#include "pch.h"

#include "GameFramework/Kits/KartRacing/KartRace.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Combat/LockOnSelector.h"
#include "GameFramework/Kits/KartRacing/KartGhost.h"
#include "GameFramework/Kits/KartRacing/KartTrack.h"

namespace sw
{
    namespace
    {
        struct KartRaceInternal
        {
            static constexpr float32 kAheadPriority = 100.0f; ///< 빨간 껍질 — 바로 앞 순위의 차를 먼저 잡는다

            static float32 computeFlatDistanceSq( const float3& lhs, const float3& rhs )
            {
                const float32 deltaX = lhs._x - rhs._x;
                const float32 deltaZ = lhs._z - rhs._z;
                return deltaX * deltaX + deltaZ * deltaZ;
            }

            /** @brief @p direction 을 @p target 쪽으로 @p maxTurn(라디안)까지 돌린 수평 단위 벡터입니다. */
            static float3 turnToward( const float3& direction, const float3& from, const float3& target, float32 maxTurn )
            {
                const float32 currentYaw = MathUtil::atan2( direction._x, direction._z );
                const float32 targetYaw  = MathUtil::atan2( target._x - from._x, target._z - from._z );
                float32       delta      = targetYaw - currentYaw;
                while ( delta > MathUtil::Pi )
                    delta -= 2.0f * MathUtil::Pi;
                while ( delta < -MathUtil::Pi )
                    delta += 2.0f * MathUtil::Pi;
                const float32 newYaw = currentYaw + MathUtil::clamp( delta, -maxTurn, maxTurn );
                return float3{ MathUtil::sin( newYaw ), 0.0f, MathUtil::cos( newYaw ) };
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    KartRace::KartRace()
        : _listRacer{}
        , _listProjectile{}
        , _listEvent{}
        , _listItemBoxTimer{}
        , _listPlaceOrder{}
        , _settings{}
        , _random{}
        , _timer{}
        , _pTrack{ nullptr }
        , _pItemCatalog{ nullptr }
        , _pGhost{ nullptr }
        , _raceTime{ 0.0f }
        , _countdown{ 0.0f }
        , _bestLapTime{ 0.0f }
        , _firstFinishTime{ -1.0f }
        , _ghostRacer{ -1 }
        , _finishedCount{ 0 }
        , _phase{ KartRacePhase::Setup }
    {
    }

    void KartRace::initialize( const KartRaceSettings& settings, const KartTrack* pTrack, const KartItemCatalog* pItemCatalog )
    {
        _settings     = settings;
        _pTrack       = pTrack;
        _pItemCatalog = pItemCatalog;
        _random.setSeed( settings._seed );
        _timer = FixedStepTimer{ settings._step, 0.25f };
        _listRacer.clear();
        _listProjectile.clear();
        _listEvent.clear();
        _listPlaceOrder.clear();
        _listItemBoxTimer.clear();
        if ( pTrack != nullptr )
            _listItemBoxTimer.resize( pTrack->getItemBoxPositions().size(), 0.0f );
        _pGhost          = nullptr;
        _ghostRacer      = -1;
        _raceTime        = 0.0f;
        _countdown       = 0.0f;
        _bestLapTime     = 0.0f;
        _firstFinishTime = -1.0f;
        _finishedCount   = 0;
        _phase           = KartRacePhase::Setup;
    }

    int32 KartRace::addRacer( const ArcadeVehicleSettings& vehicleSettings, bool bAi )
    {
        if ( _phase != KartRacePhase::Setup || _pTrack == nullptr || _pTrack->isValid() == false )
            return -1;
        _listRacer.emplace_back();
        KartRacer& kart    = _listRacer.back();
        kart._baseSettings = vehicleSettings;
        kart._bAi          = bAi ? SW_TRUE : SW_FALSE;
        kart._motor.setSettings( vehicleSettings );
        kart._motor.setGround( _pTrack );
        return static_cast<int32>( _listRacer.size() ) - 1;
    }

    void KartRace::setRacerAi( int32 racer, const KartAiSettings& settings )
    {
        if ( isValidRacer( racer ) )
            _listRacer[static_cast<size_t>( racer )]._ai.setSettings( settings );
    }

    void KartRace::start()
    {
        if ( _phase != KartRacePhase::Setup || _listRacer.empty() || _pTrack == nullptr )
            return;
        // 격자 — 결승선 뒤로 두 줄씩, 왼쪽 · 오른쪽 번갈아.
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
            kart._nextCheckpoint   = _pTrack->getCheckpointCount(); // 출발선 전은 "체크포인트를 다 지난" 것으로 본다
            kart._ai.reset();
            kart._progress = computeProgress( kart );
        }
        _phase     = KartRacePhase::Countdown;
        _countdown = _settings._countdownTime;
        _raceTime  = 0.0f;
        updatePlaces();
        if ( _countdown <= 0.0f )
        {
            _phase = KartRacePhase::Racing;
            pushEvent( KartRaceEvent::Kind::RaceStarted, -1, -1, 0, 0.0f );
        }
    }

    void KartRace::setInput( int32 racer, const KartRacerInput& input )
    {
        if ( isValidRacer( racer ) == false )
            return;
        // 눌림은 다음 걸음까지 남긴다(프레임 안에 걸음이 없어도 사라지지 않게).
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
            step();
        return stepCount;
    }

    void KartRace::step()
    {
        const float32 deltaTime = _settings._step;
        if ( _phase == KartRacePhase::Countdown )
        {
            _countdown -= deltaTime;
            if ( _countdown <= 0.0f )
            {
                _countdown = 0.0f;
                _phase     = KartRacePhase::Racing;
                pushEvent( KartRaceEvent::Kind::RaceStarted, -1, -1, 0, 0.0f );
            }
            return;
        }
        if ( _phase != KartRacePhase::Racing && _phase != KartRacePhase::Ended )
            return;
        if ( _phase == KartRacePhase::Racing )
            _raceTime += deltaTime;

        // 1) 차 — 입력 → 아이템 → 러버밴딩 → 걸음 → 부스트 패드 → 진행.
        const int32 racerCount = static_cast<int32>( _listRacer.size() );
        for ( int32 racer = 0; racer < racerCount; ++racer )
        {
            KartRacer&               kart     = _listRacer[static_cast<size_t>( racer )];
            bool                     bUseItem = false;
            const ArcadeVehicleInput input    = resolveInput( racer, bUseItem );
            const bool               bRecord  = _pGhost != nullptr && racer == _ghostRacer && kart._bFinished == SW_FALSE;
            if ( bRecord )
                _pGhost->record( input, bUseItem );
            if ( bUseItem )
                (void)useItem( racer );
            updateRubberBand( racer );

            kart._previousPosition = kart._motor.getPosition();
            kart._motor.update( input, deltaTime );
            if ( _pTrack->applyBoostPad( kart._motor, kart._lastBoostPad ) )
                pushEvent( KartRaceEvent::Kind::BoostPad, racer, -1, kart._lastBoostPad, _raceTime );
            vector<ArcadeVehicleEvent> listMotorEvent;
            kart._motor.drainEvents( listMotorEvent ); // 경기는 차 알림을 쓰지 않는다 — 쌓이지 않게 비운다

            if ( kart._spinTime > 0.0f )
                kart._spinTime = MathUtil::max( 0.0f, kart._spinTime - deltaTime );
            if ( kart._shieldTime > 0.0f )
                kart._shieldTime = MathUtil::max( 0.0f, kart._shieldTime - deltaTime );
            if ( kart._itemId.empty() == false )
                kart._itemHeldTime += deltaTime;
            updateProgress( racer );
        }

        // 2) 상자 · 아이템 · 순위 · 끝.
        updateItemBoxes( deltaTime );
        updateProjectiles( deltaTime );
        updatePlaces();
        resolveRaceEnd();
    }

    ArcadeVehicleInput KartRace::resolveInput( int32 racer, bool& outUseItem )
    {
        KartRacer& kart = _listRacer[static_cast<size_t>( racer )];
        outUseItem      = false;
        if ( kart._spinTime > 0.0f )
        {
            kart._input = KartRacerInput{}; // 도는 동안의 눌림은 버린다
            return ArcadeVehicleInput{};
        }
        if ( kart._bAi != SW_FALSE )
        {
            KartAiContext context;
            context._bHasItem        = kart._itemId.empty() ? SW_FALSE : SW_TRUE;
            context._itemHeldTime    = kart._itemHeldTime;
            context._bShielded       = kart._shieldTime > 0.0f ? SW_TRUE : SW_FALSE;
            const KartItemDef* pItem = _pItemCatalog != nullptr && kart._itemId.empty() == false ? _pItemCatalog->findItem( kart._itemId ) : nullptr;
            if ( pItem != nullptr )
                context._itemKind = pItem->_kind;
            const int32 ahead  = findRacerAtPlace( kart._place - 1 );
            const int32 behind = findRacerAtPlace( kart._place + 1 );
            if ( ahead >= 0 )
                context._gapAhead = _listRacer[static_cast<size_t>( ahead )]._progress - kart._progress;
            if ( behind >= 0 )
                context._gapBehind = kart._progress - _listRacer[static_cast<size_t>( behind )]._progress;
            outUseItem = kart._ai.shouldUseItem( context );
            return kart._ai.computeInput( *_pTrack, kart._motor, kart._distance );
        }
        // 사람 — 양자화한 입력으로 달린다(고스트 · 락스텝이 같은 경로를 내게).
        const ArcadeVehicleInput input      = KartGhost::quantizeInput( kart._input._vehicle );
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

        // 문 — 다음 체크포인트(앞) · 지난 체크포인트(뒤) · 결승선.
        const int32 checkpointCount = _pTrack->getCheckpointCount();
        if ( kart._nextCheckpoint < checkpointCount && _pTrack->computeGateCrossing( kart._nextCheckpoint + 1, kart._previousPosition, position ) > 0 )
        {
            ++kart._nextCheckpoint;
            pushEvent( KartRaceEvent::Kind::CheckpointPassed, racer, -1, kart._nextCheckpoint, _raceTime );
        }
        else if ( kart._nextCheckpoint > 0 && _pTrack->computeGateCrossing( kart._nextCheckpoint, kart._previousPosition, position ) < 0 )
        {
            --kart._nextCheckpoint;
        }
        const int32 finishCrossing = _pTrack->computeGateCrossing( 0, kart._previousPosition, position );
        if ( finishCrossing > 0 )
        {
            if ( kart._nextCheckpoint >= checkpointCount )
                completeLap( racer );
            else
                pushEvent( KartRaceEvent::Kind::CheckpointMissed, racer, -1, kart._nextCheckpoint, _raceTime );
        }
        else if ( finishCrossing < 0 && kart._nextCheckpoint == 0 && kart._lap > 0 && kart._bFinished == SW_FALSE )
        {
            // 결승선을 뒤로 넘었다 — 바퀴를 되돌리고 그 바퀴 기록을 지운다.
            --kart._lap;
            kart._nextCheckpoint = checkpointCount;
            if ( kart._listLapTime.empty() == false && kart._lap >= 1 )
            {
                kart._lapStartTime -= kart._listLapTime.back();
                kart._listLapTime.pop_back();
                kart._bestLapTime = 0.0f;
                for ( const float32 lapTime : kart._listLapTime )
                    kart._bestLapTime = kart._bestLapTime <= 0.0f ? lapTime : MathUtil::min( kart._bestLapTime, lapTime );
            }
            pushEvent( KartRaceEvent::Kind::LapRevoked, racer, -1, kart._lap, _raceTime );
        }

        // 역주행 — 트랙 방향 속도가 이어서 음수다.
        const float3& velocity   = kart._motor.getVelocity();
        const float32 alongSpeed = velocity._x * projection._tangent._x + velocity._z * projection._tangent._z;
        if ( alongSpeed < -_settings._wrongWaySpeed && kart._spinTime <= 0.0f )
        {
            kart._wrongWayTime += _settings._step;
            if ( kart._bWrongWay == SW_FALSE && kart._wrongWayTime >= _settings._wrongWayDelay )
            {
                kart._bWrongWay = SW_TRUE;
                pushEvent( KartRaceEvent::Kind::WrongWay, racer, -1, 1, _raceTime );
            }
        }
        else if ( alongSpeed > _settings._wrongWaySpeed )
        {
            kart._wrongWayTime = 0.0f;
            if ( kart._bWrongWay == SW_TRUE )
            {
                kart._bWrongWay = SW_FALSE;
                pushEvent( KartRaceEvent::Kind::WrongWay, racer, -1, 0, _raceTime );
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
            pushEvent( KartRaceEvent::Kind::LapCompleted, racer, -1, kart._lap, lapTime );
            if ( _bestLapTime <= 0.0f || lapTime < _bestLapTime )
            {
                _bestLapTime = lapTime;
                pushEvent( KartRaceEvent::Kind::NewBestLap, racer, -1, kart._lap, lapTime );
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
        pushEvent( KartRaceEvent::Kind::LapStarted, racer, -1, kart._lap, _raceTime );
        if ( kart._lap == _pTrack->getLapCount() )
            pushEvent( KartRaceEvent::Kind::FinalLap, racer, -1, kart._lap, _raceTime );
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
        pushEvent( KartRaceEvent::Kind::Finished, racer, -1, kart._finishPlace, _raceTime );
    }

    float32 KartRace::computeProgress( const KartRacer& kart ) const
    {
        const float32 length          = _pTrack->getLength();
        const int32   checkpointCount = _pTrack->getCheckpointCount();
        // 지금 있어야 할 두 문 사이로 자른다 — 문을 건너뛴 차는 다음 문보다 앞설 수 없다.
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
            if ( _listItemBoxTimer[boxIndex] > 0.0f )
            {
                _listItemBoxTimer[boxIndex] = MathUtil::max( 0.0f, _listItemBoxTimer[boxIndex] - deltaTime );
                continue;
            }
            if ( _settings._bItems == SW_FALSE || _pItemCatalog == nullptr )
                continue;
            const float32 reach = _settings._itemBoxRadius + _settings._kartRadius;
            for ( size_t racerIndex = 0; racerIndex < _listRacer.size(); ++racerIndex )
            {
                KartRacer& kart = _listRacer[racerIndex];
                if ( kart._bFinished == SW_TRUE || KartRaceInternal::computeFlatDistanceSq( kart._motor.getPosition(), listBox[boxIndex] ) > reach * reach )
                    continue;
                // 상자는 들고 있어도 깨진다 — 빈 손이면 순위 표로 굴린다.
                _listItemBoxTimer[boxIndex] = _settings._itemBoxRespawn;
                hashed_string itemId;
                if ( kart._itemId.empty() )
                {
                    const KartItemDef* pItem = _pItemCatalog->rollItem( kart._place, static_cast<int32>( _listRacer.size() ), _random );
                    if ( pItem != nullptr )
                    {
                        kart._itemId       = pItem->_id;
                        kart._itemHeldTime = 0.0f;
                        itemId             = pItem->_id;
                    }
                }
                pushEvent( KartRaceEvent::Kind::ItemBoxTaken, static_cast<int32>( racerIndex ), -1, static_cast<int32>( boxIndex ), _raceTime );
                _listEvent.back()._itemId = itemId;
                break;
            }
        }
    }

    void KartRace::giveItem( int32 racer, const hashed_string& itemId )
    {
        if ( isValidRacer( racer ) == false )
            return;
        KartRacer& kart    = _listRacer[static_cast<size_t>( racer )];
        kart._itemId       = itemId;
        kart._itemHeldTime = 0.0f;
    }

    bool KartRace::useItem( int32 racer )
    {
        if ( isValidRacer( racer ) == false || _pItemCatalog == nullptr )
            return false;
        KartRacer&         kart  = _listRacer[static_cast<size_t>( racer )];
        const KartItemDef* pItem = kart._itemId.empty() ? nullptr : _pItemCatalog->findItem( kart._itemId );
        if ( pItem == nullptr )
            return false;
        kart._itemId       = hashed_string{};
        kart._itemHeldTime = 0.0f;
        pushEvent( KartRaceEvent::Kind::ItemUsed, racer, -1, static_cast<int32>( pItem->_kind ), _raceTime );
        _listEvent.back()._itemId = pItem->_id;

        const float3   forward  = kart._motor.computeForward();
        const float3&  position = kart._motor.getPosition();
        KartProjectile projectile;
        projectile._itemId    = pItem->_id;
        projectile._kind      = pItem->_kind;
        projectile._owner     = racer;
        projectile._direction = forward;
        const float32 launch  = _settings._kartRadius + pItem->_radius + 0.5f;
        projectile._position  = float3{ position._x + forward._x * launch, position._y, position._z + forward._z * launch };
        switch ( pItem->_kind )
        {
            case KartItemKind::Banana:
            {
                projectile._position = float3{ position._x - forward._x * _settings._dropBehindDistance, position._y,
                                               position._z - forward._z * _settings._dropBehindDistance };
                _listProjectile.push_back( projectile );
                break;
            }
            case KartItemKind::GreenShell:
            {
                _listProjectile.push_back( projectile );
                break;
            }
            case KartItemKind::RedShell:
            {
                projectile._target = pickRedShellTarget( racer );
                _listProjectile.push_back( projectile );
                break;
            }
            case KartItemKind::LeaderShell:
            {
                projectile._target = findRacerAtPlace( 1 );
                _listProjectile.push_back( projectile );
                break;
            }
            case KartItemKind::Booster:
            {
                kart._motor.startBoost( pItem->_boostTime, -1 );
                break;
            }
            case KartItemKind::Shield:
            {
                kart._shieldTime = pItem->_shieldTime;
                break;
            }
        }
        return true;
    }

    int32 KartRace::pickRedShellTarget( int32 owner ) const
    {
        // 기반 록온 — 앞 콘 안의 차를 고르되, 바로 앞 순위의 차에 큰 우선도를 준다(없으면 콘 안에서 가장 좋은 차, 그것도 없으면 곧게).
        const KartRacer&        shooter = _listRacer[static_cast<size_t>( owner )];
        const int32             ahead   = findRacerAtPlace( shooter._place - 1 );
        vector<LockOnCandidate> listCandidate;
        for ( size_t racerIndex = 0; racerIndex < _listRacer.size(); ++racerIndex )
        {
            if ( static_cast<int32>( racerIndex ) == owner || _listRacer[racerIndex]._bFinished == SW_TRUE )
                continue;
            LockOnCandidate candidate;
            candidate._position = _listRacer[racerIndex]._motor.getPosition();
            candidate._id       = static_cast<uint64>( racerIndex ) + 1;
            candidate._priority = static_cast<int32>( racerIndex ) == ahead ? KartRaceInternal::kAheadPriority : 0.0f;
            listCandidate.push_back( candidate );
        }
        LockOnSettings lockSettings;
        lockSettings._maxDistance   = _settings._redShellLockDistance;
        lockSettings._breakDistance = _settings._redShellLockDistance;
        lockSettings._maxAngle      = _settings._redShellLockAngle;
        LockOnSelector selector;
        selector.setSettings( lockSettings );
        const uint64 picked = selector.pickBest( shooter._motor.getPosition(), shooter._motor.computeForward(), listCandidate );
        return picked == 0 ? -1 : static_cast<int32>( picked - 1 );
    }

    void KartRace::updateProjectiles( float32 deltaTime )
    {
        if ( _pItemCatalog == nullptr )
            return;
        const float32 kartRadius = _settings._kartRadius;
        for ( size_t projectileIndex = 0; projectileIndex < _listProjectile.size(); ++projectileIndex )
        {
            KartProjectile&    projectile = _listProjectile[projectileIndex];
            const KartItemDef* pItem      = _pItemCatalog->findItem( projectile._itemId );
            if ( pItem == nullptr || projectile._bActive == SW_FALSE )
            {
                projectile._bActive = SW_FALSE;
                continue;
            }
            projectile._age += deltaTime;
            const bool bExpires = pItem->_lifetime > 0.0f && projectile._age > pItem->_lifetime;
            if ( bExpires )
            {
                projectile._bActive = SW_FALSE;
                continue;
            }

            // 1) 움직임 — 바나나는 멈춰 있고, 1 등 공격은 늘 지금의 1 등을 쫓는다.
            if ( projectile._kind == KartItemKind::LeaderShell )
                projectile._target = findRacerAtPlace( 1 );
            if ( projectile._kind != KartItemKind::Banana )
            {
                if ( isValidRacer( projectile._target ) )
                {
                    const float3& targetPosition = _listRacer[static_cast<size_t>( projectile._target )]._motor.getPosition();
                    projectile._direction =
                        KartRaceInternal::turnToward( projectile._direction, projectile._position, targetPosition, pItem->_turnRate * deltaTime );
                }
                projectile._position._x += projectile._direction._x * pItem->_speed * deltaTime;
                projectile._position._z += projectile._direction._z * pItem->_speed * deltaTime;
                projectile._position._y = _pTrack->sampleHeight( projectile._position._x, projectile._position._z );
            }

            // 2) 맞음 — 쏜 차는 잠깐 맞지 않는다. 1 등 공격은 대상에게만 터진다.
            const float32 reach = pItem->_radius + kartRadius;
            for ( size_t racerIndex = 0; racerIndex < _listRacer.size(); ++racerIndex )
            {
                const int32      racer       = static_cast<int32>( racerIndex );
                const KartRacer& kart        = _listRacer[racerIndex];
                const bool       bOwnerGrace = racer == projectile._owner && projectile._age < _settings._ownerGrace;
                const bool       bNotTarget  = projectile._kind == KartItemKind::LeaderShell && racer != projectile._target;
                if ( kart._bFinished == SW_TRUE || bOwnerGrace || bNotTarget )
                    continue;
                if ( KartRaceInternal::computeFlatDistanceSq( kart._motor.getPosition(), projectile._position ) > reach * reach )
                    continue;
                projectile._bActive = SW_FALSE;
                if ( projectile._kind == KartItemKind::LeaderShell )
                    explode( projectile, *pItem );
                else
                    (void)applyHit( racer, projectile._owner, *pItem );
                break;
            }
        }
        size_t writeIndex = 0;
        for ( size_t readIndex = 0; readIndex < _listProjectile.size(); ++readIndex )
        {
            if ( _listProjectile[readIndex]._bActive == SW_TRUE )
                _listProjectile[writeIndex++] = _listProjectile[readIndex];
        }
        _listProjectile.resize( writeIndex );
    }

    void KartRace::explode( const KartProjectile& projectile, const KartItemDef& def )
    {
        const float32 reach = def._blastRadius + _settings._kartRadius;
        for ( size_t racerIndex = 0; racerIndex < _listRacer.size(); ++racerIndex )
        {
            const int32 racer    = static_cast<int32>( racerIndex );
            const bool  bTarget  = racer == projectile._target;
            const bool  bInBlast = KartRaceInternal::computeFlatDistanceSq( _listRacer[racerIndex]._motor.getPosition(), projectile._position ) <= reach * reach;
            if ( bTarget || bInBlast )
                (void)applyHit( racer, projectile._owner, def );
        }
    }

    bool KartRace::applyHit( int32 victim, int32 attacker, const KartItemDef& def )
    {
        if ( isValidRacer( victim ) == false )
            return false;
        KartRacer& kart = _listRacer[static_cast<size_t>( victim )];
        if ( kart._bFinished == SW_TRUE )
            return false;
        if ( kart._shieldTime > 0.0f && def._bIgnoresShield == SW_FALSE )
        {
            kart._shieldTime = 0.0f;
            pushEvent( KartRaceEvent::Kind::ShieldBlocked, victim, attacker, 0, _raceTime );
            _listEvent.back()._itemId = def._id;
            return false;
        }
        // 감속은 충격으로 — 차의 드리프트가 보상 없이 끊긴다.
        const float3& velocity = kart._motor.getVelocity();
        const float32 cut      = 1.0f - MathUtil::saturate( def._hitSpeedScale );
        kart._motor.addImpulse( float3{ -velocity._x * cut, 0.0f, -velocity._z * cut } );
        kart._spinTime = MathUtil::max( kart._spinTime, def._spinTime );
        pushEvent( KartRaceEvent::Kind::Hit, victim, attacker, static_cast<int32>( def._kind ), _raceTime );
        _listEvent.back()._itemId = def._id;
        return true;
    }

    void KartRace::updatePlaces()
    {
        const int32 racerCount = static_cast<int32>( _listRacer.size() );
        _listPlaceOrder.resize( static_cast<size_t>( racerCount ) );
        for ( int32 racer = 0; racer < racerCount; ++racer )
            _listPlaceOrder[static_cast<size_t>( racer )] = racer;
        // 들어온 차는 들어온 순서, 나머지는 진행값 — 같으면 번호(결정적).
        std::stable_sort( _listPlaceOrder.begin(), _listPlaceOrder.end(), [this]( int32 lhs, int32 rhs )
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
                const bool bAnnounce = kart._place != 0 && _phase == KartRacePhase::Racing;
                kart._place          = newPlace;
                if ( bAnnounce )
                    pushEvent( KartRaceEvent::Kind::PlaceChanged, _listPlaceOrder[static_cast<size_t>( place )], -1, newPlace, _raceTime );
            }
        }
    }

    float32 KartRace::computeRubberBandScale( int32 racer ) const
    {
        if ( isValidRacer( racer ) == false || _settings._bRubberBand == SW_FALSE || _settings._rubberBandDistance <= 0.0f )
            return 1.0f;
        const KartRacer& kart = _listRacer[static_cast<size_t>( racer )];
        if ( kart._bAi == SW_FALSE || kart._bFinished == SW_TRUE )
            return 1.0f;
        float32 leaderProgress    = kart._progress;
        float32 bestHumanProgress = MathUtil::MinFloat;
        for ( const KartRacer& other : _listRacer )
        {
            leaderProgress = MathUtil::max( leaderProgress, other._progress );
            if ( other._bAi == SW_FALSE )
                bestHumanProgress = MathUtil::max( bestHumanProgress, other._progress );
        }
        // 뒤처진 AI 는 1 등과 벌어진 만큼 빨라지고, 사람보다 앞선 AI 1 등은 조금 느려진다.
        const float32 behind = leaderProgress - kart._progress;
        if ( behind > 0.0f )
            return 1.0f + _settings._rubberBandMaxBonus * MathUtil::saturate( behind / _settings._rubberBandDistance );
        const bool bHumanBehind = bestHumanProgress > MathUtil::MinFloat && bestHumanProgress < kart._progress;
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
        kart._speedScale               = scale;
        ArcadeVehicleSettings settings = kart._baseSettings;
        settings._maxSpeed *= scale;
        kart._motor.setSettings( settings );
    }

    void KartRace::resolveRaceEnd()
    {
        if ( _phase != KartRacePhase::Racing )
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
            pushEvent( KartRaceEvent::Kind::Finished, racer, -1, kart._finishPlace, 0.0f );
        }
        _phase = KartRacePhase::Ended;
        updatePlaces();
        pushEvent( KartRaceEvent::Kind::RaceEnded, findRacerAtPlace( 1 ), -1, 0, _raceTime );
    }

    const KartRacer* KartRace::findRacer( int32 racer ) const
    {
        return isValidRacer( racer ) ? &_listRacer[static_cast<size_t>( racer )] : nullptr;
    }

    int32 KartRace::findRacerAtPlace( int32 place ) const
    {
        if ( place < 1 || place > static_cast<int32>( _listPlaceOrder.size() ) )
            return -1;
        return _listPlaceOrder[static_cast<size_t>( place - 1 )];
    }

    bool KartRace::isItemBoxActive( int32 boxIndex ) const
    {
        return 0 <= boxIndex && boxIndex < static_cast<int32>( _listItemBoxTimer.size() ) && _listItemBoxTimer[static_cast<size_t>( boxIndex )] <= 0.0f;
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

    void KartRace::drainEvents( vector<KartRaceEvent>& outListEvent )
    {
        outListEvent.swap( _listEvent );
        _listEvent.clear();
    }

    void KartRace::pushEvent( KartRaceEvent::Kind kind, int32 racer, int32 other, int32 value, float32 time )
    {
        KartRaceEvent event;
        event._kind  = kind;
        event._racer = racer;
        event._other = other;
        event._value = value;
        event._time  = time;
        _listEvent.push_back( event );
    }
} // namespace sw
