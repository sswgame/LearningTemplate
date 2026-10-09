#include "pch.h"

#include "GameFramework/Base/Gameplay/Gimmick/ElementGrid.h"

#include "Core/Common/HashUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"

namespace sw
{
    namespace
    {
        struct ElementGridInternal
        {

            static constexpr int32 kMaxStatusValue = 255;
            static constexpr int32 kNeverExpires   = 1 << 30;
        };
    } // namespace
} // namespace sw

namespace sw
{
    ElementGrid::ElementGrid()
        : _listCell{}
        , _eventBuffer{}
        , _listPending{}
        , _floodSearch{}
        , _pTable{ nullptr }
        , _timer{}
        , _wind{}
        , _width{ 0 }
        , _height{ 0 }
        , _stepCount{ 0 }
    {
    }

    void ElementGrid::initialize( int32 width, int32 height, const ElementRuleTable* pTable )
    {
        _pTable = pTable;
        _width  = MathUtil::max( 1, width );
        _height = MathUtil::max( 1, height );
        _timer  = FixedStepTimer( MathUtil::max( 0.001f, pTable != nullptr ? pTable->getStepTime() : 0.25f ), 1.0f );
        _listCell.clear();
        _listCell.resize( static_cast<size_t>( _width ) * static_cast<size_t>( _height ) );
        _eventBuffer.clear();
        _wind      = int2{};
        _stepCount = 0;
    }

    void ElementGrid::setMaterial( const int2& cell, int32 material )
    {
        if ( isInside( cell ) == false )
            return;
        Cell& target     = getCell( cell );
        target           = Cell{};
        target._material = static_cast<uint8>( MathUtil::clamp( material, 0, ElementRuleTable::kMaxMaterialCount - 1 ) );
    }

    int32 ElementGrid::getMaterial( const int2& cell ) const { return isInside( cell ) ? _listCell[static_cast<size_t>( toIndex( cell ) )]._material : 0; }

    void ElementGrid::setWind( const int2& wind ) { _wind = int2{ MathUtil::clamp( wind._x, -1, 1 ), MathUtil::clamp( wind._y, -1, 1 ) }; }

    bool ElementGrid::hasStatus( const int2& cell, int32 status ) const
    {
        if ( isInside( cell ) == false || status < 0 || status >= ElementRuleTable::kMaxStatusCount )
            return false;
        return ( _listCell[static_cast<size_t>( toIndex( cell ) )]._statusBits & ( 1u << static_cast<uint32>( status ) ) ) != 0;
    }

    int32 ElementGrid::getStatusValue( const int2& cell, int32 status ) const
    {
        return hasStatus( cell, status ) ? _listCell[static_cast<size_t>( toIndex( cell ) )]._arrStatusValue[status] : 0;
    }

    bool ElementGrid::hasFlag( const int2& cell, int32 flag ) const { return _pTable != nullptr && isInside( cell ) && _pTable->hasFlag( getMaterial( cell ), flag ); }

    int32 ElementGrid::countStatus( int32 status ) const
    {
        int32 count = 0;
        for ( int32 y = 0; y < _height; ++y )
        {
            for ( int32 x = 0; x < _width; ++x )
            {
                count += hasStatus( int2{ x, y }, status ) ? 1 : 0;
            }
        }
        return count;
    }

    void ElementGrid::addStatus( Cell& cell, int32 status ) const
    {
        if ( status < 0 || status >= static_cast<int32>( _pTable->getStatuses().size() ) )
            return;
        const ElementStatusDef& def = _pTable->getStatuses()[static_cast<size_t>( status )];
        cell._statusBits |= static_cast<uint8>( 1u << static_cast<uint32>( status ) );
        cell._arrStatusValue[status] = def._kind == ElementStatusKind::Countdown
                                         ? static_cast<uint8>( MathUtil::clamp( def._steps, 1, ElementGridInternal::kMaxStatusValue ) )
                                         : static_cast<uint8>( 0 );
    }

    void ElementGrid::removeStatus( Cell& cell, int32 status ) const
    {
        if ( status < 0 || status >= ElementRuleTable::kMaxStatusCount )
            return;
        cell._statusBits &= static_cast<uint8>( ~( 1u << static_cast<uint32>( status ) ) );
        cell._arrStatusValue[status] = 0;
    }

    bool ElementGrid::matchesRule( const Cell& cell, const ElementStimulusRule& rule ) const
    {
        const bool bMaterial = rule._material < 0 || cell._material == rule._material;
        const bool bFlag     = rule._flag < 0 || _pTable->hasFlag( cell._material, rule._flag );
        const bool bStatus   = rule._status < 0 || ( cell._statusBits & ( 1u << static_cast<uint32>( rule._status ) ) ) != 0;
        const bool bWithout  = rule._without < 0 || ( cell._statusBits & ( 1u << static_cast<uint32>( rule._without ) ) ) == 0;
        return bMaterial && bFlag && bStatus && bWithout;
    }

    void ElementGrid::pushEvent( const hashed_string& name, const int2& cell )
    {
        if ( name.empty() )
            return;
        ElementEvent event;
        event._name = name;
        event._cell = cell;
        _eventBuffer.push( event );
    }

    int32 ElementGrid::applyStimulus( const int2& cell, int32 stimulus )
    {
        const bool bValid = _pTable != nullptr && isInside( cell ) && 0 <= stimulus && stimulus < static_cast<int32>( _pTable->getStimuli().size() );
        if ( bValid == false )
            return 0;
        Cell& target = getCell( cell );
        for ( const ElementStimulusRule& rule : _pTable->getStimuli()[static_cast<size_t>( stimulus )]._listRule )
        {
            if ( matchesRule( target, rule ) == false )
                continue;
            if ( rule._floodStatus < 0 )
            {
                if ( rule._setMaterial >= 0 )
                    target._material = static_cast<uint8>( rule._setMaterial );
                removeStatus( target, rule._removeStatus );
                addStatus( target, rule._addStatus );
                pushEvent( rule._event, cell );
                return 1;
            }
            // 너비 우선으로 이어진 칸(네 이웃, 이웃 순서 고정 — 알림 순서도 늘 같다)에 상태를 붙인다.
            const GridTopology topology{ _width, _height };
            _floodSearch.begin( topology.getCellCount() );
            _floodSearch.visit( toIndex( cell ), -1 );
            while ( _floodSearch.hasNext() )
            {
                const int32 index   = _floodSearch.popNext();
                const int2  current = topology.toCell( index );
                addStatus( getCell( current ), rule._floodStatus );
                pushEvent( rule._event, current );
                for ( int32 neighbor = 0; neighbor < GridTopology::kOrthogonalCount; ++neighbor )
                {
                    const int2 next = GridTopology::getNeighbor( current, neighbor );
                    if ( isInside( next ) == false )
                        continue;
                    const int32 nextIndex = toIndex( next );
                    if ( _floodSearch.isVisited( nextIndex ) || _pTable->hasFlag( _listCell[static_cast<size_t>( nextIndex )]._material, rule._floodThrough ) == false )
                        continue;
                    _floodSearch.visit( nextIndex, index );
                }
            }
            return static_cast<int32>( _floodSearch.getVisitOrder().size() );
        }
        return 0;
    }

    void ElementGrid::collectSpread( const ElementStepRule& rule, int32 ruleIndex, const int2& position )
    {
        const bool bWind = rule._pattern == ElementSpreadPattern::Wind && ( _wind._x != 0 || _wind._y != 0 );
        if ( bWind )
        {
            _listPending.push_back( PendingChange{
                int2{ position._x + _wind._x, position._y + _wind._y },
                ruleIndex
            } );
            if ( rule._bCrosswind == SW_TRUE )
            {
                _listPending.push_back( PendingChange{
                    int2{ position._x - _wind._y, position._y + _wind._x },
                    ruleIndex
                } );
                _listPending.push_back( PendingChange{
                    int2{ position._x + _wind._y, position._y - _wind._x },
                    ruleIndex
                } );
            }
            return;
        }
        for ( int32 neighbor = 0; neighbor < GridTopology::kOrthogonalCount; ++neighbor )
        {
            _listPending.push_back( PendingChange{
                GridTopology::getNeighbor( position, neighbor ),
                ruleIndex } );
        }
    }

    void ElementGrid::step()
    {
        if ( _pTable == nullptr )
            return;
        const vector<ElementStatusDef>& listStatus = _pTable->getStatuses();
        // 1) 상태의 시간 — 칸마다 따로라 순서와 상관없다.
        for ( Cell& cell : _listCell )
        {
            for ( size_t status = 0; status < listStatus.size(); ++status )
            {
                if ( ( cell._statusBits & ( 1u << status ) ) == 0 )
                    continue;
                uint8& value = cell._arrStatusValue[status];
                if ( listStatus[status]._kind == ElementStatusKind::Age )
                {
                    if ( value < ElementGridInternal::kMaxStatusValue )
                        ++value;
                }
                else
                {
                    if ( value > 0 )
                        --value;
                    if ( value == 0 )
                        cell._statusBits &= static_cast<uint8>( ~( 1u << status ) );
                }
            }
        }

        // 2) 걸음 규칙을 지금 상태만 보고 모은다(규칙 순서 → 행 우선).
        const vector<ElementStepRule>& listRule = _pTable->getStepRules();
        _listPending.clear();
        for ( size_t ruleIndex = 0; ruleIndex < listRule.size(); ++ruleIndex )
        {
            const ElementStepRule& rule = listRule[ruleIndex];
            for ( int32 y = 0; y < _height; ++y )
            {
                for ( int32 x = 0; x < _width; ++x )
                {
                    const int2 position{ x, y };
                    if ( hasStatus( position, rule._status ) == false )
                        continue;
                    const int32 value = getStatusValue( position, rule._status );
                    switch ( rule._kind )
                    {
                        case ElementStepRuleKind::Expire:
                        {
                            const ElementMaterialDef& material = _pTable->getMaterials()[static_cast<size_t>( getMaterial( position ) )];
                            if ( value >= material.getParam( rule._stepsParam, ElementGridInternal::kNeverExpires ) )
                                _listPending.push_back( PendingChange{ position, static_cast<int32>( ruleIndex ) } );
                            break;
                        }
                        case ElementStepRuleKind::Convert:
                        {
                            for ( int32 neighbor = 0; neighbor < GridTopology::kOrthogonalCount; ++neighbor )
                            {
                                const int2 next = GridTopology::getNeighbor( int2{ x, y }, neighbor );
                                if ( isInside( next ) && getMaterial( next ) == rule._material )
                                    _listPending.push_back( PendingChange{ next, static_cast<int32>( ruleIndex ) } );
                            }
                            break;
                        }
                        case ElementStepRuleKind::Spread:
                        {
                            if ( value >= rule._after )
                                collectSpread( rule, static_cast<int32>( ruleIndex ), position );
                            break;
                        }
                    }
                }
            }
        }

        // 3) 모은 순서대로 적용한다. 앞의 것이 바꾼 칸은 뒤의 것이 다시 확인한다(두 불이 같은 얼음을 녹였다).
        for ( const PendingChange& change : _listPending )
        {
            if ( isInside( change._cell ) == false )
                continue;
            const ElementStepRule& rule = listRule[static_cast<size_t>( change._rule )];
            Cell&                  cell = getCell( change._cell );
            switch ( rule._kind )
            {
                case ElementStepRuleKind::Expire:
                {
                    cell           = Cell{};
                    cell._material = static_cast<uint8>( MathUtil::max( 0, rule._setMaterial ) );
                    pushEvent( rule._event, change._cell );
                    break;
                }
                case ElementStepRuleKind::Convert:
                {
                    if ( cell._material != rule._material )
                        break;
                    cell._material = static_cast<uint8>( MathUtil::max( 0, rule._setMaterial ) );
                    pushEvent( rule._event, change._cell );
                    break;
                }
                case ElementStepRuleKind::Spread:
                {
                    const bool bAlready = ( cell._statusBits & ( 1u << static_cast<uint32>( rule._status ) ) ) != 0;
                    if ( bAlready || _pTable->hasFlag( cell._material, rule._toFlag ) == false )
                        break;
                    addStatus( cell, rule._status );
                    pushEvent( rule._event, change._cell );
                    break;
                }
            }
        }
        ++_stepCount;
    }

    int32 ElementGrid::update( float32 deltaTime )
    {
        const int32 stepCount = _timer.consume( deltaTime );
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            step();
        }
        return stepCount;
    }

    void ElementGrid::drainEvents( vector<ElementEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }

    uint32 ElementGrid::computeStateHash() const
    {
        uint32 hash = HashUtil::kFnvOffset32;
        for ( const Cell& cell : _listCell )
        {
            hash = ( hash ^ cell._material ) * HashUtil::kFnvPrime32;
            hash = ( hash ^ cell._statusBits ) * HashUtil::kFnvPrime32;
            for ( const uint8 value : cell._arrStatusValue )
            {
                hash = ( hash ^ value ) * HashUtil::kFnvPrime32;
            }
        }
        return hash;
    }

    void ElementGrid::writeState( Archive& outArchive ) const
    {
        outArchive << _width;
        outArchive << _height;
        for ( const Cell& cell : _listCell )
        {
            outArchive << cell._material;
            outArchive << cell._statusBits;
            for ( const uint8 value : cell._arrStatusValue )
            {
                outArchive << value; // 상태 값(남은 시간 · 세기) — 비트만 실으면 되살린 불이 처음부터 탄다
            }
        }
        StateArchiveUtil::writeInt2( outArchive, _wind );
        StateArchiveUtil::writeStepTimer( outArchive, _timer );
        outArchive << _stepCount;
    }

    bool ElementGrid::readState( Archive& archive )
    {
        int32 width  = 0;
        int32 height = 0;
        archive >> width;
        archive >> height;
        const uint64 cellCount = static_cast<uint64>( MathUtil::max( 0, width ) ) * static_cast<uint64>( MathUtil::max( 0, height ) );
        // 크기는 initialize 의 것이어야 한다 — 칸마다 재질 · 상태 비트(2) + 상태 값
        const uint64 cellBytes = 2u + ElementRuleTable::kMaxStatusCount;
        if ( archive.isError() || width != _width || height != _height || archive.hasBytesAvailable( cellCount * cellBytes ) == false )
            return false;
        vector<Cell> listCell( static_cast<size_t>( cellCount ) );
        for ( Cell& cell : listCell )
        {
            archive >> cell._material;
            archive >> cell._statusBits;
            for ( uint8& value : cell._arrStatusValue )
            {
                archive >> value;
            }
        }
        int2           wind      = {};
        FixedStepTimer timer     = _timer;
        uint32         stepCount = 0;
        StateArchiveUtil::readInt2( archive, wind );
        const bool bTimerRead = StateArchiveUtil::readStepTimer( archive, timer );
        archive >> stepCount;
        if ( bTimerRead == false || archive.isError() )
            return false;
        _width     = width;
        _height    = height;
        _listCell  = std::move( listCell );
        _wind      = wind;
        _timer     = timer;
        _stepCount = stepCount;
        _listPending.clear();
        _eventBuffer.clear();
        return true;
    }
} // namespace sw
