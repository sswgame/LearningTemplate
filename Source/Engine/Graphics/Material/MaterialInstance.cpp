#include "pch.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialInternal.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Graphics/Shader/ShaderReflection.h"
#include "Engine/Utility/Resource/AssetFormat.h"
#include "Engine/Utility/Resource/ResourceManager.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
	namespace
	{
		template <typename T>
		void insertOrAssign( vector<std::pair<hashed_string, T>>& listPairs, hashed_string key, const T& val )
		{
			for ( auto& pair : listPairs )
			{
				if ( pair.first == key )
				{
					pair.second = val;
					return;
				}
			}
			listPairs.push_back( { key, val } );
		}

		template <typename T>
		const T* findValue( const vector<std::pair<hashed_string, T>>& listPairs, hashed_string key )
		{
			for ( const auto& pair : listPairs )
			{
				if ( pair.first == key )
					return &pair.second;
			}
			return nullptr;
		}

	} // namespace


	MaterialInstance::MaterialInstance()
		: _pParentMaterial{ nullptr }
		, _desc{}
		, _listValueOverrides{}
		, _listScalarOverrides{}
		, _listVectorOverrides{}
		, _listTextureOverrides{}
		, _listKeywordOverrides{}
		, _listMultiCompileOverrides{}
		, _qualityOverride{ MaterialQualityLevel::Count }
		, _listBuffer{}
		, _constantBuffer{ 0 }
		, _descriptorIndex{ kInvalidDescriptorIndex }
		, _pRHIDevice{ nullptr }
		, _listCachedDefines{}
		, _cachedPermutationHash{ 0 }
		, _parentPermutationHash{ 0 }
		, _bDefinesDirty{ 1 }
		, _bGpuDirty{ 1 }
		, _instReserved{ 0 } {}

	MaterialInstance::MaterialInstance( Material* pParentMaterial )
		: _pParentMaterial{ pParentMaterial }
		, _desc{}
		, _listValueOverrides{}
		, _listScalarOverrides{}
		, _listVectorOverrides{}
		, _listTextureOverrides{}
		, _listKeywordOverrides{}
		, _listMultiCompileOverrides{}
		, _qualityOverride{ MaterialQualityLevel::Count }
		, _listBuffer{}
		, _constantBuffer{ 0 }
		, _descriptorIndex{ kInvalidDescriptorIndex }
		, _pRHIDevice{ nullptr }
		, _listCachedDefines{}
		, _cachedPermutationHash{ 0 }
		, _parentPermutationHash{ 0 }
		, _bDefinesDirty{ 1 }
		, _bGpuDirty{ 1 }
		, _instReserved{ 0 } {}

	MaterialInstance::~MaterialInstance()
	{
		if ( _pRHIDevice != nullptr )
			shutdown( _pRHIDevice );
	}

	void MaterialInstance::shutdown( IRHIDevice* pRhi )
	{
		if ( pRhi != nullptr )
		{
			if ( _descriptorIndex != kInvalidDescriptorIndex )
				pRhi->getResource()->unregisterBindlessResource( _descriptorIndex );
			if ( _constantBuffer != 0 )
				pRhi->getResource()->destroyBuffer( _constantBuffer );
		}
		_constantBuffer	 = 0;
		_descriptorIndex = kInvalidDescriptorIndex;
		_pRHIDevice		 = nullptr;
		_listBuffer.clear();
		_bGpuDirty = 1;
	}

	bool MaterialInstance::loadFromFile( string_view assetRelativePath )
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
		return loadFromXml( text );
	}

	bool MaterialInstance::saveToFile( string_view assetRelativePath ) const
	{
		string absPath = ResourceUtil::getResourcePath( assetRelativePath );
		if ( absPath.empty() )
			absPath = assetRelativePath;

		syncDescOverrides();

		XmlDocument doc;
		XmlNode		root = doc.appendRoot( "MaterialInstanceDesc" );
		engine::getResourceManager().getAssetFormatRegistry().writeXmlVersion( root, AssetFormatVersions::kMaterialInstance );
		appendAttr( root, "name", _desc._name );
		if ( _desc._parentPath.empty() == false )
			appendAttr( root, "parentPath", _desc._parentPath );
		if ( _desc._quality.empty() == false )
			appendAttr( root, "quality", _desc._quality );

		XmlNode overrides = root.appendChild( "_overrides" );
		for ( const MaterialInstanceDesc::Override& overrideItem : _desc._listOverrides )
		{
			XmlNode item = overrides.appendChild( "item" );
			appendAttr( item, "name", overrideItem._name );
			appendAttr( item, "value", overrideItem._value );
			if ( overrideItem._assetPath.empty() == false )
				appendAttr( item, "assetPath", overrideItem._assetPath );
		}

		if ( _desc._listKeywords.empty() == false )
		{
			XmlNode list = root.appendChild( "_keywords" );
			for ( const MaterialInstanceDesc::KeywordOverride& keywordItem : _desc._listKeywords )
			{
				XmlNode item = list.appendChild( "item" );
				appendAttr( item, "name", keywordItem._name );
				appendBoolAttr( item, "bEnabled", keywordItem._bEnabled );
			}
		}
		if ( _desc._listMultiCompiles.empty() == false )
		{
			XmlNode list = root.appendChild( "_multiCompiles" );
			for ( const MaterialInstanceDesc::MultiCompileOverride& multiCompileItem : _desc._listMultiCompiles )
			{
				XmlNode item = list.appendChild( "item" );
				appendAttr( item, "name", multiCompileItem._name );
				appendAttr( item, "selected", multiCompileItem._selected );
			}
		}

		string out = doc.saveToString();
		return FileUtil::writeFile( absPath, reinterpret_cast<const uint8*>( out.data() ), out.size() );
	}

	bool MaterialInstance::applyToGpu( IRHIDevice* pRhi )
	{
		if ( pRhi == nullptr || _pParentMaterial == nullptr )
			return false;

		_pRHIDevice = pRhi;

		if ( _bGpuDirty == 0 && _constantBuffer != 0 && _descriptorIndex != kInvalidDescriptorIndex )
			return true;

		_listBuffer = _pParentMaterial->getBuffer();
		if ( _listBuffer.empty() )
			return false;

		for ( const auto& [name, value] : _listValueOverrides )
		{
			_pParentMaterial->packNamedValueIntoBuffer( name, value, _listBuffer );
		}

		for ( const auto& [name, idx] : _listTextureOverrides )
		{
			_pParentMaterial->packTextureIntoBuffer( name, idx, _listBuffer );
		}

		const uint32 size = static_cast<uint32>( _listBuffer.size() );
		if ( _constantBuffer == 0 )
		{
			_constantBuffer = pRhi->getResource()->createConstantBuffer( size );
			if ( _constantBuffer == 0 )
				return false;
			_descriptorIndex = pRhi->getResource()->registerBindlessResource( _constantBuffer );
		}
		pRhi->getResource()->updateConstantBuffer( _constantBuffer, _listBuffer.data(), size );
		_bGpuDirty = 0;
		return _descriptorIndex != kInvalidDescriptorIndex;
	}

	void MaterialInstance::clearOverrides()
	{
		_listValueOverrides.clear();
		_listScalarOverrides.clear();
		_listVectorOverrides.clear();
		_listTextureOverrides.clear();
		_listKeywordOverrides.clear();
		_listMultiCompileOverrides.clear();
		_qualityOverride = MaterialQualityLevel::Count;
		_bDefinesDirty	 = 1;
		_bGpuDirty		 = 1;
	}

	void MaterialInstance::enableKeyword( hashed_string keyword )
	{
		insertOrAssign( _listKeywordOverrides, keyword, true );
		_bDefinesDirty = 1;
		_bGpuDirty	   = 1;
	}

	void MaterialInstance::disableKeyword( hashed_string keyword )
	{
		insertOrAssign( _listKeywordOverrides, keyword, false );
		_bDefinesDirty = 1;
		_bGpuDirty	   = 1;
	}

	void MaterialInstance::setParent( Material* parentMaterial )
	{
		_pParentMaterial = parentMaterial;
		_bDefinesDirty	 = 1;
		_bGpuDirty		 = 1;
	}

	void MaterialInstance::setParameter( hashed_string name, string_view value )
	{
		insertOrAssign( _listValueOverrides, name, string( value ) );
		_bGpuDirty = 1;
	}

	void MaterialInstance::setScalarParameter( hashed_string name, float32 value )
	{
		insertOrAssign( _listScalarOverrides, name, value );
		insertOrAssign( _listValueOverrides, name, to_string( value ) );
		_bGpuDirty = 1;
	}

	void MaterialInstance::setVectorParameter( hashed_string name, const float32 color[4] )
	{
		if ( color == nullptr )
			return;
		array<float32, 4> val = { color[0], color[1], color[2], color[3] };
		insertOrAssign( _listVectorOverrides, name, val );
		StringBuilder<64> sb;
		sb.append( color[0] ).append( ' ' ).append( color[1] ).append( ' ' ).append( color[2] ).append( ' ' ).append( color[3] );
		insertOrAssign( _listValueOverrides, name, string{ sb.c_str(), sb.size() } );
		_bGpuDirty = 1;
	}

	void MaterialInstance::setTextureParameter( hashed_string name, RHIDescriptorIndex descIdx )
	{
		insertOrAssign( _listTextureOverrides, name, descIdx );
		insertOrAssign( _listValueOverrides, name, to_string( descIdx ) );
		_bGpuDirty = 1;
	}

	void MaterialInstance::setQualityLevel( MaterialQualityLevel level )
	{
		if ( _qualityOverride != level )
		{
			_qualityOverride = level;
			_bDefinesDirty	 = 1;
			_bGpuDirty		 = 1;
		}
	}

	void MaterialInstance::setMultiCompile( hashed_string name, string_view selectedOption )
	{
		insertOrAssign( _listMultiCompileOverrides, name, string( selectedOption ) );
		_bDefinesDirty = 1;
		_bGpuDirty	   = 1;
	}

	bool MaterialInstance::getParameter( hashed_string name, string& outValue ) const
	{
		const string* val = findValue( _listValueOverrides, name );
		if ( val != nullptr )
		{
			outValue = *val;
			return true;
		}
		if ( _pParentMaterial != nullptr )
		{
			const MaterialProperty* prop = _pParentMaterial->findProperty( name );
			if ( prop != nullptr )
			{
				outValue = prop->_value.empty() == false ? prop->_value : prop->_defaultValue;
				return true;
			}
		}
		return false;
	}

	float32 MaterialInstance::getScalarParameter( hashed_string name, float32 defaultValue ) const
	{
		const float32* pVal = findValue( _listScalarOverrides, name );
		if ( pVal != nullptr )
			return *pVal;
		if ( _pParentMaterial != nullptr )
		{
			float32 v = defaultValue;
			if ( _pParentMaterial->getParameterFloat( name, v ) )
				return v;
		}
		return defaultValue;
	}

	const float32* MaterialInstance::getVectorParameter( hashed_string name ) const
	{
		const array<float32, 4>* pVal = findValue( _listVectorOverrides, name );
		if ( pVal != nullptr )
			return pVal->data();
		if ( _pParentMaterial != nullptr )
		{
			const void* pData = _pParentMaterial->getPropertyData( name.c_str() ? name.c_str() : "" );
			return pData ? reinterpret_cast<const float32*>( pData ) : nullptr;
		}
		return nullptr;
	}

	RHIDescriptorIndex MaterialInstance::getTextureParameter( hashed_string name ) const
	{
		const RHIDescriptorIndex* pVal = findValue( _listTextureOverrides, name );
		if ( pVal != nullptr )
			return *pVal;
		if ( _pParentMaterial != nullptr )
		{
			const MaterialProperty* pProp = _pParentMaterial->findProperty( name );
			if ( pProp != nullptr )
				return pProp->_textureIndex;
			return _pParentMaterial->getDescriptorIndex();
		}
		return kInvalidDescriptorIndex;
	}

	bool MaterialInstance::isKeywordEnabled( hashed_string keyword ) const
	{
		const bool* pVal = findValue( _listKeywordOverrides, keyword );
		if ( pVal != nullptr )
			return *pVal;
		if ( _pParentMaterial == nullptr )
			return false;
		const vector<string>& defs = _pParentMaterial->getCachedShaderDefines();
		const utf8*			  pKey = keyword.c_str();
		if ( pKey == nullptr )
			return false;
		for ( const string& defineStr : defs )
		{
			if ( defineStr == pKey )
				return true;
		}
		return false;
	}

	const vector<string>& MaterialInstance::getCachedShaderDefines() const
	{
		uint64 currentParentHash = _pParentMaterial != nullptr ? _pParentMaterial->getPermutationHash() : 0;
		if ( _parentPermutationHash != currentParentHash )
		{
			_parentPermutationHash = currentParentHash;
			_bDefinesDirty		   = 1;
		}

		if ( _bDefinesDirty )
		{
			_listCachedDefines.clear();
			if ( _pParentMaterial != nullptr )
				_listCachedDefines = _pParentMaterial->getCachedShaderDefines();

			if ( _qualityOverride != MaterialQualityLevel::Count )
			{
				_listCachedDefines.erase( std::remove_if( _listCachedDefines.begin(), _listCachedDefines.end(),
														  []( string_view defineStr )
				{ return defineStr.rfind( "MATERIAL_QUALITY", 0 ) == 0; } ),
										  _listCachedDefines.end() );
				appendQualityDefines( _qualityOverride, _listCachedDefines );
			}

			for ( const auto& [name, selected] : _listMultiCompileOverrides )
			{
				if ( _pParentMaterial != nullptr )
				{
					for ( const MaterialMultiCompile& mc : _pParentMaterial->getPermutations()._listMultiCompiles )
					{
						if ( hashed_string( mc._name.c_str() ) != name )
							continue;
						for ( const string& opt : mc._listOptions )
						{
							_listCachedDefines.erase( std::remove( _listCachedDefines.begin(), _listCachedDefines.end(), opt ), _listCachedDefines.end() );
						}
						break;
					}
				}
				appendUniqueDefine( _listCachedDefines, selected );
			}

			for ( const auto& [keyword, enabled] : _listKeywordOverrides )
			{
				const utf8* pKey = keyword.c_str();
				if ( pKey == nullptr )
					continue;
				_listCachedDefines.erase( std::remove( _listCachedDefines.begin(), _listCachedDefines.end(), string( pKey ) ), _listCachedDefines.end() );
				if ( enabled )
					appendUniqueDefine( _listCachedDefines, pKey );
			}

			std::sort( _listCachedDefines.begin(), _listCachedDefines.end() );
			_cachedPermutationHash = hashDefines( _listCachedDefines );
			_bDefinesDirty		   = 0;
		}
		return _listCachedDefines;
	}

	uint64 MaterialInstance::getPermutationHash() const
	{
		if ( _bDefinesDirty )
			getCachedShaderDefines();
		return _cachedPermutationHash;
	}

	RHIDescriptorIndex MaterialInstance::getDescriptorIndex() const
	{
		if ( _descriptorIndex != kInvalidDescriptorIndex )
			return _descriptorIndex;
		if ( _pParentMaterial != nullptr )
			return _pParentMaterial->getDescriptorIndex();
		return kInvalidDescriptorIndex;
	}

	bool MaterialInstance::isParameterOverridden( hashed_string name ) const
	{
		if ( findValue( _listValueOverrides, name ) != nullptr )
			return true;
		if ( findValue( _listScalarOverrides, name ) != nullptr )
			return true;
		if ( findValue( _listVectorOverrides, name ) != nullptr )
			return true;
		if ( findValue( _listTextureOverrides, name ) != nullptr )
			return true;
		if ( findValue( _listKeywordOverrides, name ) != nullptr )
			return true;
		if ( findValue( _listMultiCompileOverrides, name ) != nullptr )
			return true;
		return false;
	}

	bool MaterialInstance::validateParametersWithReflection( const ShaderReflectionData& reflectionData ) const
	{
		auto checkParam = [&]( hashed_string paramName ) -> bool
		{
			for ( const ShaderBufferInfo& cb : reflectionData._listConstantBuffers )
			{
				for ( const ShaderVariableInfo& var : cb._listVariables )
				{
					if ( hashed_string( var._name.c_str() ) == paramName )
						return true;
				}
			}
			for ( const ShaderResourceBinding& res : reflectionData._listResources )
			{
				if ( hashed_string( res._name.c_str() ) == paramName )
					return true;
			}
			return false;
		};

		for ( const auto& [name, val] : _listValueOverrides )
		{
			(void)val;
			if ( checkParam( name ) == false )
				return false;
		}
		return true;
	}

	void MaterialInstance::syncDescOverrides() const
	{
		auto self = const_cast<MaterialInstance*>( this );
		self->_desc._listOverrides.clear();
		for ( const auto& [name, value] : _listValueOverrides )
		{
			MaterialInstanceDesc::Override o{};
			o._name	 = name.c_str() ? name.c_str() : "";
			o._value = value;
			self->_desc._listOverrides.push_back( std::move( o ) );
		}
		self->_desc._listKeywords.clear();
		for ( const auto& [name, enabled] : _listKeywordOverrides )
		{
			MaterialInstanceDesc::KeywordOverride k{};
			k._name		= name.c_str() ? name.c_str() : "";
			k._bEnabled = enabled;
			self->_desc._listKeywords.push_back( std::move( k ) );
		}
		self->_desc._listMultiCompiles.clear();
		for ( const auto& [name, selected] : _listMultiCompileOverrides )
		{
			MaterialInstanceDesc::MultiCompileOverride m{};
			m._name		= name.c_str() ? name.c_str() : "";
			m._selected = selected;
			self->_desc._listMultiCompiles.push_back( std::move( m ) );
		}
		if ( _qualityOverride != MaterialQualityLevel::Count )
			self->_desc._quality = qualityToString( _qualityOverride );
		else
			self->_desc._quality.clear();
		if ( _pParentMaterial != nullptr )
			self->_desc._parentPath.clear(); // runtime parent; path filled by caller if desired
	}

	bool MaterialInstance::loadFromXml( string_view xmlText )
	{
		XmlDocument doc;
		if ( doc.parse( xmlText ) == false )
			return false;

		XmlNode root = doc.root( "MaterialInstanceDesc" );
		if ( root.isValid() == false )
			return false;

		if ( engine::getResourceManager().getAssetFormatRegistry().upgradeXml( AssetKind::MaterialInstance, doc, root,
																			   AssetFormatVersions::kMaterialInstance ) == false )
			return false;

		_desc			  = MaterialInstanceDesc{};
		_desc._name		  = fieldText( root, "name" );
		_desc._parentPath = fieldText( root, "parentPath" );
		_desc._quality	  = fieldText( root, "quality" );

		XmlNode overrides = root.child( "_overrides" );
		if ( overrides.isValid() )
		{
			for ( XmlNode item = overrides.child( "item" ); item; item = item.next( "item" ) )
			{
				MaterialInstanceDesc::Override o{};
				o._name		 = fieldText( item, "name" );
				o._value	 = fieldText( item, "value" );
				o._assetPath = fieldText( item, "assetPath" );
				if ( o._name.empty() == false )
					_desc._listOverrides.push_back( std::move( o ) );
			}
		}
		XmlNode keywords = root.child( "_keywords" );
		if ( keywords.isValid() )
		{
			for ( XmlNode item = keywords.child( "item" ); item; item = item.next( "item" ) )
			{
				MaterialInstanceDesc::KeywordOverride k{};
				k._name		= fieldText( item, "name" );
				k._bEnabled = parseBoolField( item, "bEnabled", true );
				if ( k._name.empty() == false )
					_desc._listKeywords.push_back( std::move( k ) );
			}
		}
		XmlNode mcs = root.child( "_multiCompiles" );
		if ( mcs.isValid() )
		{
			for ( XmlNode item = mcs.child( "item" ); item; item = item.next( "item" ) )
			{
				MaterialInstanceDesc::MultiCompileOverride m{};
				m._name		= fieldText( item, "name" );
				m._selected = fieldText( item, "selected" );
				if ( m._name.empty() == false )
					_desc._listMultiCompiles.push_back( std::move( m ) );
			}
		}

		_listValueOverrides.clear();
		_listKeywordOverrides.clear();
		_listMultiCompileOverrides.clear();
		auto insertOrAssign = []( auto& vec, hashed_string key, const auto& val )
		{
			for ( auto& pair : vec )
			{
				if ( pair.first == key )
				{
					pair.second = val;
					return;
				}
			}
			vec.push_back( { key, val } );
		};

		for ( const MaterialInstanceDesc::Override& overrideItem : _desc._listOverrides )
		{
			insertOrAssign( _listValueOverrides, hashed_string( overrideItem._name.c_str() ), overrideItem._value );
		}
		for ( const MaterialInstanceDesc::KeywordOverride& keywordItem : _desc._listKeywords )
		{
			insertOrAssign( _listKeywordOverrides, hashed_string( keywordItem._name.c_str() ), keywordItem._bEnabled );
		}
		for ( const MaterialInstanceDesc::MultiCompileOverride& multiCompileItem : _desc._listMultiCompiles )
		{
			insertOrAssign( _listMultiCompileOverrides, hashed_string( multiCompileItem._name.c_str() ), multiCompileItem._selected );
		}
		if ( _desc._quality.empty() == false )
			_qualityOverride = parseQuality( _desc._quality );
		_bGpuDirty = 1;
		return true;
	}

} // namespace sw
