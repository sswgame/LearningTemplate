#include "pch.h"

#include "GameFramework/Kits/Genre/Action/ActionAdventure/Rule/AdventureElementGrid.h"

#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"

#include "GameFramework/Runtime/Foundation/GameData/ConfigRangeUtil.h"

namespace sw
{
    namespace
    {
        struct AdventureElementGridInternal
        {
            /** @brief 알림 이름을 열거형으로 바꿉니다. 테이블의 알림 이름은 열거자 이름과 같습니다. */
            [[nodiscard]] static bool findEventType( const hashed_string& name, AdventureElementEventKind& outType )
            {
                struct NameType
                {
                    const utf8*               _pName;
                    AdventureElementEventKind _type;
                };
                static constexpr NameType kArrEvent[] = {
                    {     "Ignited",      AdventureElementEventKind::Ignited},
                    {   "BurnedOut",    AdventureElementEventKind::BurnedOut},
                    {"Extinguished", AdventureElementEventKind::Extinguished},
                    {      "Melted",       AdventureElementEventKind::Melted},
                    {      "Frozen",       AdventureElementEventKind::Frozen},
                    { "Electrified",  AdventureElementEventKind::Electrified},
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
        , _burningStatus{ constant::invalid_index::kInt32 }
        , _chargedStatus{ constant::invalid_index::kInt32 }
        , _updraftFlag{ constant::invalid_index::kInt32 }
        , _fireStimulus{ constant::invalid_index::kInt32 }
        , _iceStimulus{ constant::invalid_index::kInt32 }
        , _electricStimulus{ constant::invalid_index::kInt32 }
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

    void AdventureElementGrid::populateRuleTable()
    {
        using Internal = AdventureElementGridInternal;
        _table.clear();
        _table.setStepTime( _settings._stepTime );
        // 이 표는 코드가 정한 것이라 더하기는 실패하지 않는다(실패하면 결함 — value 가 단언한다).
        const int32 flammable  = _table.addFlag( "Flammable" ).value();
        const int32 conductive = _table.addFlag( "Conductive" ).value();
        _updraftFlag           = _table.addFlag( "Updraft" ).value();
        _burningStatus         = _table.addStatus( "Burning", ElementStatusKind::Age ).value();
        _chargedStatus         = _table.addStatus( "Charged", ElementStatusKind::Countdown, _settings._chargeSteps ).value();
        // 재질 번호는 `AdventureMaterial` 의 순서와 같다
        const int32 empty = _table.addMaterial( "Empty", {} ).value();
        const int32 grass = _table.addMaterial( "Grass", { hashed_string( "Flammable" ), hashed_string( "Updraft" ) } ).value();
        const int32 wood  = _table.addMaterial( "Wood", { hashed_string( "Flammable" ) } ).value();
        (void)_table.addMaterial( "Metal", { hashed_string( "Conductive" ) } ).value();
        const int32 water = _table.addMaterial( "Water", { hashed_string( "Conductive" ) } ).value();
        const int32 ice   = _table.addMaterial( "Ice", {} ).value();
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

        // 부도체에 떨어진 번개는 그 칸에만 작용한다. 그 위의 생물은 게임이 처리한다.
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

    bool AdventureElementGrid::initialize( int32 width, int32 height, const AdventureElementSettings& settings )
    {
        const string rangeError = ConfigRangeUtil::describeRangeErrors( settings );
        if ( rangeError.empty() == false )
        {
            SW_LOG_ERROR_ID( "AdventureElementGrid.AdventureElementGridRefusedSettings", "AdventureElementGrid: refused settings - %#", rangeError.c_str() );
            return false;
        }
        if ( width < 1 || height < 1 )
        {
            SW_LOG_ERROR_ID( "AdventureElementGrid.AdventureElementGridRefusedSizeX", "AdventureElementGrid: refused size %#x%# (both need 1 or more)", width, height );
            return false;
        }
        _settings = settings;
        populateRuleTable();
        if ( _grid.initialize( width, height, &_table ) == false )
            return false;
        _listEvent.clear();
        return true;
    }

    void AdventureElementGrid::setMaterial( const int2& cell, AdventureMaterial material )
    {
        _grid.setMaterial( cell, static_cast<int32>( material ) );
    }

    AdventureMaterial AdventureElementGrid::getMaterial( const int2& cell ) const
    {
        return static_cast<AdventureMaterial>( _grid.getMaterial( cell ) );
    }

    void AdventureElementGrid::setWind( const int2& wind )
    {
        _grid.setWind( wind );
    }

    bool AdventureElementGrid::applyFire( const int2& cell )
    {
        return _grid.applyStimulus( cell, _fireStimulus ) > 0;
    }

    bool AdventureElementGrid::applyIce( const int2& cell )
    {
        return _grid.applyStimulus( cell, _iceStimulus ) > 0;
    }

    int32 AdventureElementGrid::applyElectric( const int2& cell )
    {
        return _grid.applyStimulus( cell, _electricStimulus );
    }

    void AdventureElementGrid::step()
    {
        _grid.step();
    }

    int32 AdventureElementGrid::update( float32 deltaTime )
    {
        return _grid.update( deltaTime );
    }

    void AdventureElementGrid::pollEvents( vector<AdventureElementEvent>& outListEvent )
    {
        _listGridEvent.clear();
        _grid.pollEvents( _listGridEvent );
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

    bool AdventureElementGrid::isBurning( const int2& cell ) const
    {
        return _grid.hasStatus( cell, _burningStatus );
    }

    bool AdventureElementGrid::isCharged( const int2& cell ) const
    {
        return _grid.hasStatus( cell, _chargedStatus );
    }

    bool AdventureElementGrid::hasUpdraft( const int2& cell ) const
    {
        return isBurning( cell ) && _grid.hasFlag( cell, _updraftFlag );
    }

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

    int32 AdventureElementGrid::countBurning() const
    {
        return _grid.countStatus( _burningStatus );
    }

    uint32 AdventureElementGrid::computeStateHash() const
    {
        return _grid.computeStateHash();
    }

    void AdventureElementGrid::serializeState( StateIO& io )
    {
        io.value( _grid ); // 격자가 크기 불일치와 깨진 데이터를 스스로 거절한다
        if ( io.isReading() )
        {
            _listGridEvent.clear();
            _listEvent.clear();
        }
    }

} // namespace sw
