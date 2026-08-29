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
		struct ComponentDefaultsInternal
		{
			static inline XmlDocument s_defaultsDoc;
			static inline bool		  s_defaultsLoaded{ false };
			static inline string	  s_customDefaultsPath;
			static inline mutex		  s_defaultsMutex;

			static void ensureDefaultsLoaded()
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

			static void pushLookupName( vector<string>& listNames, const string& name )
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

			static void collectLookupNames( const TypeInfo& typeInfo, vector<string>& listNames )
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

			static XmlNode findDefaultsNode( XmlNode defaultsNode, const vector<string>& listNames )
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
		};
	} // namespace
} // namespace sw

namespace sw
{
	void ComponentDefaults::applyDefaults( void* pInstance, const TypeInfo& typeInfo, const TypeInfo* pAliasTypeInfo )
	{
		if ( pInstance == nullptr )
			return;

		ComponentDefaultsInternal::ensureDefaultsLoaded();
		if ( ComponentDefaultsInternal::s_defaultsLoaded == false )
			return;

		XmlNode root = ComponentDefaultsInternal::s_defaultsDoc.root( "GameData" );
		if ( root.isValid() == false )
			return;

		XmlNode defaultsNode = root.child( "Defaults" );
		if ( defaultsNode.isValid() == false )
			return;

		vector<string> listNames;
		ComponentDefaultsInternal::collectLookupNames( typeInfo, listNames );
		if ( pAliasTypeInfo != nullptr )
			ComponentDefaultsInternal::collectLookupNames( *pAliasTypeInfo, listNames );

		XmlNode compNode = ComponentDefaultsInternal::findDefaultsNode( defaultsNode, listNames );
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
		std::scoped_lock<mutex> lock{ ComponentDefaultsInternal::s_defaultsMutex };
		ComponentDefaultsInternal::s_customDefaultsPath = path;
		ComponentDefaultsInternal::s_defaultsLoaded		= false;
	}

	string_view ComponentDefaults::getDefaultsPath()
	{
		std::scoped_lock<mutex> lock{ ComponentDefaultsInternal::s_defaultsMutex };
		return ComponentDefaultsInternal::s_customDefaultsPath;
	}

	void ComponentDefaults::reload()
	{
		std::scoped_lock<mutex> lock{ ComponentDefaultsInternal::s_defaultsMutex };
		ComponentDefaultsInternal::s_defaultsLoaded = false;
	}
} // namespace sw
