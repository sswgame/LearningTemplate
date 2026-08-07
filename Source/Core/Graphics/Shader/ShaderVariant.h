#pragma once
/**
 * @file ShaderVariant.h
 * @brief 셰이더 변형(키워드) 정의
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
		 * @brief VariantHashKey을(를) 반환합니다
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
		 * @brief 내부 상태를 비웁니다
		 */
		void clear();

	private:
		std::unordered_map<hashed_string, ShaderCompileResult> _variantCache;
	};
} // namespace sw
