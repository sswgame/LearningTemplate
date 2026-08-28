#include "pch.h"

#include "Engine/Object/Component/ComponentDefaults.h"

#include "Core/Concurrency/mutex.h"

#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Serialization/Core/SchemaMigrate.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
	namespace
	{
		XmlDocument s_defaultsDoc;
		bool		s_defaultsLoaded{ false };
		string		s_customDefaultsPath;
		mutex		s_defaultsMutex;

		void ensureDefaultsLoaded()
		{
			if ( s_defaultsLoaded )
				return;
			std::scoped_lock<mutex> lock{ s_defaultsMutex };
			if ( s_defaultsLoaded )
				return;
			if ( s_customDefaultsPath.empty() )
				return;

			string absPath;
			if ( s_defaultsDoc.loadResource( s_customDefaultsPath.c_str(), &absPath ) )
				s_defaultsLoaded = true;
		}

		void pushLookupName( vector<string>& listNames, const string& name )
		{
			if ( name.empty() )
				return;
			for ( const string& existing : listNames )
			{
				if ( existing == name )
					return;
			}
			listNames.push_back( name );
		}

		void collectLookupNames( const TypeInfo& typeInfo, vector<string>& listNames )
		{
			string typeNameStr = typeInfo._name.c_str() ? typeInfo._name.c_str() : "";
			pushLookupName( listNames, typeNameStr );

			string stripped = typeNameStr;
			if ( stripped.size() > 9 && stripped.rfind( "Component" ) == stripped.size() - 9 )
				stripped = stripped.substr( 0, stripped.size() - 9 );
			else if ( stripped.size() > 4 && stripped.rfind( "Data" ) == stripped.size() - 4 )
				stripped = stripped.substr( 0, stripped.size() - 4 );
			pushLookupName( listNames, stripped );
		}

		XmlNode findDefaultsNode( XmlNode defaultsNode, const vector<string>& listNames )
		{
			for ( const string& name : listNames )
			{
				if ( name.empty() )
					continue;
				XmlNode node = defaultsNode.child( name.c_str() );
				if ( node.isValid() )
					return node;
			}
			return {};
		}
	} // namespace

	void ComponentDefaults::applyDefaults( void* pInstance, const TypeInfo& typeInfo, const TypeInfo* pAliasTypeInfo )
	{
		if ( pInstance == nullptr )
			return;

		ensureDefaultsLoaded();
		if ( s_defaultsLoaded == false )
			return;

		XmlNode root = s_defaultsDoc.root( "GameData" );
		if ( root.isValid() == false )
			return;

		XmlNode defaultsNode = root.child( "Defaults" );
		if ( defaultsNode.isValid() == false )
			return;

		vector<string> listNames;
		collectLookupNames( typeInfo, listNames );
		if ( pAliasTypeInfo != nullptr )
			collectLookupNames( *pAliasTypeInfo, listNames );

		XmlNode compNode = findDefaultsNode( defaultsNode, listNames );
		if ( compNode.isValid() == false )
			return;

		typeInfo.forEachProperty( [&]( const PropertyInfo& prop )
		{
			const utf8* pPropName = prop._name.c_str();
			if ( pPropName == nullptr )
				return;
			const utf8* pAttrVal = compNode.attr( pPropName );
			if ( pAttrVal == nullptr )
			{
				for ( const hashed_string& alias : prop._listAlias )
				{
					const utf8* pAliasName = alias.c_str();
					if ( pAliasName == nullptr )
						continue;
					pAttrVal = compNode.attr( pAliasName );
					if ( pAttrVal != nullptr )
						break;
				}
			}
			if ( pAttrVal == nullptr )
				return;

			void* pPropPtr = prop.getRawPtr( pInstance );
			parseTextValueCoerced( pPropPtr, prop._typeName, pAttrVal, SerializeContext::getDefault() );
		} );
	}

	void ComponentDefaults::applyDefaults( Component* pComp, const TypeInfo& typeInfo )
	{
		applyDefaults( pComp, typeInfo, nullptr );
	}

	void ComponentDefaults::setDefaultsPath( string_view path )
	{
		std::scoped_lock<mutex> lock{ s_defaultsMutex };
		s_customDefaultsPath = path;
		s_defaultsLoaded	 = false;
	}

	string_view ComponentDefaults::getDefaultsPath()
	{
		std::scoped_lock<mutex> lock{ s_defaultsMutex };
		return s_customDefaultsPath;
	}

	void ComponentDefaults::reload()
	{
		std::scoped_lock<mutex> lock{ s_defaultsMutex };
		s_defaultsLoaded = false;
	}
} // namespace sw
