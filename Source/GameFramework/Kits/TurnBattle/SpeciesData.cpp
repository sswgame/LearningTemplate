#include "pch.h"

#include "GameFramework/Kits/TurnBattle/SpeciesData.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

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
        _listSpecies.push_back( {
            "critter_a",
            "Wild Critter",
            40,
            10,
            { 0, 1 }
        } );
        _listSpecies.push_back( {
            "starter_a",
            "Leaf Pup",
            45,
            11,
            { 0, 1 }
        } );
        rebuildLookup();
    }

    void SpeciesCatalog::rebuildLookup()
    {
        _mapMoveIndex.clear();
        _mapSpeciesIndex.clear();
        for ( size_t moveIndex = 0; moveIndex < _listMove.size(); ++moveIndex )
            _mapMoveIndex.insert_or_assign( hashed_string( _listMove[moveIndex]._id.c_str() ), moveIndex );
        for ( size_t speciesIndex = 0; speciesIndex < _listSpecies.size(); ++speciesIndex )
            _mapSpeciesIndex.insert_or_assign( hashed_string( _listSpecies[speciesIndex]._id.c_str() ), speciesIndex );
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
                if ( StringUtil::isNullOrEmpty( pId ) )
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

        // 종족 파싱이 findMoveIndex 를 부르므로 기술 맵이 먼저 서 있어야 한다.
        rebuildLookup();

        XmlNode speciesNode = root.child( "species" );
        if ( speciesNode.isValid() )
        {
            for ( XmlNode entryNode = speciesNode.child( "entry" ); entryNode.isValid(); entryNode = entryNode.next( "entry" ) )
            {
                const utf8* pId = entryNode.attr( "id" );
                if ( StringUtil::isNullOrEmpty( pId ) )
                    continue;
                const utf8* pName = entryNode.attr( "name" );
                SpeciesDef  def{};
                def._id      = pId;
                def._name    = pName != nullptr ? pName : pId;
                def._baseHp  = entryNode.attrInt( "baseHp", 1 );
                def._baseAtk = entryNode.attrInt( "baseAtk", 1 );
                // move0, move1, ... 을 끊길 때까지 읽는다 — 슬롯 수를 코드가 아니라 데이터가 정한다.
                def._listMoveIndex.clear();
                for ( int32 slot = 0;; ++slot )
                {
                    const string attrName = string( "move" ) + to_string( slot );
                    const utf8*  pMoveId  = entryNode.attr( attrName.c_str() );
                    if ( StringUtil::isNullOrEmpty( pMoveId ) )
                        break;
                    def._listMoveIndex.push_back( MathUtil::max( findMoveIndex( pMoveId ), 0 ) );
                }
                if ( def._listMoveIndex.empty() )
                    def._listMoveIndex.push_back( 0 );
                _listSpecies.push_back( std::move( def ) );
            }
        }

        rebuildLookup();

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
        const auto mapIter = _mapSpeciesIndex.find( hashed_string( pId ) );
        if ( mapIter != _mapSpeciesIndex.end() && mapIter->second < _listSpecies.size() )
            return &_listSpecies[mapIter->second];
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
        const auto mapIter = _mapMoveIndex.find( hashed_string( pId ) );
        return mapIter != _mapMoveIndex.end() ? static_cast<int32>( mapIter->second ) : -1;
    }

    const MoveDef* SpeciesCatalog::findMoveAtSlot( const SpeciesDef& species, size_t slot ) const
    {
        if ( slot >= species._listMoveIndex.size() )
            return nullptr;
        return findMove( species._listMoveIndex[slot] );
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
        m._listPp.clear();
        m._listPp.reserve( pSpecies->_listMoveIndex.size() );
        for ( size_t slot = 0; slot < pSpecies->_listMoveIndex.size(); ++slot )
        {
            const MoveDef* pMove = findMoveAtSlot( *pSpecies, slot );
            m._listPp.push_back( pMove != nullptr ? pMove->_ppMax : 0 );
        }
        m._exp     = 0;
        m._expNext = 40 + level * 10;
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
        _mapMoveIndex.clear();
        _mapSpeciesIndex.clear();
    }
} // namespace sw
