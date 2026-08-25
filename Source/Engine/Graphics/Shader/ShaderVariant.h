/**
 * @file ShaderVariant.h
 * @brief 셰이더 변형(키워드) 정의
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "Engine/Graphics/Shader/ShaderCompiler.h"

namespace sw
{
	/// @brief 경로 + define 해시 permutation 키
	struct SW_API ShaderVariantKey
	{
		string					  _shaderPath;
		vector<ShaderMacroDefine> _listDefines;
		ShaderTargetFormat		  _targetFormat{ ShaderTargetFormat::Count };

		/** @brief 경로+define+타깃포맷 해시 permutation 키를 반환합니다. */
		hashed_string getVariantHashKey() const;
	};

	/// @brief permutation별 컴파일 결과 캐시
	class SW_API ShaderVariantManager
	{
	public:
		/** @brief 빈 permutation 캐시. */
		ShaderVariantManager() = default;

		/** @brief 복사를 금지합니다. */
		ShaderVariantManager( const ShaderVariantManager& ) = delete;
		/** @brief 대입을 금지합니다. */
		ShaderVariantManager& operator=( const ShaderVariantManager& ) = delete;
		/** @brief 캐시를 이동합니다. */
		ShaderVariantManager( ShaderVariantManager&& ) = default;
		/** @brief 이동 대입입니다. */
		ShaderVariantManager& operator=( ShaderVariantManager&& ) = default;

		/** @brief 캐시된 permutation을 반환하거나 없으면 컴파일합니다. */
		const ShaderCompileResult* getOrCompileVariant( const ShaderVariantKey& variantKey );

		/** @brief 크기를 반환합니다. */
		uint32 getCompiledVariantCount() const { return static_cast<uint32>( _mapVariantCache.size() ); }

		/**
		 * @brief 내부 상태를 비웁니다
		 */
		void clear();

	private:
		unordered_map<hashed_string, ShaderCompileResult> _mapVariantCache;
	};
} // namespace sw
