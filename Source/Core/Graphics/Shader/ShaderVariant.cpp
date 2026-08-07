/**
 * @file ShaderVariant.cpp
 * @brief 셰이더 변형 구현
 */
#include "pch.h"
#include "ShaderVariant.h"

namespace sw
{
	hashed_string ShaderVariantKey::getVariantHashKey() const
	{
		std::string combined;
		combined.reserve( 128 );
		combined += _shaderPath;

		std::vector<ShaderMacroDefine> sortedDefines = _defines;
		std::sort( sortedDefines.begin(), sortedDefines.end(),
				   []( const ShaderMacroDefine& a, const ShaderMacroDefine& b )
		{
			return a._name < b._name;
		} );

		for ( const ShaderMacroDefine& def : sortedDefines )
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

		std::unordered_map<hashed_string, ShaderCompileResult>::const_iterator iter = _variantCache.find( key );
		if ( iter != _variantCache.end() )
		{
			return &iter->second;
		}

		ShaderCompileDesc compileDesc;
		compileDesc._filePath	  = variantKey._shaderPath;
		compileDesc._entryPoint	  = "VSMain";
		compileDesc._stage		  = ShaderStage::Vertex;
		compileDesc._targetFormat = ShaderTargetFormat::DXBC_D3D11;

		ShaderCompileResult compileResult = ShaderCompiler::compileHLSL( compileDesc );
		auto [insertedIter, success]	  = _variantCache.try_emplace( key, std::move( compileResult ) );
		return &insertedIter->second;
	}

	void ShaderVariantManager::clear()
	{
		_variantCache.clear();
	}
} // namespace sw
