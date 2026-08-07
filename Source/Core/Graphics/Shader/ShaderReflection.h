#pragma once
/**
 * @file ShaderReflection.h
 * @brief 셰이더 바이트코드 리플렉션 데이터
 */

#include "Core/CoreMinimal.h"
#include "Core/Graphics/Shader/ShaderCompiler.h"

namespace sw
{
	struct ShaderVariableInfo
	{
		std::string _name;
		uint32		_offset = 0;
		uint32		_size	= 0;
	};

	struct ShaderBufferInfo
	{
		std::string						_name;
		uint32							_bindPoint = 0;
		uint32							_totalSize = 0;
		std::vector<ShaderVariableInfo> _variables;
	};

	struct ShaderResourceBinding
	{
		std::string _name;
		uint32		_bindPoint = 0;
		uint32		_bindCount = 0;
		std::string _type;
	};

	struct ShaderReflectionData
	{
		std::vector<ShaderBufferInfo>	   _constantBuffers;
		std::vector<ShaderResourceBinding> _resources;
	};

	class SW_API ShaderReflection
	{
	public:
		/**
		 * @brief 바이트코드에서 리플렉션 데이터를 추출합니다
		 */
		static ShaderReflectionData reflect( const std::vector<uint8>& bytecode, ShaderTargetFormat targetFormat );
	};
}
