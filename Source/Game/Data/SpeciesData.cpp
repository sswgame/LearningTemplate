/**
 * @file SpeciesData.cpp
 */
#include "SpeciesData.h"
#include <cstring>

namespace sw
{
	const SpeciesDef* SpeciesCatalog::findSpecies( const char* id )
	{
		if ( id == nullptr )
			return &kSpecies[0];
		for ( size_t i = 0; i < kSpeciesCount; ++i )
		{
			if ( std::strcmp( kSpecies[i]._id, id ) == 0 )
				return &kSpecies[i];
		}
		return &kSpecies[0];
	}

	const MoveDef* SpeciesCatalog::findMove( int32 index )
	{
		if ( index < 0 || index >= static_cast<int32>( kMoveCount ) )
			return &kMoves[0];
		return &kMoves[static_cast<size_t>( index )];
	}

	PartyMember SpeciesCatalog::makeWild( const char* speciesId, int32 level )
	{
		const SpeciesDef* sp = findSpecies( speciesId );
		PartyMember		  m{};
		m._speciesId = sp->_id;
		m._nickname	 = sp->_name;
		m._level	 = level;
		m._hpMax	 = sp->_baseHp + level * 2;
		m._hp		 = m._hpMax;
		m._pp0		 = findMove( sp->_move0 )->_ppMax;
		m._pp1		 = findMove( sp->_move1 )->_ppMax;
		m._exp		 = 0;
		m._expNext	 = 40 + level * 10;
		return m;
	}

	PartyMember SpeciesCatalog::makeStarter()
	{
		return makeWild( "starter_a", 5 );
	}
} // namespace sw
