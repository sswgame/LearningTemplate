#include "pch.h"

#include "GameFramework/Kits/Action/ActionAdventure/AdventureElementGrid.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        struct AdventureElementGridInternal
        {
            static constexpr int32 kNeighborCount             = 4;
            static constexpr int32 kNeighborX[kNeighborCount] = { 1, -1, 0, 0 };
            static constexpr int32 kNeighborY[kNeighborCount] = { 0, 0, 1, -1 };

            static uint8 toStepCount( int32 value ) { return static_cast<uint8>( MathUtil::clamp( value, 0, 255 ) ); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    AdventureElementGrid::AdventureElementGrid()
        : _settings{}
        , _timer{}
        , _listCell{}
        , _listEvent{}
        , _wind{}
        , _width{ 0 }
        , _height{ 0 }
        , _stepCount{ 0 }
    {
    }

    void AdventureElementGrid::initialize( int32 width, int32 height, const AdventureElementSettings& settings )
    {
        _settings                   = settings;
        _settings._grassBurnSteps   = MathUtil::clamp( _settings._grassBurnSteps, 1, 255 );
        _settings._woodBurnSteps    = MathUtil::clamp( _settings._woodBurnSteps, 1, 255 );
        _settings._spreadDelaySteps = MathUtil::clamp( _settings._spreadDelaySteps, 1, 255 );
        _settings._chargeSteps      = MathUtil::clamp( _settings._chargeSteps, 1, 255 );
        _timer                      = FixedStepTimer( MathUtil::max( 0.001f, _settings._stepTime ), 1.0f );
        _width                      = MathUtil::max( 1, width );
        _height                     = MathUtil::max( 1, height );
        _listCell.clear();
        _listCell.resize( static_cast<size_t>( _width ) * static_cast<size_t>( _height ) );
        _listEvent.clear();
        _wind      = int2{};
        _stepCount = 0;
    }

    void AdventureElementGrid::setMaterial( const int2& cell, AdventureMaterial material )
    {
        if ( isInside( cell ) == false )
            return;
        Cell& target     = _listCell[static_cast<size_t>( toIndex( cell ) )];
        target           = Cell{};
        target._material = material;
    }

    AdventureMaterial AdventureElementGrid::getMaterial( const int2& cell ) const
    {
        return isInside( cell ) ? _listCell[static_cast<size_t>( toIndex( cell ) )]._material : AdventureMaterial::Empty;
    }

    void AdventureElementGrid::setWind( const int2& wind ) { _wind = int2{ MathUtil::clamp( wind._x, -1, 1 ), MathUtil::clamp( wind._y, -1, 1 ) }; }

    void AdventureElementGrid::ignite( Cell& cell, const int2& position )
    {
        cell._bBurning = SW_TRUE;
        cell._burnStep = 0;
        pushEvent( AdventureElementEventType::Ignited, position );
    }

    bool AdventureElementGrid::applyFire( const int2& cell )
    {
        if ( isInside( cell ) == false )
            return false;
        Cell& target = _listCell[static_cast<size_t>( toIndex( cell ) )];
        if ( target._material == AdventureMaterial::Ice )
        {
            target._material = AdventureMaterial::Water;
            pushEvent( AdventureElementEventType::Melted, cell );
            return true;
        }
        if ( isFlammable( target._material ) == false || target._bBurning == SW_TRUE )
            return false;
        ignite( target, cell );
        return true;
    }

    bool AdventureElementGrid::applyIce( const int2& cell )
    {
        if ( isInside( cell ) == false )
            return false;
        Cell& target = _listCell[static_cast<size_t>( toIndex( cell ) )];
        if ( target._bBurning == SW_TRUE )
        {
            target._bBurning = SW_FALSE;
            target._burnStep = 0;
            pushEvent( AdventureElementEventType::Extinguished, cell );
            return true;
        }
        if ( target._material != AdventureMaterial::Water )
            return false;
        target._material        = AdventureMaterial::Ice;
        target._chargeRemaining = 0;
        pushEvent( AdventureElementEventType::Frozen, cell );
        return true;
    }

    int32 AdventureElementGrid::applyElectric( const int2& cell )
    {
        if ( isInside( cell ) == false )
            return 0;
        const uint8 chargeSteps = AdventureElementGridInternal::toStepCount( _settings._chargeSteps );
        Cell&       origin      = _listCell[static_cast<size_t>( toIndex( cell ) )];
        if ( isConductive( origin._material ) == false )
        {
            // 부도체에 떨어진 번개는 그 칸만 — 그 위의 생물은 게임이 맞힌다.
            origin._chargeRemaining = chargeSteps;
            pushEvent( AdventureElementEventType::Electrified, cell );
            return 1;
        }
        // 너비 우선으로 이어진 도체를 모두 찾는다(이웃 순서가 정해져 있어 알림 순서도 늘 같다).
        vector<uint8> listVisited;
        listVisited.resize( _listCell.size(), SW_FALSE );
        vector<int2> listQueue;
        listQueue.push_back( cell );
        listVisited[static_cast<size_t>( toIndex( cell ) )] = SW_TRUE;
        for ( size_t head = 0; head < listQueue.size(); ++head )
        {
            const int2 current      = listQueue[head];
            Cell&      target       = _listCell[static_cast<size_t>( toIndex( current ) )];
            target._chargeRemaining = chargeSteps;
            pushEvent( AdventureElementEventType::Electrified, current );
            for ( int32 neighbor = 0; neighbor < AdventureElementGridInternal::kNeighborCount; ++neighbor )
            {
                const int2 next{ current._x + AdventureElementGridInternal::kNeighborX[neighbor], current._y + AdventureElementGridInternal::kNeighborY[neighbor] };
                if ( isInside( next ) == false )
                    continue;
                const size_t nextIndex = static_cast<size_t>( toIndex( next ) );
                if ( listVisited[nextIndex] == SW_TRUE || isConductive( _listCell[nextIndex]._material ) == false )
                    continue;
                listVisited[nextIndex] = SW_TRUE;
                listQueue.push_back( next );
            }
        }
        return static_cast<int32>( listQueue.size() );
    }

    void AdventureElementGrid::step()
    {
        vector<int2> listIgnite;
        vector<int2> listMelt;
        vector<int2> listBurnOut;
        const bool   bWind = _wind._x != 0 || _wind._y != 0;
        for ( int32 y = 0; y < _height; ++y )
        {
            for ( int32 x = 0; x < _width; ++x )
            {
                const int2 position{ x, y };
                Cell&      cell = _listCell[static_cast<size_t>( toIndex( position ) )];
                if ( cell._chargeRemaining > 0 )
                    --cell._chargeRemaining;
                if ( cell._bBurning == SW_FALSE )
                    continue;
                if ( cell._burnStep < 255 )
                    ++cell._burnStep;
                // 옮겨 붙을 곳 — 바람이 있으면 바람 쪽(옆바람 설정이면 바람의 양옆도), 없으면 네 이웃.
                if ( cell._burnStep >= _settings._spreadDelaySteps )
                {
                    if ( bWind )
                    {
                        listIgnite.push_back( int2{ x + _wind._x, y + _wind._y } );
                        if ( _settings._bCrosswindSpread == SW_TRUE )
                        {
                            listIgnite.push_back( int2{ x - _wind._y, y + _wind._x } );
                            listIgnite.push_back( int2{ x + _wind._y, y - _wind._x } );
                        }
                    }
                    else
                    {
                        for ( int32 neighbor = 0; neighbor < AdventureElementGridInternal::kNeighborCount; ++neighbor )
                            listIgnite.push_back( int2{ x + AdventureElementGridInternal::kNeighborX[neighbor], y + AdventureElementGridInternal::kNeighborY[neighbor] } );
                    }
                }
                // 열은 바람과 상관없이 네 이웃의 얼음을 녹인다.
                for ( int32 neighbor = 0; neighbor < AdventureElementGridInternal::kNeighborCount; ++neighbor )
                {
                    const int2 next{ x + AdventureElementGridInternal::kNeighborX[neighbor], y + AdventureElementGridInternal::kNeighborY[neighbor] };
                    if ( getMaterial( next ) == AdventureMaterial::Ice )
                        listMelt.push_back( next );
                }
                const int32 burnLimit = cell._material == AdventureMaterial::Grass ? _settings._grassBurnSteps : _settings._woodBurnSteps;
                if ( cell._burnStep >= burnLimit )
                    listBurnOut.push_back( position );
            }
        }
        for ( const int2& position : listBurnOut )
        {
            Cell& cell = _listCell[static_cast<size_t>( toIndex( position ) )];
            cell       = Cell{};
            pushEvent( AdventureElementEventType::BurnedOut, position );
        }
        for ( const int2& position : listMelt )
        {
            Cell& cell = _listCell[static_cast<size_t>( toIndex( position ) )];
            if ( cell._material != AdventureMaterial::Ice )
                continue; // 두 불이 같은 얼음을 녹였다
            cell._material = AdventureMaterial::Water;
            pushEvent( AdventureElementEventType::Melted, position );
        }
        for ( const int2& position : listIgnite )
        {
            if ( isInside( position ) == false )
                continue;
            Cell& cell = _listCell[static_cast<size_t>( toIndex( position ) )];
            if ( isFlammable( cell._material ) && cell._bBurning == SW_FALSE )
                ignite( cell, position );
        }
        ++_stepCount;
    }

    int32 AdventureElementGrid::update( float32 deltaTime )
    {
        const int32 stepCount = _timer.consume( deltaTime );
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
            step();
        return stepCount;
    }

    void AdventureElementGrid::drainEvents( vector<AdventureElementEvent>& outListEvent )
    {
        outListEvent.insert( outListEvent.end(), _listEvent.begin(), _listEvent.end() );
        _listEvent.clear();
    }

    bool AdventureElementGrid::isBurning( const int2& cell ) const
    {
        return isInside( cell ) && _listCell[static_cast<size_t>( toIndex( cell ) )]._bBurning == SW_TRUE;
    }

    bool AdventureElementGrid::isCharged( const int2& cell ) const
    {
        return isInside( cell ) && _listCell[static_cast<size_t>( toIndex( cell ) )]._chargeRemaining > 0;
    }

    bool AdventureElementGrid::hasUpdraft( const int2& cell ) const
    {
        if ( isInside( cell ) == false )
            return false;
        const Cell& target = _listCell[static_cast<size_t>( toIndex( cell ) )];
        return target._bBurning == SW_TRUE && target._material == AdventureMaterial::Grass;
    }

    void AdventureElementGrid::collectUpdraft( vector<int2>& outListCell ) const
    {
        outListCell.clear();
        for ( int32 y = 0; y < _height; ++y )
        {
            for ( int32 x = 0; x < _width; ++x )
            {
                if ( hasUpdraft( int2{ x, y } ) )
                    outListCell.push_back( int2{ x, y } );
            }
        }
    }

    int32 AdventureElementGrid::countBurning() const
    {
        int32 count = 0;
        for ( const Cell& cell : _listCell )
        {
            if ( cell._bBurning == SW_TRUE )
                ++count;
        }
        return count;
    }

    uint32 AdventureElementGrid::computeStateHash() const
    {
        uint32 hash = 2166136261u;
        for ( const Cell& cell : _listCell )
        {
            const uint32 packed = static_cast<uint32>( cell._material ) | ( static_cast<uint32>( cell._bBurning ) << 8 ) | ( static_cast<uint32>( cell._burnStep ) << 16 ) |
                                  ( static_cast<uint32>( cell._chargeRemaining ) << 24 );
            hash = ( hash ^ packed ) * 16777619u;
        }
        return hash;
    }

    void AdventureElementGrid::pushEvent( AdventureElementEventType type, const int2& cell )
    {
        AdventureElementEvent event;
        event._type = type;
        event._cell = cell;
        _listEvent.push_back( event );
    }
} // namespace sw
