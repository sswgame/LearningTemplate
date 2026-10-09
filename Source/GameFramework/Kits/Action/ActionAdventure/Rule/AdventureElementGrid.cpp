#include "pch.h"

#include "GameFramework/Kits/Action/ActionAdventure/Rule/AdventureElementGrid.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        struct AdventureElementGridInternal
        {
            /** @brief 알림 이름 → 열거입니다. 표의 알림 이름은 열거자 이름 그대로입니다. */
            static bool findEventType( const hashed_string& name, AdventureElementEventType& outType )
            {
                struct NameType
                {
                    const utf8*               _pName;
                    AdventureElementEventType _type;
                };
                static constexpr NameType kArrEvent[] = {
                    {     "Ignited",      AdventureElementEventType::Ignited},
                    {   "BurnedOut",    AdventureElementEventType::BurnedOut},
                    {"Extinguished", AdventureElementEventType::Extinguished},
                    {      "Melted",       AdventureElementEventType::Melted},
                    {      "Frozen",       AdventureElementEventType::Frozen},
                    { "Electrified",  AdventureElementEventType::Electrified},
                };
                for ( const NameType& entry : kArrEvent )
                {
                    if ( name == hashed_string( entry._pName ) )
                    {
                        outType = entry._type;
                        return true;
                    }
                }
                return false;
            }

            static ElementStimulusRule makeRule( const utf8* pEvent )
            {
                ElementStimulusRule rule;
                rule._event = hashed_string( pEvent );
                return rule;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    AdventureElementGrid::AdventureElementGrid()
        : _settings{}
        , _table{}
        , _grid{}
        , _listGridEvent{}
        , _listEvent{}
        , _burningStatus{ -1 }
        , _chargedStatus{ -1 }
        , _updraftFlag{ -1 }
        , _fireStimulus{ -1 }
        , _iceStimulus{ -1 }
        , _electricStimulus{ -1 }
    {
    }

    AdventureElementGrid::AdventureElementGrid( const AdventureElementGrid& other )
        : _settings{ other._settings }
        , _table{ other._table }
        , _grid{ other._grid }
        , _listGridEvent{}
        , _listEvent{ other._listEvent }
        , _burningStatus{ other._burningStatus }
        , _chargedStatus{ other._chargedStatus }
        , _updraftFlag{ other._updraftFlag }
        , _fireStimulus{ other._fireStimulus }
        , _iceStimulus{ other._iceStimulus }
        , _electricStimulus{ other._electricStimulus }
    {
        _grid.setTable( &_table );
    }

    AdventureElementGrid& AdventureElementGrid::operator=( const AdventureElementGrid& other )
    {
        if ( this == &other )
            return *this;
        _settings         = other._settings;
        _table            = other._table;
        _grid             = other._grid;
        _listEvent        = other._listEvent;
        _burningStatus    = other._burningStatus;
        _chargedStatus    = other._chargedStatus;
        _updraftFlag      = other._updraftFlag;
        _fireStimulus     = other._fireStimulus;
        _iceStimulus      = other._iceStimulus;
        _electricStimulus = other._electricStimulus;
        _grid.setTable( &_table );
        return *this;
    }

    void AdventureElementGrid::buildRuleTable()
    {
        using Internal = AdventureElementGridInternal;
        _table.clear();
        _table.setStepTime( MathUtil::max( 0.001f, _settings._stepTime ) );
        const int32 flammable  = _table.addFlag( "Flammable" );
        const int32 conductive = _table.addFlag( "Conductive" );
        _updraftFlag           = _table.addFlag( "Updraft" );
        _burningStatus         = _table.addStatus( "Burning", ElementStatusKind::Age );
        _chargedStatus         = _table.addStatus( "Charged", ElementStatusKind::Countdown, _settings._chargeSteps );
        // 재질 번호 = AdventureMaterial 의 순서
        const int32 empty = _table.addMaterial( "Empty", {} );
        const int32 grass = _table.addMaterial( "Grass", { hashed_string( "Flammable" ), hashed_string( "Updraft" ) } );
        const int32 wood  = _table.addMaterial( "Wood", { hashed_string( "Flammable" ) } );
        (void)_table.addMaterial( "Metal", { hashed_string( "Conductive" ) } );
        const int32 water = _table.addMaterial( "Water", { hashed_string( "Conductive" ) } );
        const int32 ice   = _table.addMaterial( "Ice", {} );
        _table.setMaterialParam( grass, "burnSteps", _settings._grassBurnSteps );
        _table.setMaterialParam( wood, "burnSteps", _settings._woodBurnSteps );

        _fireStimulus             = _table.addStimulus( "Fire" );
        ElementStimulusRule melt  = Internal::makeRule( "Melted" );
        melt._material            = ice;
        melt._setMaterial         = water;
        ElementStimulusRule light = Internal::makeRule( "Ignited" );
        light._flag               = flammable;
        light._without            = _burningStatus;
        light._addStatus          = _burningStatus;
        _table.addStimulusRule( _fireStimulus, melt );
        _table.addStimulusRule( _fireStimulus, light );

        _iceStimulus                   = _table.addStimulus( "Ice" );
        ElementStimulusRule extinguish = Internal::makeRule( "Extinguished" );
        extinguish._status             = _burningStatus;
        extinguish._removeStatus       = _burningStatus;
        ElementStimulusRule freeze     = Internal::makeRule( "Frozen" );
        freeze._material               = water;
        freeze._setMaterial            = ice;
        freeze._removeStatus           = _chargedStatus;
        _table.addStimulusRule( _iceStimulus, extinguish );
        _table.addStimulusRule( _iceStimulus, freeze );

        // 부도체에 떨어진 번개는 그 칸만 — 그 위의 생물은 게임이 맞힌다.
        _electricStimulus           = _table.addStimulus( "Electric" );
        ElementStimulusRule conduct = Internal::makeRule( "Electrified" );
        conduct._flag               = conductive;
        conduct._floodStatus        = _chargedStatus;
        conduct._floodThrough       = conductive;
        ElementStimulusRule strike  = Internal::makeRule( "Electrified" );
        strike._addStatus           = _chargedStatus;
        _table.addStimulusRule( _electricStimulus, conduct );
        _table.addStimulusRule( _electricStimulus, strike );

        ElementStepRule burnOut;
        burnOut._kind        = ElementStepRuleKind::Expire;
        burnOut._status      = _burningStatus;
        burnOut._stepsParam  = hashed_string( "burnSteps" );
        burnOut._setMaterial = empty;
        burnOut._event       = hashed_string( "BurnedOut" );
        ElementStepRule thaw;
        thaw._kind        = ElementStepRuleKind::Convert;
        thaw._status      = _burningStatus;
        thaw._material    = ice;
        thaw._setMaterial = water;
        thaw._event       = hashed_string( "Melted" );
        ElementStepRule spread;
        spread._kind       = ElementStepRuleKind::Spread;
        spread._status     = _burningStatus;
        spread._toFlag     = flammable;
        spread._after      = _settings._spreadDelaySteps;
        spread._pattern    = ElementSpreadPattern::Wind;
        spread._bCrosswind = _settings._bCrosswindSpread;
        spread._event      = hashed_string( "Ignited" );
        _table.addStepRule( burnOut );
        _table.addStepRule( thaw );
        _table.addStepRule( spread );
    }

    void AdventureElementGrid::initialize( int32 width, int32 height, const AdventureElementSettings& settings )
    {
        _settings                   = settings;
        _settings._grassBurnSteps   = MathUtil::clamp( _settings._grassBurnSteps, 1, 255 );
        _settings._woodBurnSteps    = MathUtil::clamp( _settings._woodBurnSteps, 1, 255 );
        _settings._spreadDelaySteps = MathUtil::clamp( _settings._spreadDelaySteps, 1, 255 );
        _settings._chargeSteps      = MathUtil::clamp( _settings._chargeSteps, 1, 255 );
        buildRuleTable();
        _grid.initialize( width, height, &_table );
        _listEvent.clear();
    }

    void AdventureElementGrid::setMaterial( const int2& cell, AdventureMaterial material ) { _grid.setMaterial( cell, static_cast<int32>( material ) ); }

    AdventureMaterial AdventureElementGrid::getMaterial( const int2& cell ) const { return static_cast<AdventureMaterial>( _grid.getMaterial( cell ) ); }

    void AdventureElementGrid::setWind( const int2& wind ) { _grid.setWind( wind ); }

    bool AdventureElementGrid::applyFire( const int2& cell ) { return _grid.applyStimulus( cell, _fireStimulus ) > 0; }

    bool AdventureElementGrid::applyIce( const int2& cell ) { return _grid.applyStimulus( cell, _iceStimulus ) > 0; }

    int32 AdventureElementGrid::applyElectric( const int2& cell ) { return _grid.applyStimulus( cell, _electricStimulus ); }

    void AdventureElementGrid::step() { _grid.step(); }

    int32 AdventureElementGrid::update( float32 deltaTime ) { return _grid.update( deltaTime ); }

    void AdventureElementGrid::drainEvents( vector<AdventureElementEvent>& outListEvent )
    {
        _listGridEvent.clear();
        _grid.drainEvents( _listGridEvent );
        for ( const ElementEvent& gridEvent : _listGridEvent )
        {
            AdventureElementEvent event;
            event._cell = gridEvent._cell;
            if ( AdventureElementGridInternal::findEventType( gridEvent._name, event._type ) )
                _listEvent.push_back( event );
        }
        outListEvent.insert( outListEvent.end(), _listEvent.begin(), _listEvent.end() );
        _listEvent.clear();
    }

    bool AdventureElementGrid::isBurning( const int2& cell ) const { return _grid.hasStatus( cell, _burningStatus ); }

    bool AdventureElementGrid::isCharged( const int2& cell ) const { return _grid.hasStatus( cell, _chargedStatus ); }

    bool AdventureElementGrid::hasUpdraft( const int2& cell ) const { return isBurning( cell ) && _grid.hasFlag( cell, _updraftFlag ); }

    void AdventureElementGrid::collectUpdraft( vector<int2>& outListCell ) const
    {
        outListCell.clear();
        for ( int32 y = 0; y < _grid.getHeight(); ++y )
        {
            for ( int32 x = 0; x < _grid.getWidth(); ++x )
            {
                if ( hasUpdraft( int2{ x, y } ) )
                    outListCell.push_back( int2{ x, y } );
            }
        }
    }

    int32 AdventureElementGrid::countBurning() const { return _grid.countStatus( _burningStatus ); }

    uint32 AdventureElementGrid::computeStateHash() const { return _grid.computeStateHash(); }

    void AdventureElementGrid::writeState( Archive& outArchive ) const { _grid.writeState( outArchive ); }

    bool AdventureElementGrid::readState( Archive& archive )
    {
        // 격자가 크기 · 깨짐을 스스로 거절한다(그대로 둔다).
        if ( _grid.readState( archive ) == false )
            return false;
        _listGridEvent.clear();
        _listEvent.clear();
        return true;
    }
} // namespace sw
