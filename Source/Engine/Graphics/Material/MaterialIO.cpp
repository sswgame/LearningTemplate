#include "pch.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialInternal.h"
#include "Engine/Utility/Resource/AssetFormat.h"
#include "Engine/Utility/Resource/ResourceManager.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
	SW_LOG_CALLER( "Material" );

	bool Material::loadFromFile( string_view assetRelativePath )
	{
		XmlDocument doc;
		if ( doc.loadPath( assetRelativePath ) == false )
			return false;
		return loadFromXml( doc.saveToString() );
	}

	TaskHandle Material::loadFromFileAsync( string_view assetRelativePath )
	{
		TaskHandle handle = engine::getTaskManager().emplaceTask(
			"LoadMaterialAsync",
			SW_DELEGATE_FUNCTION( TaskArgsDelegate, Material::loadFromFileAsyncJob ),
			MakeTaskArgs( _asyncLoadState, string( assetRelativePath ) ) );
		handle.submit();
		return handle;
	}

	void Material::loadFromFileAsyncJob( const TaskArgs& args )
	{
		shared_ptr<AsyncLoadState> state = args.get<shared_ptr<AsyncLoadState>>( 0 );
		if ( state == nullptr )
			return;

		std::scoped_lock<mutex> lock{ state->_mutex };
		if ( state->_pMaterial == nullptr )
			return;
		state->_pMaterial->loadFromFile( args.get<string>( 1 ) );
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

		for ( const MaterialProperty& prop : _data._listProperty )
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
				item.appendAttr( "min", prop._min );
				item.appendAttr( "max", prop._max );
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

			if ( prop._listEnumEntry.empty() == false )
			{
				XmlNode list = item.appendChild( "_enumEntries" );
				for ( const MaterialEnumEntry& enumEntry : prop._listEnumEntry )
				{
					XmlNode eItem = list.appendChild( "item" );
					appendAttr( eItem, "name", enumEntry._name );
					eItem.appendAttr( "value", enumEntry._value );
				}
			}
		}

		appendPermutationNode( root, _desc._permutations );

		return doc.saveFile( absPath );
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
					_desc._listProperty.push_back( std::move( prop ) );
			}
		}

		parsePermutationNode( root, _desc._permutations );

		applyDescToRuntime();
		return true;
	}

	void Material::applyDescToRuntime()
	{
		_data._listProperty = _desc._listProperty;
		_blendMode			= parseBlendMode( _desc._blendMode );
		rebuildPackedBuffer();
	}

	void Material::syncDescFromRuntime() const
	{
		auto self				  = const_cast<Material*>( this );
		self->_desc._listProperty = _data._listProperty;
		self->_desc._blendMode	  = blendModeToString( _blendMode );
		self->_desc._name		  = _desc._name;
		self->_desc._shaderPath	  = _desc._shaderPath;
	}

} // namespace sw
