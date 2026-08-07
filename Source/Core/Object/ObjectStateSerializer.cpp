/**
 * @file ObjectStateSerializer.cpp
 * @brief Auto-generated documentation header
 */
#include "pch.h"
#include "ObjectStateSerializer.h"
#include "Core/Object/GameObject.h"
#include "Core/Reflection/Serializer.h"

namespace sw
{
	std::string ObjectStateSerializer::saveToXmlString( const GameObject* gameObject )
	{
		if ( gameObject == nullptr )
			return {};

		RapidXmlBackend xmlBackend;
		xmlBackend.initXmlSerialization( "GameObjectState" );
		xmlBackend.writeValue( "Name", gameObject->getName().c_str() );
		xmlBackend.writeValue( "ObjectId", std::to_string( gameObject->getObjectId() ).c_str() );
		xmlBackend.writeValue( "IsActive", gameObject->isActive() ? "true" : "false" );

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

		std::string activeStr;
		if ( xmlBackend.readValue( "IsActive", activeStr ) && activeStr.empty() == false )
		{
			gameObject->setActive( activeStr == "true" );
		}

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
}
