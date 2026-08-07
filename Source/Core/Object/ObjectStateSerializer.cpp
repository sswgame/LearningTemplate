/**
 * @file ObjectStateSerializer.cpp
 * @brief ObjectStateSerializer 구현
 */
#include "pch.h"
#include "ObjectStateSerializer.h"
#include "Core/Object/GameObject.h"
#include "Core/Object/Component.h"
#include "Core/Object/TagSystem.h"
#include "Core/Reflection/Serializer.h"
#include "Core/Reflection/ReflectionCore.h"
#include "Core/Utility/Log/Logger.h"

namespace sw
{
	namespace
	{
		std::string formatTagId( const TagID& tag )
		{
			// id:parentHash:ancestorCount:a0,a1,...
			std::string out = std::to_string( tag._id );
			out += ':';
			out += std::to_string( tag._parentHash );
			out += ':';
			out += std::to_string( static_cast<uint32>( tag._ancestorCount ) );
			for ( uint8 i = 0; i < tag._ancestorCount; ++i )
			{
				out += ':';
				out += std::to_string( tag._ancestorHashes[i] );
			}
			return out;
		}

		bool parseTagId( std::string_view text, TagID& outTag )
		{
			outTag = TagID{};
			if ( text.empty() )
				return false;

			std::vector<uint64> parts;
			size_t				start = 0;
			while ( start <= text.size() )
			{
				const size_t sep = text.find( ':', start );
				const auto	 token = text.substr( start, sep == std::string_view::npos ? std::string_view::npos : sep - start );
				if ( token.empty() == false )
				{
					try
					{
						parts.push_back( std::stoull( std::string( token ) ) );
					}
					catch ( ... )
					{
						return false;
					}
				}
				if ( sep == std::string_view::npos )
					break;
				start = sep + 1;
			}

			if ( parts.empty() )
				return false;

			outTag._id		   = parts[0];
			outTag._parentHash = parts.size() > 1 ? parts[1] : 0;
			const uint32 ancCount = parts.size() > 2 ? static_cast<uint32>( parts[2] ) : 0;
			outTag._ancestorCount = static_cast<uint8>( std::min<uint32>( ancCount, TagID::kMaxAncestors ) );
			for ( uint8 i = 0; i < outTag._ancestorCount; ++i )
			{
				const size_t idx = 3u + static_cast<size_t>( i );
				outTag._ancestorHashes[i] = idx < parts.size() ? parts[idx] : 0;
			}
			return outTag.isValid();
		}
	} // namespace

	std::string ObjectStateSerializer::saveToXmlString( const GameObject* gameObject )
	{
		if ( gameObject == nullptr )
			return {};

		RapidXmlBackend xmlBackend;
		xmlBackend.initXmlSerialization( "GameObjectState" );
		xmlBackend.writeValue( "Name", gameObject->getName().c_str() );
		// ObjectId is emitted for debugging/diff only. Runtime IDs are process-local and must not be restored
		// (would collide with GameObject::_s_nextObjectId and break manager id maps).
		xmlBackend.writeValue( "ObjectId", std::to_string( gameObject->getObjectId() ).c_str() );
		xmlBackend.writeValue( "IsActive", gameObject->isActive() ? "true" : "false" );

		// When TypeInfo is registered, also emit reflected PROPERTY fields via XmlSerializer
		// into a nested document stored as ReflectedXml (keeps GameObjectState root/aliases).
		if ( const TypeInfo* typeInfo = gameObject->getTypeInfo() )
		{
			const std::string reflected = XmlSerializer::serialize( gameObject, *typeInfo );
			if ( reflected.empty() == false )
				xmlBackend.writeValue( "ReflectedXml", reflected.c_str() );
		}

		xmlBackend.beginArray( "Tags" );
		for ( const TagID& tag : gameObject->getTags().getTags() )
			xmlBackend.writeArrayItem( formatTagId( tag ).c_str() );
		xmlBackend.endArray();

		xmlBackend.beginMap( "Components" );
		for ( Component* comp : gameObject->getAllComponents() )
		{
			if ( comp == nullptr )
				continue;

			xmlBackend.beginMapEntry();
			xmlBackend.writeMapKey( comp->getComponentName().c_str() );

			std::string reflected;
			if ( const TypeInfo* typeInfo = comp->getTypeInfo() )
				reflected = XmlSerializer::serialize( comp, *typeInfo );
			xmlBackend.writeMapValue( reflected.c_str() );
			xmlBackend.endMapEntry();
		}
		xmlBackend.endMap();

		return xmlBackend.endSerialize();
	}

	bool ObjectStateSerializer::loadFromXmlString( GameObject* gameObject, std::string_view xmlString )
	{
		if ( gameObject == nullptr || xmlString.empty() )
			return false;

		std::string		xmlCopy( xmlString );
		RapidXmlBackend xmlBackend;
		if ( xmlBackend.initXmlDeserialization( xmlCopy.c_str(), "GameObjectState" ) == false )
			return false;

		std::string nameStr;
		if ( xmlBackend.readValue( "Name", nameStr ) && nameStr.empty() == false )
		{
			gameObject->setName( hashed_string( nameStr.c_str() ) );
		}

		// Intentionally skip ObjectId restore — IDs are runtime-allocated and not stable across sessions.

		std::string activeStr;
		if ( xmlBackend.readValue( "IsActive", activeStr ) && activeStr.empty() == false )
		{
			gameObject->setActive( activeStr == "true" );
		}

		std::string reflectedXml;
		if ( xmlBackend.readValue( "ReflectedXml", reflectedXml ) && reflectedXml.empty() == false )
		{
			if ( const TypeInfo* typeInfo = gameObject->getTypeInfo() )
			{
				if ( XmlSerializer::deserialize( gameObject, *typeInfo, reflectedXml ) == false )
					SW_LOG_WARNING( "[ObjectStateSerializer] ReflectedXml deserialize failed for %#", typeInfo->_fullyQualifiedName.c_str() );
			}
		}

		XmlArrayItemDelegate tagCb = SW_DELEGATE_LAMBDA( XmlArrayItemDelegate, [gameObject]( std::string_view itemStr )
		{
			TagID tag{};
			if ( parseTagId( itemStr, tag ) )
				gameObject->addTag( tag );
		} );
		xmlBackend.iterateArray( "Tags", tagCb );

		XmlMapItemDelegate compCb = SW_DELEGATE_LAMBDA( XmlMapItemDelegate, [gameObject]( std::string_view keyStr, std::string_view valStr )
		{
			if ( keyStr.empty() )
				return;

			Component* comp = gameObject->addComponentByName( hashed_string( std::string( keyStr ).c_str() ) );
			if ( comp == nullptr )
			{
				const std::string typeName( keyStr );
				SW_LOG_WARNING( "[ObjectStateSerializer] Failed to recreate component '%#'", typeName.c_str() );
				return;
			}

			if ( valStr.empty() == false )
			{
				if ( const TypeInfo* typeInfo = comp->getTypeInfo() )
				{
					if ( XmlSerializer::deserialize( comp, *typeInfo, std::string( valStr ) ) == false )
						SW_LOG_WARNING( "[ObjectStateSerializer] Component ReflectedXml deserialize failed for %#",
										typeInfo->_fullyQualifiedName.c_str() );
				}
			}
		} );
		xmlBackend.iterateMap( "Components", compCb );

		return true;
	}

	bool ObjectStateSerializer::saveToXmlFile( const GameObject* gameObject, const std::string_view filePath )
	{
		std::string xmlStr = saveToXmlString( gameObject );
		if ( xmlStr.empty() )
			return false;

		return FileUtil::writeFile( std::string{ filePath }, reinterpret_cast<const uint8*>( xmlStr.data() ), xmlStr.size() );
	}

	bool ObjectStateSerializer::loadFromXmlFile( GameObject* gameObject, const std::string_view filePath )
	{
		std::vector<uint8> data;
		if ( FileUtil::readFile( std::string{ filePath }, data ) == false || data.empty() )
			return false;

		std::string_view xmlStr( reinterpret_cast<const char*>( data.data() ), data.size() );
		return loadFromXmlString( gameObject, xmlStr );
	}

	void ObjectStateSerializer::openSaveFileDialog( const GameObject* gameObject, FileDialogDelegate onSaveDone )
	{
		FileDialogParams params;
		params._type				= FileDialogParams::Type::Save;
		params._description			= "GameObject State XML File (*.xml)";
		params._filterExtensionList = { "xml" };
		params._bEnableMultiselect	= false;

		FileDialogDelegate del = SW_DELEGATE_LAMBDA( FileDialogDelegate, [gameObject, onSaveDone]( const std::vector<std::string>& fileNames )
		{
			if ( fileNames.empty() == false && gameObject != nullptr )
			{
				saveToXmlFile( gameObject, fileNames.front() );
			}
			if ( onSaveDone.isBound() )
			{
				onSaveDone( fileNames );
			}
		} );
		FileUtil::openFileDialog( params, del );
	}

	void ObjectStateSerializer::openLoadFileDialog( GameObject* gameObject, FileDialogDelegate onLoadDone )
	{
		FileDialogParams params;
		params._type				= FileDialogParams::Type::Open;
		params._description			= "GameObject State XML File (*.xml)";
		params._filterExtensionList = { "xml" };
		params._bEnableMultiselect	= false;

		FileDialogDelegate del = SW_DELEGATE_LAMBDA( FileDialogDelegate, [gameObject, onLoadDone]( const std::vector<std::string>& fileNames )
		{
			if ( fileNames.empty() == false && gameObject != nullptr )
			{
				loadFromXmlFile( gameObject, fileNames.front() );
			}
			if ( onLoadDone.isBound() )
			{
				onLoadDone( fileNames );
			}
		} );
		FileUtil::openFileDialog( params, del );
	}
} // namespace sw
