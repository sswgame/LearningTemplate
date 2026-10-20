#include "pch.h"

#include "GameFramework/Kits/Genre/Simulation/ThemePark/ThemePark.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Framework/SessionStateRefs.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Base/Gameplay/Inventory/Shop.h"
#include "GameFramework/Kits/Genre/Simulation/ThemePark/CoasterTrain.h"

namespace sw
{
    namespace
    {
        struct ThemeParkInternal
        {
            static constexpr uint32 kMinRideStateBytes  = 64; ///< 놀이기구 하나의 상태가 최소한 차지하는 바이트 수(개수 상한 계산용)
            static constexpr uint32 kMinGuestStateBytes = 80; ///< 손님 하나의 상태가 최소한 차지하는 바이트 수(개수 상한 계산용)

            static float3 lerpPosition( const float3& from, const float3& to, float32 alpha )
            {
                return from + ( to - from ) * MathUtil::clamp( alpha, 0.0f, 1.0f );
            }

            /** @brief 손님 id 목록 하나(개수 + id)를 읽습니다. */
            [[nodiscard]] static bool readIDList( Archive& archive, vector<uint32>& outListID )
            {
                uint32 count = 0;
                if ( StateArchiveUtil::readCount( archive, sizeof( uint32 ), count ) == false )
                    return false;
                outListID.resize( count );
                for ( uint32& id : outListID )
                {
                    archive >> id;
                }
                return archive.isOk();
            }

            /** @brief `writeState` 가 쓴 놀이기구 하나를 읽습니다. */
            [[nodiscard]] static bool readRide( Archive& archive, ParkRide& outRide )
            {
                if ( StateArchiveUtil::readName( archive, outRide._id ) == false )
                    return false;
                archive >> outRide._name;
                archive >> outRide._excitement;
                archive >> outRide._intensity;
                archive >> outRide._nausea;
                archive >> outRide._cycleTime;
                archive >> outRide._entrance;
                archive >> outRide._capacity;
                archive >> outRide._price;
                archive >> outRide._runningCostPerMinute;
                archive >> outRide._bOpen;
                if ( readIDList( archive, outRide._listQueue ) == false || readIDList( archive, outRide._listRider ) == false )
                    return false;
                archive >> outRide._cycleTimer._remaining;
                archive >> outRide._totalRiders;
                archive >> outRide._totalIncome;
                return archive.isError() == false;
            }

            /** @brief `writeState` 가 쓴 손님 하나를 읽습니다. 열거형이 범위 밖이거나 목표 놀이기구가 @p rideCount 밖이면 false 입니다. */
            [[nodiscard]] static bool readGuest( Archive& archive, uint32 rideCount, ParkGuest& outGuest )
            {
                uint8 state   = 0;
                uint8 thought = 0;
                archive >> outGuest._position;
                archive >> outGuest._walkFrom;
                archive >> outGuest._walkTo;
                archive >> outGuest._id;
                archive >> outGuest._cash;
                archive >> outGuest._targetRideIndex;
                archive >> outGuest._rideCount;
                archive >> outGuest._happiness;
                archive >> outGuest._nausea;
                archive >> outGuest._energy;
                archive >> outGuest._minIntensity;
                archive >> outGuest._maxIntensity;
                archive >> outGuest._nauseaTolerance;
                archive >> outGuest._walkTimer;
                archive >> outGuest._walkDuration;
                archive >> outGuest._queueTime;
                archive >> state;
                archive >> thought;
                const bool bEnumValid        = state <= static_cast<uint8>( ParkGuestState::Left ) && thought <= static_cast<uint8>( ParkGuestThought::NothingToRide );
                const bool bRideValid        = -1 <= outGuest._targetRideIndex && outGuest._targetRideIndex < static_cast<int32>( rideCount );
                const bool bInvalidGuestData = archive.isError() || bEnumValid == false || bRideValid == false;
                if ( bInvalidGuestData )
                    return false;
                outGuest._state   = static_cast<ParkGuestState>( state );
                outGuest._thought = static_cast<ParkGuestThought>( thought );
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ThemeParkSimulation::ThemeParkSimulation()
        : _settings{}
        , _listRide{}
        , _listGuest{}
        , _arrival{}
        , _runningCost{}
        , _elapsedTime{ 0.0f }
        , _stepTimer{}
        , _random{ 12345u }
        , _pWallet{ nullptr }
        , _pLand{ nullptr }
        , _parkRating{ 0 }
        , _landOwner{ LandRegistry::kNoOwner }
        , _nextGuestID{ 1 }
        , _totalVisitorCount{ 0 }
    {
    }

    void ThemeParkSimulation::initialize( const ThemeParkSettings& settings, const SessionStateRefs& refs )
    {
        _settings = settings;
        _pWallet  = refs._pWallet;
        _listRide.clear();
        _listGuest.clear();
        _arrival.reset();
        _runningCost.reset();
        _elapsedTime       = 0.0f;
        _stepTimer         = FixedStepTimer( settings._fixedStep, settings._maxFrameTime );
        _nextGuestID       = 1;
        _totalVisitorCount = 0;
        _random.setSeed( settings._randomSeed != 0 ? settings._randomSeed : 12345u );
        updateParkRating();
    }

    void ThemeParkSimulation::update( float32 deltaTime )
    {
        const int32 stepCount = _stepTimer.consume( deltaTime );
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            stepFixed( _stepTimer.getStep() );
        }
    }

    void ThemeParkSimulation::bindLand( LandRegistry* pLand )
    {
        _pLand     = pLand;
        _landOwner = pLand != nullptr ? pLand->registerOwner( hashed_string( "ThemePark" ) ) : LandRegistry::kNoOwner;
    }

    int32 ThemeParkSimulation::buildRide( const ParkRide& ride, int32 buildCost )
    {
        const bool bClaimLand = _pLand != nullptr && _landOwner != LandRegistry::kNoOwner && ride._footprintSize._x > 0.0f && ride._footprintSize._z > 0.0f;
        if ( bClaimLand && _pLand->claimWorldRect( _landOwner, ride._footprintCenter, ride._footprintSize, true ) == false )
            return -1;
        if ( _pWallet == nullptr || _pWallet->trySpend( _settings._currency, MathUtil::max( 0, buildCost ) ) == false )
        {
            if ( bClaimLand )
                _pLand->releaseWorldRect( _landOwner, ride._footprintCenter, ride._footprintSize );
            return -1;
        }
        ParkRide built = ride;
        built._listQueue.clear();
        built._listRider.clear();
        built._cycleTimer.clear();
        built._totalRiders = 0;
        built._totalIncome = 0;
        built._capacity    = MathUtil::max( 1, built._capacity );
        built._cycleTime   = MathUtil::max( 1.0f, built._cycleTime );
        _listRide.push_back( built );
        updateParkRating();
        return static_cast<int32>( _listRide.size() ) - 1;
    }

    void ThemeParkSimulation::setRideOpen( int32 rideIndex, bool bOpen )
    {
        if ( rideIndex < 0 || rideIndex >= static_cast<int32>( _listRide.size() ) )
            return;
        ParkRide& ride = _listRide[static_cast<size_t>( rideIndex )];
        ride._bOpen    = bOpen ? SW_TRUE : SW_FALSE;
        if ( bOpen )
            return;
        // 닫으면 줄이 흩어진다(타고 있는 손님은 이번 바퀴를 마저 돈다).
        const vector<uint32> listQueued = ride._listQueue;
        ride._listQueue.clear();
        for ( const uint32 guestID : listQueued )
        {
            ParkGuest* pGuest = findGuest( guestID );
            if ( pGuest != nullptr )
            {
                pGuest->_targetRideIndex = -1;
                chooseNextRide( *pGuest );
            }
        }
    }

    void ThemeParkSimulation::setRidePrice( int32 rideIndex, int32 price )
    {
        if ( 0 <= rideIndex && rideIndex < static_cast<int32>( _listRide.size() ) )
            _listRide[static_cast<size_t>( rideIndex )]._price = MathUtil::max( 0, price );
    }

    bool ThemeParkSimulation::admitGuest( int32 cash, float32 minIntensity, float32 maxIntensity, float32 nauseaTolerance )
    {
        if ( static_cast<int32>( getGuestCount() ) >= _settings._maxGuests || cash < _settings._entryFee )
            return false;

        ParkGuest guest;
        guest._id              = _nextGuestID++;
        guest._position        = _settings._gatePosition;
        guest._walkFrom        = guest._position;
        guest._walkTo          = guest._position;
        guest._cash            = cash - _settings._entryFee;
        guest._minIntensity    = minIntensity;
        guest._maxIntensity    = MathUtil::max( minIntensity, maxIntensity );
        guest._nauseaTolerance = nauseaTolerance;
        guest._state           = ParkGuestState::Walking;
        guest._walkTimer       = 0.0f; // 다음 갱신에 바로 고른다
        if ( _pWallet != nullptr )
            _pWallet->add( _settings._currency, _settings._entryFee );
        ++_totalVisitorCount;
        _listGuest.push_back( guest );
        return true;
    }

    ParkRide ThemeParkSimulation::makeRideFromCoaster( const hashed_string& id, const string& name, const CoasterRideStats& stats, int32 capacity, float32 loadTime )
    {
        ParkRide ride;
        ride._id         = id;
        ride._name       = name;
        ride._excitement = stats._excitement;
        ride._intensity  = stats._intensity;
        ride._nausea     = stats._nausea;
        ride._cycleTime  = MathUtil::max( 1.0f, stats._lapTime + loadTime );
        ride._capacity   = MathUtil::max( 1, capacity );
        // 시작 값은 가치만큼 받는다(게임이 바꾼다).
        ride._price = static_cast<int32>( MathUtil::round( computeRideValue( ride ) ) );
        return ride;
    }

    float32 ThemeParkSimulation::computeRideValue( const ParkRide& ride )
    {
        return ride._excitement * 2.0f;
    }

    uint32 ThemeParkSimulation::getGuestCount() const
    {
        uint32 guestCount = 0;
        for ( const ParkGuest& guest : _listGuest )
        {
            if ( guest._state != ParkGuestState::Left )
                ++guestCount;
        }
        return guestCount;
    }

    float32 ThemeParkSimulation::getAverageHappiness() const
    {
        float32 total      = 0.0f;
        uint32  guestCount = 0;
        for ( const ParkGuest& guest : _listGuest )
        {
            if ( guest._state == ParkGuestState::Left )
                continue;
            total += guest._happiness;
            ++guestCount;
        }
        return guestCount > 0 ? total / static_cast<float32>( guestCount ) : 0.0f;
    }

    uint32 ThemeParkSimulation::countGuestsThinking( ParkGuestThought thought ) const
    {
        uint32 thinkingCount = 0;
        for ( const ParkGuest& guest : _listGuest )
        {
            if ( guest._state != ParkGuestState::Left && guest._thought == thought )
                ++thinkingCount;
        }
        return thinkingCount;
    }

    // ------------------------------------------------------------------------------
    // 한 간격
    // ------------------------------------------------------------------------------
    void ThemeParkSimulation::stepFixed( float32 deltaTime )
    {
        _elapsedTime += deltaTime;
        spawnGuests( deltaTime );
        runRides( deltaTime );
        updateGuests( deltaTime );

        // 운영비는 열린 놀이기구마다 분당 나간다.
        float32 costPerSecond = 0.0f;
        for ( const ParkRide& ride : _listRide )
        {
            if ( ride._bOpen == SW_TRUE )
                costPerSecond += static_cast<float32>( ride._runningCostPerMinute ) / 60.0f;
        }
        _runningCost.add( costPerSecond * deltaTime );
        const int32 runningCost = _runningCost.takeWhole();
        if ( _pWallet != nullptr )
            _pWallet->charge( _settings._currency, runningCost );

        // 떠난 손님을 지운다(놀이기구의 줄과 탑승자에는 남아 있지 않다. 떠나기 전에 뺐다). 한 번에 당겨 담아 순서(id 오름차순)를 지킨다.
        // 손님마다 `erase` 하면 뒤를 매번 옮기게 된다.
        VectorUtil::removeIf( _listGuest, []( const ParkGuest& guest )
        { return guest._state == ParkGuestState::Left; } );
        updateParkRating();
    }

    void ThemeParkSimulation::spawnGuests( float32 deltaTime )
    {
        const float32 feeFactor    = MathUtil::max( 0.0f, 1.0f - static_cast<float32>( _settings._entryFee ) / 100.0f );
        const float32 ratingFactor = 0.4f + static_cast<float32>( _parkRating ) / 1000.0f;
        const float32 rideFactor   = _listRide.empty() ? 0.2f : 1.0f; // 놀이기구 없는 공원에는 거의 오지 않는다
        _arrival.add( _settings._guestArrivalPerMinute / 60.0f * ratingFactor * feeFactor * rideFactor * deltaTime );
        while ( _arrival.takeOne() )
        {
            const int32   cash            = _settings._guestCashMin + static_cast<int32>( nextRandom() * static_cast<float32>( _settings._guestCashMax - _settings._guestCashMin ) );
            const float32 minIntensity    = nextRandom() * _settings._guestMinIntensityMax;
            const float32 maxIntensity    = _random.nextRange( _settings._guestMaxIntensityMin, _settings._guestMaxIntensityMax );
            const float32 nauseaTolerance = _random.nextRange( _settings._guestNauseaToleranceMin, _settings._guestNauseaToleranceMax );
            if ( admitGuest( cash, minIntensity, maxIntensity, nauseaTolerance ) == false )
                break; // 꽉 찼거나 입장료를 못 낸다. 이번에는 그만 들인다
        }
    }

    void ThemeParkSimulation::runRides( float32 deltaTime )
    {
        for ( size_t rideIndex = 0; rideIndex < _listRide.size(); ++rideIndex )
        {
            ParkRide& ride = _listRide[rideIndex];

            // 1) 도는 중이면 시간을 흘리고, 다 돌면 내린다.
            if ( ride._listRider.empty() == false )
            {
                ride._cycleTimer.update( deltaTime );
                if ( ride._cycleTimer.isActive() )
                    continue;

                const vector<uint32> listRider = ride._listRider;
                ride._listRider.clear();
                for ( const uint32 guestID : listRider )
                {
                    ParkGuest* pGuest = findGuest( guestID );
                    if ( pGuest == nullptr )
                        continue;
                    float32 fun      = 0.04f + ride._excitement * 0.025f;
                    pGuest->_thought = ride._excitement >= 6.0f ? ParkGuestThought::GreatRide : ParkGuestThought::None;
                    if ( ride._intensity < pGuest->_minIntensity )
                    {
                        fun *= 0.5f;
                        pGuest->_thought = ParkGuestThought::TooTame;
                    }
                    pGuest->_nausea = MathUtil::min( 1.0f, pGuest->_nausea + ride._nausea * _settings._nauseaPerRating );
                    if ( pGuest->_nausea > pGuest->_nauseaTolerance )
                    {
                        fun -= 0.1f;
                        pGuest->_thought = ParkGuestThought::Sick;
                    }
                    pGuest->_happiness = MathUtil::clamp( pGuest->_happiness + fun, 0.0f, 1.0f );
                    pGuest->_energy    = MathUtil::max( 0.0f, pGuest->_energy - 0.04f );
                    ++pGuest->_rideCount;
                    pGuest->_position = ride._entrance;
                    pGuest->_state    = ParkGuestState::Walking;
                    chooseNextRide( *pGuest );
                }
                ride._totalRiders += static_cast<uint32>( listRider.size() );
            }

            // 2) 쉬고 있으면 줄 앞에서 정원만큼 태운다.
            if ( ride._bOpen == SW_FALSE || ride._listQueue.empty() )
                continue;
            const size_t         boardCount = MathUtil::min( ride._listQueue.size(), static_cast<size_t>( ride._capacity ) );
            const vector<uint32> listBoarding( ride._listQueue.begin(), ride._listQueue.begin() + static_cast<ptrdiff_t>( boardCount ) );
            ride._listQueue.erase( ride._listQueue.begin(), ride._listQueue.begin() + static_cast<ptrdiff_t>( boardCount ) );
            for ( const uint32 guestID : listBoarding )
            {
                ParkGuest* pGuest = findGuest( guestID );
                if ( pGuest == nullptr )
                    continue;
                if ( pGuest->_cash < ride._price )
                {
                    pGuest->_targetRideIndex = -1; // 이미 줄에서 뺐다
                    sendHome( *pGuest, ParkGuestThought::OutOfCash );
                    continue;
                }
                pGuest->_cash -= ride._price;
                if ( _pWallet != nullptr )
                    _pWallet->add( _settings._currency, ride._price );
                ride._totalIncome += ride._price;
                pGuest->_state    = ParkGuestState::Riding;
                pGuest->_position = ride._entrance;
                ride._listRider.push_back( guestID );
            }
            if ( ride._listRider.empty() == false )
                ride._cycleTimer.start( ride._cycleTime );
        }
    }

    void ThemeParkSimulation::updateGuests( float32 deltaTime )
    {
        for ( ParkGuest& guest : _listGuest )
        {
            if ( guest._state == ParkGuestState::Left )
                continue;
            guest._energy = MathUtil::max( 0.0f, guest._energy - _settings._energyDrainPerSecond * deltaTime );
            guest._nausea = MathUtil::max( 0.0f, guest._nausea - _settings._nauseaRecoveryPerSecond * deltaTime );

            switch ( guest._state )
            {
                case ParkGuestState::Walking:
                case ParkGuestState::Leaving:
                {
                    guest._walkTimer -= deltaTime;
                    const float32 progress = 1.0f - guest._walkTimer / MathUtil::max( 1.0e-3f, guest._walkDuration );
                    guest._position        = ThemeParkInternal::lerpPosition( guest._walkFrom, guest._walkTo, progress );
                    if ( guest._walkTimer > 0.0f )
                        break;
                    guest._position = guest._walkTo;
                    if ( guest._state == ParkGuestState::Leaving )
                    {
                        guest._state = ParkGuestState::Left;
                        break;
                    }
                    const bool bTargetOpen = 0 <= guest._targetRideIndex && guest._targetRideIndex < static_cast<int32>( _listRide.size() ) &&
                                             _listRide[static_cast<size_t>( guest._targetRideIndex )]._bOpen == SW_TRUE;
                    if ( bTargetOpen )
                    {
                        guest._state     = ParkGuestState::Queuing;
                        guest._queueTime = 0.0f;
                        _listRide[static_cast<size_t>( guest._targetRideIndex )]._listQueue.push_back( guest._id );
                    }
                    else
                    {
                        chooseNextRide( guest );
                    }
                    break;
                }
                case ParkGuestState::Queuing:
                {
                    guest._queueTime += deltaTime;
                    guest._happiness = MathUtil::max( 0.0f, guest._happiness - _settings._queueUnhappinessPerSecond * deltaTime );
                    if ( guest._queueTime > _settings._queuePatience )
                    {
                        leaveQueue( guest );
                        guest._thought   = ParkGuestThought::QueueTooLong;
                        guest._happiness = MathUtil::max( 0.0f, guest._happiness - 0.1f );
                        chooseNextRide( guest );
                        break;
                    }
                    // 줄의 위치는 입구에서 정문 쪽으로 늘어선다(그리기용).
                    const ParkRide& ride      = _listRide[static_cast<size_t>( guest._targetRideIndex )];
                    size_t          lineIndex = 0;
                    while ( lineIndex < ride._listQueue.size() && ride._listQueue[lineIndex] != guest._id )
                    {
                        ++lineIndex;
                    }
                    float3        toGate = _settings._gatePosition - ride._entrance;
                    const float32 length = toGate.getLength();
                    toGate               = length > 1.0e-3f ? toGate * ( 1.0f / length ) : float3{ 0.0f, 0.0f, -1.0f };
                    guest._position      = ride._entrance + toGate * ( _settings._queueSpacing * static_cast<float32>( lineIndex + 1 ) );
                    break;
                }
                case ParkGuestState::Riding:
                case ParkGuestState::Left:
                {
                    break;
                }
            }
        }
    }

    void ThemeParkSimulation::chooseNextRide( ParkGuest& guest )
    {
        // 떠날 때인지 판단한다. 지쳤거나, 기분이 바닥이거나, 멀미가 심하거나, 돈이 바닥이면 떠난다.
        if ( guest._energy < _settings._leaveEnergy )
        {
            sendHome( guest, ParkGuestThought::Tired );
            return;
        }
        if ( guest._happiness < _settings._leaveHappiness )
        {
            sendHome( guest, guest._thought );
            return;
        }
        if ( guest._nausea > 0.95f )
        {
            sendHome( guest, ParkGuestThought::Sick );
            return;
        }

        const int32      previousRide = guest._targetRideIndex;
        int32            bestRide     = -1;
        float32          bestScore    = -1.0e9f;
        ParkGuestThought lastReject   = _listRide.empty() ? ParkGuestThought::NothingToRide : ParkGuestThought::None;
        for ( size_t rideIndex = 0; rideIndex < _listRide.size(); ++rideIndex )
        {
            const ParkRide& ride = _listRide[rideIndex];
            if ( ride._bOpen == SW_FALSE )
                continue;
            if ( ride._intensity > guest._maxIntensity )
            {
                lastReject = ParkGuestThought::TooIntense;
                continue;
            }
            if ( guest._nausea + ride._nausea * _settings._nauseaPerRating > guest._nauseaTolerance )
            {
                lastReject = ParkGuestThought::Sick;
                continue;
            }
            if ( static_cast<float32>( ride._price ) > computeRideValue( ride ) * 2.0f + 0.5f )
            {
                lastReject = ParkGuestThought::TooExpensive;
                continue;
            }
            if ( ride._price > guest._cash )
            {
                lastReject = ParkGuestThought::OutOfCash;
                continue;
            }
            const float32 expectedWait = static_cast<float32>( ride._listQueue.size() ) / static_cast<float32>( ride._capacity ) * ride._cycleTime;
            if ( expectedWait > _settings._queuePatience )
            {
                lastReject = ParkGuestThought::QueueTooLong;
                continue;
            }

            float32 score = ride._excitement + nextRandom() * 1.5f;
            if ( ride._intensity < guest._minIntensity )
                score -= 2.0f; // 시시하지만 탈 수는 있다
            if ( static_cast<int32>( rideIndex ) == previousRide )
                score -= 1.5f; // 같은 것만 타지는 않는다
            if ( score > bestScore )
            {
                bestScore = score;
                bestRide  = static_cast<int32>( rideIndex );
            }
        }

        if ( bestRide < 0 )
        {
            // 탈 것이 없으면 기분이 조금 상하고 둘러본다.
            guest._happiness       = MathUtil::max( 0.0f, guest._happiness - 0.05f );
            guest._thought         = lastReject == ParkGuestThought::None ? ParkGuestThought::NothingToRide : lastReject;
            guest._targetRideIndex = -1;
            if ( lastReject == ParkGuestThought::OutOfCash )
            {
                sendHome( guest, ParkGuestThought::OutOfCash );
                return;
            }
            const float32 angle  = nextRandom() * 2.0f * MathUtil::kPi;
            const float32 radius = nextRandom() * _settings._wanderRadius;
            const float3  anchor = _listRide.empty() ? _settings._gatePosition
                                                     : _listRide[static_cast<size_t>( nextRandom() * static_cast<float32>( _listRide.size() ) ) % _listRide.size()]._entrance;
            startWalking( guest, anchor + float3{ MathUtil::cos( angle ) * radius, 0.0f, MathUtil::sin( angle ) * radius } );
            return;
        }

        guest._targetRideIndex = bestRide;
        startWalking( guest, _listRide[static_cast<size_t>( bestRide )]._entrance );
    }

    void ThemeParkSimulation::startWalking( ParkGuest& guest, const float3& destination )
    {
        guest._walkFrom     = guest._position;
        guest._walkTo       = destination;
        guest._walkDuration = MathUtil::max( 0.5f, float3::getDistance( guest._position, destination ) / MathUtil::max( 0.1f, _settings._walkSpeed ) );
        guest._walkTimer    = guest._walkDuration;
        guest._state        = ParkGuestState::Walking;
    }

    void ThemeParkSimulation::sendHome( ParkGuest& guest, ParkGuestThought thought )
    {
        if ( guest._state == ParkGuestState::Queuing )
            leaveQueue( guest );
        guest._thought         = thought;
        guest._targetRideIndex = -1;
        startWalking( guest, _settings._gatePosition );
        guest._state = ParkGuestState::Leaving;
    }

    void ThemeParkSimulation::leaveQueue( ParkGuest& guest )
    {
        if ( 0 <= guest._targetRideIndex && guest._targetRideIndex < static_cast<int32>( _listRide.size() ) )
        {
            vector<uint32>& listQueue = _listRide[static_cast<size_t>( guest._targetRideIndex )]._listQueue;
            for ( size_t queueIndex = 0; queueIndex < listQueue.size(); ++queueIndex )
            {
                if ( listQueue[queueIndex] == guest._id )
                {
                    listQueue.erase( listQueue.begin() + static_cast<ptrdiff_t>( queueIndex ) );
                    break;
                }
            }
        }
        guest._state = ParkGuestState::Walking;
    }

    void ThemeParkSimulation::updateParkRating()
    {
        uint32 openRideCount = 0;
        for ( const ParkRide& ride : _listRide )
        {
            if ( ride._bOpen == SW_TRUE )
                ++openRideCount;
        }
        const float32 rideBonus  = static_cast<float32>( MathUtil::min( openRideCount, 8u ) ) * 25.0f;
        const uint32  guestCount = getGuestCount();
        float32       rating     = 0.0f;
        if ( guestCount == 0 )
        {
            rating = 400.0f + rideBonus;
        }
        else
        {
            float32 totalNausea = 0.0f;
            for ( const ParkGuest& guest : _listGuest )
            {
                if ( guest._state != ParkGuestState::Left )
                    totalNausea += guest._nausea;
            }
            const float32 averageNausea = totalNausea / static_cast<float32>( guestCount );
            const float32 queueShare    = static_cast<float32>( countGuestsThinking( ParkGuestThought::QueueTooLong ) ) / static_cast<float32>( guestCount );
            rating                      = 200.0f + getAverageHappiness() * 600.0f - averageNausea * 300.0f + rideBonus - queueShare * 100.0f;
        }
        _parkRating = static_cast<int32>( MathUtil::clamp( rating, 0.0f, 999.0f ) );
    }

    ParkGuest* ThemeParkSimulation::findGuest( uint32 guestID )
    {
        const auto guestIter = std::lower_bound( _listGuest.begin(), _listGuest.end(), guestID, []( const ParkGuest& guest, uint32 id )
        { return guest._id < id; } );
        return ( guestIter != _listGuest.end() && guestIter->_id == guestID ) ? &*guestIter : nullptr;
    }
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 상태 쓰기와 읽기(핫 리로드, 세이브)
    // ------------------------------------------------------------------------------
    void ThemeParkSimulation::writeState( Archive& outArchive ) const
    {
        const auto writeIDList = [&outArchive]( const vector<uint32>& listID )
        {
            outArchive << static_cast<uint32>( listID.size() );
            for ( const uint32 id : listID )
            {
                outArchive << id;
            }
        };
        outArchive << _settings._entryFee;
        outArchive << static_cast<uint32>( _listRide.size() );
        for ( const ParkRide& ride : _listRide )
        {
            StateArchiveUtil::writeName( outArchive, ride._id );
            outArchive << string_view( ride._name );
            outArchive << ride._excitement;
            outArchive << ride._intensity;
            outArchive << ride._nausea;
            outArchive << ride._cycleTime;
            outArchive << ride._entrance;
            outArchive << ride._capacity;
            outArchive << ride._price;
            outArchive << ride._runningCostPerMinute;
            outArchive << ride._bOpen;
            writeIDList( ride._listQueue );
            writeIDList( ride._listRider );
            outArchive << ride._cycleTimer._remaining;
            outArchive << ride._totalRiders;
            outArchive << ride._totalIncome;
        }
        outArchive << static_cast<uint32>( _listGuest.size() );
        for ( const ParkGuest& guest : _listGuest )
        {
            outArchive << guest._position;
            outArchive << guest._walkFrom;
            outArchive << guest._walkTo;
            outArchive << guest._id;
            outArchive << guest._cash;
            outArchive << guest._targetRideIndex;
            outArchive << guest._rideCount;
            outArchive << guest._happiness;
            outArchive << guest._nausea;
            outArchive << guest._energy;
            outArchive << guest._minIntensity;
            outArchive << guest._maxIntensity;
            outArchive << guest._nauseaTolerance;
            outArchive << guest._walkTimer;
            outArchive << guest._walkDuration;
            outArchive << guest._queueTime;
            outArchive << static_cast<uint8>( guest._state );
            outArchive << static_cast<uint8>( guest._thought );
        }
        StateArchiveUtil::writeStepTimer( outArchive, _stepTimer );
        StateArchiveUtil::writeRandom( outArchive, _random );
        outArchive << _arrival._fraction;
        outArchive << _runningCost._fraction;
        outArchive << _elapsedTime;
        outArchive << _parkRating;
        outArchive << _nextGuestID;
        outArchive << _totalVisitorCount;
    }

    bool ThemeParkSimulation::readState( Archive& archive )
    {
        int32 entryFee = 0;
        archive >> entryFee;
        uint32 rideCount = 0;
        if ( archive.isError() || StateArchiveUtil::readCount( archive, ThemeParkInternal::kMinRideStateBytes, rideCount ) == false )
            return false;
        vector<ParkRide> listRide( rideCount );
        for ( ParkRide& ride : listRide )
        {
            if ( ThemeParkInternal::readRide( archive, ride ) == false )
                return false;
        }
        uint32 guestCount = 0;
        if ( StateArchiveUtil::readCount( archive, ThemeParkInternal::kMinGuestStateBytes, guestCount ) == false )
            return false;
        vector<ParkGuest> listGuest( guestCount );
        for ( ParkGuest& guest : listGuest )
        {
            if ( ThemeParkInternal::readGuest( archive, rideCount, guest ) == false )
                return false;
        }
        FixedStepTimer stepTimer = _stepTimer;
        GameRandom     random;
        if ( StateArchiveUtil::readStepTimer( archive, stepTimer ) == false || StateArchiveUtil::readRandom( archive, random ) == false )
            return false;
        float32 arrivalAccumulator = 0.0f;
        float32 costAccumulator    = 0.0f;
        float32 elapsedTime        = 0.0f;
        int32   parkRating         = 0;
        uint32  nextGuestID        = 0;
        uint32  totalVisitorCount  = 0;
        archive >> arrivalAccumulator;
        archive >> costAccumulator;
        archive >> elapsedTime;
        archive >> parkRating;
        archive >> nextGuestID;
        archive >> totalVisitorCount;
        if ( archive.isError() )
            return false;

        _settings._entryFee    = entryFee;
        _listRide              = std::move( listRide );
        _listGuest             = std::move( listGuest );
        _stepTimer             = stepTimer;
        _random                = random;
        _arrival._fraction     = arrivalAccumulator;
        _runningCost._fraction = costAccumulator;
        _elapsedTime           = elapsedTime;
        _parkRating            = parkRating;
        _nextGuestID           = nextGuestID;
        _totalVisitorCount     = totalVisitorCount;
        return true;
    }
} // namespace sw
