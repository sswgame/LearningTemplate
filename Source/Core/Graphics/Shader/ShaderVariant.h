#pragma once
/**
 * @file ShaderVariant.h
 * @brief Auto-generated documentation header
 */

#include "Core/Common/Types.h"
#include "Core/Common/CommonHeaders.h"

#include "Core/Graphics/Shader/ShaderCompiler.h"

namespace sw
{
	struct ShaderMacroDefine
	{
		std::string _name;
		std::string _value;
	};

	struct ShaderVariantKey
	{
		std::string					   _shaderPath;
		std::vector<ShaderMacroDefine> _defines;

		/**
		 * @brief getVariantHashKey 처리를 수행합니다.
		 */
		hashed_string getVariantHashKey() const;
	};

	class SW_API ShaderVariantManager
	{
	public:
		ShaderVariantManager() = default;

		ShaderVariantManager( const ShaderVariantManager& )			   = delete;
		ShaderVariantManager& operator=( const ShaderVariantManager& ) = delete;
		ShaderVariantManager( ShaderVariantManager&& )				   = default;
		ShaderVariantManager& operator=( ShaderVariantManager&& )	   = default;

		const ShaderCompileResult* getOrCompileVariant( const ShaderVariantKey& variantKey );

		uint32 getCompiledVariantCount() const { return static_cast<uint32>( _variantCache.size() ); }

		/**
		 * @brief clear 처리를 수행합니다.
		 */
		void clear();

	private:
		std::unordered_map<hashed_string, ShaderCompileResult> _variantCache;
	};
}
