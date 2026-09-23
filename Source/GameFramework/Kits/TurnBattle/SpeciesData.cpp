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
        // **여기서 심는다.** 예전에는 `findSpecies()` · `findMove()` 가 비어 있으면 그 자리에서
        // `const_cast` 로 자기 자신을 고쳐 폴백을 심었다. 그 둘은 `const` 이고 이 카탈로그는
        // 서비스라 여러 스레드가 동시에 읽는다. 읽기인 줄 알고 부른 함수가 벡터를 키우고
        // 있었다. 게다가 `clear()` 가 아무 뜻도 없었다(다음 조회가 다시 채운다).
        seedFallback();
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

        XmlNode root = doc.getRoot( "SpeciesCatalog" );
        if ( root.isValid() == false )
        {
            SW_LOG_ERROR( "Missing <SpeciesCatalog> in %# — using fallback.", absPath );
            seedFallback();
            return false;
        }

        XmlNode movesNode = root.findChild( "moves" );
        if ( movesNode.isValid() )
        {
            for ( XmlNode moveNode = movesNode.findChild( "move" ); moveNode.isValid(); moveNode = moveNode.findNextSibling( "move" ) )
            {
                const utf8* pId = moveNode.findAttribute( "id" );
                if ( StringUtil::isNullOrEmpty( pId ) )
                    continue;
                const utf8* pName = moveNode.findAttribute( "name" );
                MoveDef     def{};
                def._id    = pId;
                def._name  = pName != nullptr ? pName : pId;
                def._power = moveNode.getAttributeInt( "power", 0 );
                def._ppMax = moveNode.getAttributeInt( "ppMax", 0 );
                _listMove.push_back( std::move( def ) );
            }
        }

        // 종족 파싱이 findMoveIndex 를 부르므로 기술 맵이 먼저 서 있어야 한다.
        rebuildLookup();

        XmlNode speciesNode = root.findChild( "species" );
        if ( speciesNode.isValid() )
        {
            for ( XmlNode entryNode = speciesNode.findChild( "entry" ); entryNode.isValid(); entryNode = entryNode.findNextSibling( "entry" ) )
            {
                const utf8* pId = entryNode.findAttribute( "id" );
                if ( StringUtil::isNullOrEmpty( pId ) )
                    continue;
                const utf8* pName = entryNode.findAttribute( "name" );
                SpeciesDef  def{};
                def._id      = pId;
                def._name    = pName != nullptr ? pName : pId;
                def._baseHp  = entryNode.getAttributeInt( "baseHp", 1 );
                def._baseAtk = entryNode.getAttributeInt( "baseAtk", 1 );
                // move0, move1, ... 을 끊길 때까지 읽는다. 슬롯 수를 코드가 아니라 데이터가 정한다.
                def._listMoveIndex.clear();
                for ( int32 slot = 0;; ++slot )
                {
                    const string attrName = string( "move" ) + to_string( slot );
                    const utf8*  pMoveId  = entryNode.findAttribute( attrName.c_str() );
                    if ( StringUtil::isNullOrEmpty( pMoveId ) )
                        break;

                    // 모르는 기술 id 는 **말하고 나서** 0 번으로 떨어진다. 예전에는 조용히
                    // 떨어져서, 철자 하나 틀리면 그 종족의 기술이 모두 첫 기술로 바뀌었다.
                    const int32 moveIndex = findMoveIndex( pMoveId );
                    if ( moveIndex < 0 )
                        SW_LOG_WARNING( "Unknown move id '%#' on species '%#' — using the first move.", pMoveId, def._id );
                    def._listMoveIndex.push_back( MathUtil::max( moveIndex, 0 ) );
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

    // 아래 둘은 **읽기만 한다.** 비어 있으면 nullptr 이다. 부르는 쪽은 이미 모두 널을 본다.
    const SpeciesDef* SpeciesCatalog::findSpecies( const utf8* pId ) const
    {
        if ( _listSpecies.empty() )
            return nullptr;
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
            return nullptr;
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
        PartyMember m{};

        const SpeciesDef* pSpecies = findSpecies( pSpeciesId );
        if ( pSpecies == nullptr )
            return m;

        // **레벨로 곱하기 전에 자른다.** 레벨은 세이브에서 오고 세이브는 손으로 고칠 수 있다.
        // `level * 10` 하나면 부호 있는 정수 오버플로(= 미정의 동작)다.
        const int32 safeLevel = MathUtil::clamp( level, 1, kMaxLevel );

        m._speciesId = pSpecies->_id;
        m._nickname  = pSpecies->_name;
        m._level     = safeLevel;
        m._hpMax     = pSpecies->_baseHp + safeLevel * 2;
        m._hp        = m._hpMax;
        m._listPp.clear();
        m._listPp.reserve( pSpecies->_listMoveIndex.size() );
        for ( size_t slot = 0; slot < pSpecies->_listMoveIndex.size(); ++slot )
        {
            const MoveDef* pMove = findMoveAtSlot( *pSpecies, slot );
            m._listPp.push_back( pMove != nullptr ? pMove->_ppMax : 0 );
        }
        m._exp     = 0;
        m._expNext = 40 + safeLevel * 10;
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
