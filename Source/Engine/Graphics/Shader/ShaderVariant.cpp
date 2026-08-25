#include "pch.h"

#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/Shader/ShaderVariant.h"

#include "Core/String/StringUtil.h"

namespace sw
{
	hashed_string ShaderVariantKey::getVariantHashKey() const
	{
		string combined;
		combined.reserve( 128 );
		combined += _shaderPath;
		combined += ";";
		const ShaderTargetFormat targetFmt = ( _targetFormat == ShaderTargetFormat::Count ) ? RHI::getShaderTargetFormat( gv_rhiBackend ) : _targetFormat;
		combined += to_string( static_cast<uint32>( targetFmt ) );

		vector<ShaderMacroDefine> listSortedDefines = _listDefines;
		std::sort( listSortedDefines.begin(), listSortedDefines.end(),
				   []( const ShaderMacroDefine& defA, const ShaderMacroDefine& defB )
		{ return defA._name < defB._name; } );

		for ( const ShaderMacroDefine& def : listSortedDefines )
		{
			combined += ";";
			combined += def._name;
			combined += "=";
			combined += def._value;
		}

		return hashed_string( combined.c_str() );
	}

	const ShaderCompileResult* ShaderVariantManager::getOrCompileVariant( const ShaderVariantKey& variantKey )
	{
		hashed_string key = variantKey.getVariantHashKey();

		unordered_map<hashed_string, ShaderCompileResult>::const_iterator iter = _mapVariantCache.find( key );
		if ( iter != _mapVariantCache.end() )
			return &iter->second;

		ShaderCompileDesc compileDesc;
		compileDesc._filePath	  = variantKey._shaderPath;
		compileDesc._entryPoint	  = "VSMain";
		compileDesc._stage		  = ShaderStage::Vertex;
		compileDesc._targetFormat = ( variantKey._targetFormat == ShaderTargetFormat::Count ) ? RHI::getShaderTargetFormat( gv_rhiBackend ) : variantKey._targetFormat;
		compileDesc._listDefines  = variantKey._listDefines;

		ShaderCompileResult compileResult = ShaderCompiler::compileHLSL( compileDesc );
		auto [insertedIter, success]	  = _mapVariantCache.try_emplace( key, std::move( compileResult ) );
		return &insertedIter->second;
	}

	void ShaderVariantManager::clear()
	{
		_mapVariantCache.clear();
	}
} // namespace sw
