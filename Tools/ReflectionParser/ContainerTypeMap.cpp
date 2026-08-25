#include "pch.h"

#include "ReflectionParser/ContainerTypeMap.h"

#include "Core/Log/Logger.h"

#include "Engine/Reflection/ReflectionEnumNames.h"

namespace sw
{
	ContainerTypeMap& ContainerTypeMap::instance()
	{
		static ContainerTypeMap s_map;
		return s_map;
	}

	void ContainerTypeMap::clear()
	{
		_listRules.clear();
		_bLoaded = false;
	}

	void ContainerTypeMap::registerRule( const string& match, ContainerKind kind, const string& type )
	{
		if ( match.empty() || type.empty() )
			return;
		ContainerTypeRule rule;
		rule._match = match;
		rule._kind	= kind;
		rule._type	= type;
		_listRules.push_back( std::move( rule ) );
	}

	void ContainerTypeMap::registerRule( const string& match, const string& kindSpelling,
										 const string& type )
	{
		ContainerKind kind = ContainerKind::Sequence;
		if ( kindSpelling.empty() == false && tryParseContainerKind( kindSpelling, kind ) == false )
			SW_LOG_WARNING( "[ContainerTypeMap] Unknown kind '%#', using Sequence", kindSpelling );
		registerRule( match, kind, type );
	}

	const ContainerTypeRule* ContainerTypeMap::match( const std::string_view clangTypeSpelling ) const
	{
		for ( const ContainerTypeRule& rule : _listRules )
		{
			if ( clangTypeSpelling.find( rule._match ) != std::string_view::npos )
				return &rule;
		}
		return nullptr;
	}
} // namespace sw
