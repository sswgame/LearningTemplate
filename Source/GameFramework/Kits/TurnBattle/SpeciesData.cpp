#include "pch.h"

#include "GameFramework/Kits/TurnBattle/SpeciesData.h"

#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{

	namespace
	{

		vector<MoveDef>& moves()
		{
			static vector<MoveDef> s_listMoves;
			return s_listMoves;
		}

		vector<SpeciesDef>& species()
		{
			static vector<SpeciesDef> s_listSpecies;
			return s_listSpecies;
		}

		void seedFallback()
		{
			moves().clear();
			species().clear();
			moves().push_back( { "tackle", "Tackle", 40, 35 } );
			moves().push_back( { "growl", "Growl", 0, 40 } );
			species().push_back( { "critter_a", "Wild Critter", 40, 10, 0, 1 } );
			species().push_back( { "starter_a", "Leaf Pup", 45, 11, 0, 1 } );
		}


	} // namespace

	bool SpeciesCatalog::loadFromResource( string_view assetRelativePath )
	{
		clear();

		XmlDocument doc;
		string		absPath;
		if ( doc.loadResource( assetRelativePath, &absPath ) == false )
		{
			SW_LOG_ERROR( "[SpeciesCatalog] Failed to read %# — using fallback table.", assetRelativePath );
			seedFallback();
			return false;
		}

		XmlNode root = doc.root( "SpeciesCatalog" );
		if ( root.isValid() == false )
		{
			SW_LOG_ERROR( "[SpeciesCatalog] Missing <SpeciesCatalog> in %# — using fallback.", absPath );
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
				MoveDef		def{};
				def._id	   = pId;
				def._name  = pName != nullptr ? pName : pId;
				def._power = moveNode.attrInt( "power", 0 );
				def._ppMax = moveNode.attrInt( "ppMax", 0 );
				moves().push_back( std::move( def ) );
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
				SpeciesDef	def{};
				def._id		 = pId;
				def._name	 = pName != nullptr ? pName : pId;
				def._baseHp	 = entryNode.attrInt( "baseHp", 1 );
				def._baseAtk = entryNode.attrInt( "baseAtk", 1 );
				def._move0	 = findMoveIndex( entryNode.attr( "move0" ) );
				def._move1	 = findMoveIndex( entryNode.attr( "move1" ) );
				if ( def._move0 < 0 )
					def._move0 = 0;
				if ( def._move1 < 0 )
					def._move1 = 0;
				species().push_back( std::move( def ) );
			}
		}

		if ( moves().empty() || species().empty() )
		{
			SW_LOG_ERROR( "[SpeciesCatalog] Empty table in %# — using fallback.", absPath );
			seedFallback();
			return false;
		}

		SW_LOG_INFO( "[SpeciesCatalog] Loaded %# moves, %# species from %#",
					 static_cast<uint32>( moves().size() ), static_cast<uint32>( species().size() ), absPath );
		return true;
	}

	const SpeciesDef* SpeciesCatalog::findSpecies( const utf8* pId )
	{
		if ( species().empty() )
			seedFallback();
		if ( pId == nullptr )
			return &species()[0];
		for ( const SpeciesDef& speciesDef : species() )
		{
			if ( speciesDef._id == pId )
				return &speciesDef;
		}
		return &species()[0];
	}

	const MoveDef* SpeciesCatalog::findMove( int32 index )
	{
		if ( moves().empty() )
			seedFallback();
		if ( index < 0 || index >= static_cast<int32>( moves().size() ) )
			return &moves()[0];
		return &moves()[static_cast<size_t>( index )];
	}

	int32 SpeciesCatalog::findMoveIndex( const utf8* pId )
	{
		if ( pId == nullptr )
			return -1;
		for ( size_t moveIndex = 0; moveIndex < moves().size(); ++moveIndex )
		{
			if ( moves()[moveIndex]._id == pId )
				return static_cast<int32>( moveIndex );
		}
		return -1;
	}

	PartyMember SpeciesCatalog::makeWild( const utf8* pSpeciesId, int32 level )
	{
		const SpeciesDef* pSpecies = findSpecies( pSpeciesId );
		PartyMember		  m{};
		m._speciesId = pSpecies->_id;
		m._nickname	 = pSpecies->_name;
		m._level	 = level;
		m._hpMax	 = pSpecies->_baseHp + level * 2;
		m._hp		 = m._hpMax;
		m._pp0		 = findMove( pSpecies->_move0 )->_ppMax;
		m._pp1		 = findMove( pSpecies->_move1 )->_ppMax;
		m._exp		 = 0;
		m._expNext	 = 40 + level * 10;
		return m;
	}

	PartyMember SpeciesCatalog::makeStarter( const utf8* pSpeciesId, int32 level )
	{
		return makeWild( pSpeciesId, level );
	}

	void SpeciesCatalog::clear()
	{
		moves().clear();
		species().clear();
	}
} // namespace sw
