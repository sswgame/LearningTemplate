#include "pch.h"

#include "GameFramework/Kits/TurnBattle/SpeciesData.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
    SW_LOG_CALLER( "SpeciesCatalog" );

    SpeciesCatalog::SpeciesCatalog()
        : _listMove{}
        , _listSpecies{}
    {
    }

    SpeciesCatalog::~SpeciesCatalog()
    {
        clear();
    }

    void SpeciesCatalog::seedFallback()
    {
        _listMove.clear();
        _listSpecies.clear();
        _listMove.push_back( { "tackle", "Tackle", 40, 35 } );
        _listMove.push_back( { "growl", "Growl", 0, 40 } );
        _listSpecies.push_back( { "critter_a", "Wild Critter", 40, 10, 0, 1 } );
        _listSpecies.push_back( { "starter_a", "Leaf Pup", 45, 11, 0, 1 } );
    }

    bool SpeciesCatalog::loadFromResource( string_view assetRelativePath )
    {
        clear();

        XmlDocument doc;
        string      absPath;
        if ( doc.loadResource( assetRelativePath, &absPath ) == false )
        {
            SW_LOG_ERROR( "Failed to read %# — using fallback table.", assetRelativePath );
            seedFallback();
            return false;
        }

        XmlNode root = doc.root( "SpeciesCatalog" );
        if ( root.isValid() == false )
        {
            SW_LOG_ERROR( "Missing <SpeciesCatalog> in %# — using fallback.", absPath );
            seedFallback();
            return false;
        }

        XmlNode movesNode = root.child( "moves" );
        if ( movesNode.isValid() )
        {
            for ( XmlNode moveNode = movesNode.child( "move" ); moveNode.isValid(); moveNode = moveNode.next( "move" ) )
            {
                const utf8* pId = moveNode.attr( "id" );
                if ( pId == nullptr || pId[0] == '\0' )
                    continue;
                const utf8* pName = moveNode.attr( "name" );
                MoveDef     def{};
                def._id    = pId;
                def._name  = pName != nullptr ? pName : pId;
                def._power = moveNode.attrInt( "power", 0 );
                def._ppMax = moveNode.attrInt( "ppMax", 0 );
                _listMove.push_back( std::move( def ) );
            }
        }

        XmlNode speciesNode = root.child( "species" );
        if ( speciesNode.isValid() )
        {
            for ( XmlNode entryNode = speciesNode.child( "entry" ); entryNode.isValid(); entryNode = entryNode.next( "entry" ) )
            {
                const utf8* pId = entryNode.attr( "id" );
                if ( pId == nullptr || pId[0] == '\0' )
                    continue;
                const utf8* pName = entryNode.attr( "name" );
                SpeciesDef  def{};
                def._id      = pId;
                def._name    = pName != nullptr ? pName : pId;
                def._baseHp  = entryNode.attrInt( "baseHp", 1 );
                def._baseAtk = entryNode.attrInt( "baseAtk", 1 );
                def._move0   = findMoveIndex( entryNode.attr( "move0" ) );
                def._move1   = findMoveIndex( entryNode.attr( "move1" ) );
                def._move0   = MathUtil::max( def._move0, 0 );
                def._move1   = MathUtil::max( def._move1, 0 );
                _listSpecies.push_back( std::move( def ) );
            }
        }

        if ( _listMove.empty() || _listSpecies.empty() )
        {
            SW_LOG_ERROR( "Empty table in %# — using fallback.", absPath );
            seedFallback();
            return false;
        }

        SW_LOG_INFO( "Loaded %# moves, %# species from %#",
                     static_cast<uint32>( _listMove.size() ), static_cast<uint32>( _listSpecies.size() ), absPath );
        return true;
    }

    const SpeciesDef* SpeciesCatalog::findSpecies( const utf8* pId ) const
    {
        if ( _listSpecies.empty() )
            const_cast<SpeciesCatalog*>( this )->seedFallback();
        if ( pId == nullptr )
            return &_listSpecies[0];
        for ( const SpeciesDef& speciesDef : _listSpecies )
        {
            if ( speciesDef._id == pId )
                return &speciesDef;
        }
        return &_listSpecies[0];
    }

    const MoveDef* SpeciesCatalog::findMove( int32 index ) const
    {
        if ( _listMove.empty() )
            const_cast<SpeciesCatalog*>( this )->seedFallback();
        if ( index < 0 || index >= static_cast<int32>( _listMove.size() ) )
            return &_listMove[0];
        return &_listMove[static_cast<size_t>( index )];
    }

    int32 SpeciesCatalog::findMoveIndex( const utf8* pId ) const
    {
        if ( pId == nullptr )
            return -1;
        for ( size_t moveIndex = 0; moveIndex < _listMove.size(); ++moveIndex )
        {
            if ( _listMove[moveIndex]._id == pId )
                return static_cast<int32>( moveIndex );
        }
        return -1;
    }

    PartyMember SpeciesCatalog::makeWild( const utf8* pSpeciesId, int32 level ) const
    {
        const SpeciesDef* pSpecies = findSpecies( pSpeciesId );
        PartyMember       m{};
        m._speciesId = pSpecies->_id;
        m._nickname  = pSpecies->_name;
        m._level     = level;
        m._hpMax     = pSpecies->_baseHp + level * 2;
        m._hp        = m._hpMax;
        m._pp0       = findMove( pSpecies->_move0 )->_ppMax;
        m._pp1       = findMove( pSpecies->_move1 )->_ppMax;
        m._exp       = 0;
        m._expNext   = 40 + level * 10;
        return m;
    }

    PartyMember SpeciesCatalog::makeStarter( const utf8* pSpeciesId, int32 level ) const
    {
        return makeWild( pSpeciesId, level );
    }

    void SpeciesCatalog::clear()
    {
        _listMove.clear();
        _listSpecies.clear();
    }
} // namespace sw
