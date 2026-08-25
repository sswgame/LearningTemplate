#include "pch.h"

#include "Core/Concurrency/mutex.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialInternal.h"
#include "Engine/Utility/Resource/AssetFormat.h"
#include "Engine/Utility/Resource/ResourceManager.h"
#include "Engine/Utility/Task/TaskManager.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
	bool Material::loadFromFile( string_view assetRelativePath )
	{
		string absPath = ResourceUtil::getResourcePath( assetRelativePath );
		if ( absPath.empty() )
			absPath = assetRelativePath;

		if ( FileUtil::fileExists( absPath ) == false )
			return false;

		vector<uint8> listFileData;
		if ( FileUtil::readFile( absPath, listFileData ) == false )
			return false;

		const string text( reinterpret_cast<const utf8*>( listFileData.data() ), listFileData.size() );
		const string trimmed = StringUtil::trim( text.c_str() );
		if ( trimmed.empty() )
			return false;

		if ( trimmed[0] != '<' )
		{
			SW_LOG_ERROR( "[Material] Expected XML material file: %#", absPath );
			return false;
		}
		return loadFromXml( text );
	}

	TaskHandle Material::loadFromFileAsync( string_view assetRelativePath )
	{
		shared_ptr<AsyncLoadState> state  = _asyncLoadState;
		TaskHandle				   handle = engine::getTaskManager().emplaceTask( "LoadMaterialAsync", SW_DELEGATE_LAMBDA( TaskDelegate, [state, path = string( assetRelativePath )]()
		{
			std::scoped_lock<mutex> lock{ state->_mutex };
			if ( state->_pMaterial == nullptr )
				return;
			state->_pMaterial->loadFromFile( path );
		} ) );
		handle.submit();
		return handle;
	}

	bool Material::saveToFile( string_view assetRelativePath ) const
	{
		string absPath = ResourceUtil::getResourcePath( assetRelativePath );
		if ( absPath.empty() )
			absPath = assetRelativePath;

		this->syncDescFromRuntime();

		XmlDocument doc;
		XmlNode		root = doc.appendRoot( "MaterialDesc" );

		engine::getResourceManager().getAssetFormatRegistry().writeXmlVersion( root, AssetFormatVersions::kMaterial );
		appendAttr( root, "name", _desc._name );
		appendAttr( root, "shaderPath", _desc._shaderPath );
		appendAttr( root, "blendMode", blendModeToString( _blendMode ) );

		XmlNode props = root.appendChild( "_properties" );

		for ( const MaterialProperty& prop : _data._listProperties )
		{
			XmlNode item = props.appendChild( "item" );

			appendAttr( item, "name", prop._name );
			appendAttr( item, "type", typeToString( prop._type ) );
			if ( prop._shaderType != MaterialPropertyType::Unknown )
				appendAttr( item, "shaderType", typeToString( prop._shaderType ) );
			if ( prop._defaultValue.empty() == false )
				appendAttr( item, "defaultValue", prop._defaultValue );
			if ( prop._value.empty() == false && prop._value != prop._defaultValue )
				appendAttr( item, "value", prop._value );
			if ( prop._assetPath.empty() == false )
				appendAttr( item, "assetPath", prop._assetPath );
			if ( prop._enumType.empty() == false )
				appendAttr( item, "enumType", prop._enumType );
			if ( prop._displayName.empty() == false )
				appendAttr( item, "displayName", prop._displayName );
			if ( prop._group.empty() == false )
				appendAttr( item, "group", prop._group );
			if ( prop._tooltip.empty() == false )
				appendAttr( item, "tooltip", prop._tooltip );
			if ( prop._shaderKeyword.empty() == false )
				appendAttr( item, "shaderKeyword", prop._shaderKeyword );
			if ( prop._type == MaterialPropertyType::Range )
			{
				appendAttr( item, "min", to_string( prop._min ) );
				appendAttr( item, "max", to_string( prop._max ) );
			}
			if ( prop._type == MaterialPropertyType::Color )
			{
				appendBoolAttr( item, "bHdr", prop._bHdr );
				appendBoolAttr( item, "bSrgb", prop._bSrgb );
			}
			if ( isTextureType( prop._type ) )
				appendBoolAttr( item, "bSrgb", prop._bSrgb );
			if ( prop._bHidden )
				appendBoolAttr( item, "bHidden", true );
			if ( prop._bAdvanced )
				appendBoolAttr( item, "bAdvanced", true );

			if ( prop._listEnumEntries.empty() == false )
			{
				XmlNode list = item.appendChild( "_enumEntries" );
				for ( const MaterialEnumEntry& enumEntry : prop._listEnumEntries )
				{
					XmlNode eItem = list.appendChild( "item" );
					appendAttr( eItem, "name", enumEntry._name );
					appendAttr( eItem, "value", to_string( enumEntry._value ) );
				}
			}
		}

		appendPermutationNode( root, _desc._permutations );

		string out = doc.saveToString();
		return FileUtil::writeFile( absPath, reinterpret_cast<const uint8*>( out.data() ), out.size() );
	}

	bool Material::loadFromXml( string_view xmlText )
	{
		XmlDocument doc;
		if ( doc.parse( xmlText ) == false )
			return false;

		XmlNode root = doc.root( "MaterialDesc" );
		if ( root.isValid() == false )
			return false;

		if ( engine::getResourceManager().getAssetFormatRegistry().upgradeXml( AssetKind::Material, doc, root, AssetFormatVersions::kMaterial ) ==
			 false )
			return false;

		_desc			  = MaterialDesc{};
		_desc._name		  = fieldText( root, "name" );
		_desc._shaderPath = fieldText( root, "shaderPath" );
		_desc._blendMode  = fieldText( root, "blendMode" );

		XmlNode props = root.child( "_properties" );
		if ( props.isValid() )
		{
			for ( XmlNode item = props.child( "item" ); item; item = item.next( "item" ) )
			{
				MaterialProperty prop = parsePropertyNode( item );
				if ( prop._name.empty() == false )
					_desc._listProperties.push_back( std::move( prop ) );
			}
		}

		parsePermutationNode( root, _desc._permutations );

		applyDescToRuntime();
		return true;
	}

	void Material::applyDescToRuntime()
	{
		_data._listProperties = _desc._listProperties;
		_blendMode			  = parseBlendMode( _desc._blendMode );
		rebuildPackedBuffer();
	}

	void Material::syncDescFromRuntime() const
	{
		auto self					= const_cast<Material*>( this );
		self->_desc._listProperties = _data._listProperties;
		self->_desc._blendMode		= blendModeToString( _blendMode );
		self->_desc._name			= _desc._name;
		self->_desc._shaderPath		= _desc._shaderPath;
	}

} // namespace sw
