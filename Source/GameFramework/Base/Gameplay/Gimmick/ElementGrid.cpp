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
        };
    } // namespace
} // namespace sw

namespace sw
{
    void ElementGrid::Cell::write( Archive& outArchive ) const
    {
        static_assert( kStateBytes == sizeof( Cell ), "Cell has a field that kStateBytes does not count (or padding) - update kStateBytes, write and read together" );
        outArchive << _material;
        outArchive << _statusBits;
        for ( const uint8 value : _arrStatusValue )
        {
            outArchive << value;
        }
    }

    void ElementGrid::Cell::read( Archive& archive )
    {
        archive >> _material;
        archive >> _statusBits;
        for ( uint8& value : _arrStatusValue )
        {
            archive >> value;
        }
    }

    ElementGrid::ElementGrid()
        : _listCell{}
        , _eventBuffer{}
        , _listPending{}
        , _floodSearch{}
        , _pTable{ nullptr }
        , _timer{}
        , _wind{}
        , _topology{}
        , _stepCount{ 0 }
    {
    }

    bool ElementGrid::initialize( int32 width, int32 height, const ElementRuleTable* pTable )
    {
        const bool bValid = 1 <= width && 1 <= height && pTable != nullptr && 0.0f < pTable->getStepTime();
        if ( bValid == false )
        {
            SW_LOG_ERROR( "ElementGrid: refused - size %#x%# (both need 1 or more), table %# (needs a table with a step time above 0)", width, height,
                          pTable != nullptr ? "given" : "missing" );
            return false;
        }
        _pTable   = pTable;
        _topology = GridTopology( width, height );
        _timer    = FixedStepTimer( pTable->getStepTime(), 1.0f );
        _listCell.clear();
        _listCell.resize( static_cast<size_t>( _topology.getCellCount() ) );
        _eventBuffer.clear();
        _wind      = int2{};
        _stepCount = 0;
        return true;
    }

    void ElementGrid::setMaterial( const int2& cell, int32 material )
    {
        if ( isInside( cell ) == false )
            return;
        Cell& target     = getCell( cell );
        target           = Cell{};
        target._material = static_cast<uint8>( MathUtil::clamp( material, 0, ElementRuleTable::kMaxMaterialCount - 1 ) );
    }

    int32 ElementGrid::getMaterial( const int2& cell ) const
    {
        return isInside( cell ) ? _listCell[static_cast<size_t>( toIndex( cell ) )]._material : 0;
    }

    void ElementGrid::setWind( const int2& wind )
    {
        _wind = int2{ MathUtil::clamp( wind._x, -1, 1 ), MathUtil::clamp( wind._y, -1, 1 ) };
    }

    bool ElementGrid::hasStatus( const int2& cell, int32 status ) const
    {
        const bool bInvalidStatus = isInside( cell ) == false || status < 0 || status >= ElementRuleTable::kMaxStatusCount;
        if ( bInvalidStatus )
            return false;
        return ( _listCell[static_cast<size_t>( toIndex( cell ) )]._statusBits & ( 1u << static_cast<uint32>( status ) ) ) != 0;
    }

    int32 ElementGrid::getStatusValue( const int2& cell, int32 status ) const
    {
        return hasStatus( cell, status ) ? _listCell[static_cast<size_t>( toIndex( cell ) )]._arrStatusValue[status] : 0;
    }

    bool ElementGrid::hasFlag( const int2& cell, int32 flag ) const
    {
        return _pTable != nullptr && isInside( cell ) && _pTable->hasFlag( getMaterial( cell ), flag );
    }

    int32 ElementGrid::countStatus( int32 status ) const
    {
        int32 count = 0;
        for ( int32 y = 0; y < _topology._height; ++y )
        {
            for ( int32 x = 0; x < _topology._width; ++x )
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
            // 너비 우선으로 이어진 셀(네 이웃, 이웃 순서 고정)에 상태를 붙인다. 이웃 순서가 고정이므로 알림 순서도 항상 같다.
            _floodSearch.begin( _topology.getCellCount() );
            _floodSearch.visit( toIndex( cell ), -1 );
            while ( _floodSearch.hasNext() )
            {
                const int32 index   = _floodSearch.popNext();
                const int2  current = _topology.toCell( index );
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

    void ElementGrid::advanceStatusTimers()
    {
        const vector<ElementStatusDef>& listStatus = _pTable->getStatuses();
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
    }

    void ElementGrid::collectStepChanges()
    {
        const vector<ElementStepRule>& listRule = _pTable->getStepRules();
        _listPending.clear();
        for ( size_t ruleIndex = 0; ruleIndex < listRule.size(); ++ruleIndex )
        {
            const ElementStepRule& rule = listRule[ruleIndex];
            for ( int32 y = 0; y < _topology._height; ++y )
            {
                for ( int32 x = 0; x < _topology._width; ++x )
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
                            // 재질이 수명 수치를 적지 않았으면 이 상태는 그 재질에서 끝나지 않는다.
                            const std::optional<int32> steps = material.getParam( rule._stepsParam );
                            if ( steps.has_value() && value >= *steps )
                                _listPending.push_back( PendingChange{ position, static_cast<int32>( ruleIndex ) } );
                            break;
                        }
                        case ElementStepRuleKind::Convert:
                        {
                            for ( int32 neighbor = 0; neighbor < GridTopology::kOrthogonalCount; ++neighbor )
                            {
                                const int2 next = GridTopology::getNeighbor( position, neighbor );
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
    }

    void ElementGrid::applyStepChanges()
    {
        const vector<ElementStepRule>& listRule = _pTable->getStepRules();
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
    }

    void ElementGrid::step()
    {
        if ( _pTable == nullptr )
            return;
        advanceStatusTimers();
        collectStepChanges();
        applyStepChanges();
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
            hash = HashUtil::fnvStep32( hash, cell._material );
            hash = HashUtil::fnvStep32( hash, cell._statusBits );
            for ( const uint8 value : cell._arrStatusValue )
            {
                hash = HashUtil::fnvStep32( hash, value );
            }
        }
        return hash;
    }

    void ElementGrid::writeState( Archive& outArchive ) const
    {
        outArchive << _topology._width;
        outArchive << _topology._height;
        for ( const Cell& cell : _listCell )
        {
            cell.write( outArchive );
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
        // 크기는 initialize 의 것이어야 한다. 셀마다 `Cell::write` 가 쓴 바이트(`Cell::kStateBytes`)가 있어야 한다
        const bool bInvalidGridArchive =
            archive.isError() || width != _topology._width || height != _topology._height || archive.hasBytesAvailable( cellCount * Cell::kStateBytes ) == false;
        if ( bInvalidGridArchive )
            return false;
        vector<Cell> listCell( static_cast<size_t>( cellCount ) );
        for ( Cell& cell : listCell )
        {
            cell.read( archive );
        }
        int2           wind      = {};
        FixedStepTimer timer     = _timer;
        uint32         stepCount = 0;
        StateArchiveUtil::readInt2( archive, wind );
        const bool bTimerRead = StateArchiveUtil::readStepTimer( archive, timer );
        archive >> stepCount;
        if ( bTimerRead == false || archive.isError() )
            return false;
        _topology  = GridTopology( width, height );
        _listCell  = std::move( listCell );
        _wind      = wind;
        _timer     = timer;
        _stepCount = stepCount;
        _listPending.clear();
        _eventBuffer.clear();
        return true;
    }
} // namespace sw
