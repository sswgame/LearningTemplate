#pragma once
/**
 * @file ShaderReflection.h
 * @brief Auto-generated documentation header
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
		 * @brief reflect 처리를 수행합니다.
		 */
		static ShaderReflectionData reflect( const std::vector<uint8>& bytecode, ShaderTargetFormat targetFormat );
	};
}
