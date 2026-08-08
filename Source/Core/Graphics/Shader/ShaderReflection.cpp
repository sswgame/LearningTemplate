/**
 * @file ShaderReflection.cpp
 * @brief 셰이더 리플렉션 파싱 (DX D3DReflect + SPIR-V 최소)
 */
#include "ShaderReflection.h"
#include "Core/Utility/Log/Logger.h"

#include <cstring>
#include <unordered_map>

namespace sw
{
	namespace
	{
		constexpr uint32 kSpirvMagic			   = 0x07230203u;
		constexpr uint32 kOpName			   = 5u;
		constexpr uint32 kOpDecorate		   = 71u;
		constexpr uint32 kOpVariable		   = 59u;
		constexpr uint32 kDecorationBinding	   = 33u;
		constexpr uint32 kDecorationDescriptorSet = 34u;
		constexpr uint32 kStorageClassUniform  = 2u;
		constexpr uint32 kStorageClassUniformConstant = 0u;
		constexpr uint32 kStorageClassStorageBuffer = 12u;

		ShaderReflectionData reflectSpirv( const std::vector<uint8>& bytecode )
		{
			ShaderReflectionData data{};
			if ( bytecode.size() < 20 || ( bytecode.size() % 4 ) != 0 )
			{
				SW_LOG_WARNING( "[ShaderReflection] SPIR-V bytecode size invalid." );
				return data;
			}

			const uint32* words		= reinterpret_cast<const uint32*>( bytecode.data() );
			const size_t  wordCount = bytecode.size() / 4;
			if ( words[0] != kSpirvMagic )
			{
				SW_LOG_WARNING( "[ShaderReflection] Not a SPIR-V module (bad magic)." );
				return data;
			}

			std::unordered_map<uint32, std::string> names;
			std::unordered_map<uint32, uint32>		bindings;
			std::unordered_map<uint32, uint32>		descriptorSets;
			struct VariableInfo
			{
				uint32 _storageClass = 0;
			};
			std::unordered_map<uint32, VariableInfo> variables;

			size_t offset = 5;
			while ( offset < wordCount )
			{
				const uint32 first	   = words[offset];
				const uint32 instrWords = first & 0xFFFFu;
				const uint32 opcode		= first >> 16;
				if ( instrWords == 0 || offset + instrWords > wordCount )
					break;

				if ( opcode == kOpName && instrWords >= 3 )
				{
					const uint32 target = words[offset + 1];
					const char*	 str	= reinterpret_cast<const char*>( &words[offset + 2] );
					const size_t maxLen = ( instrWords - 2 ) * 4;
					names[target]		= std::string( str, strnlen( str, maxLen ) );
				}
				else if ( opcode == kOpDecorate && instrWords >= 3 )
				{
					const uint32 target		= words[offset + 1];
					const uint32 decoration = words[offset + 2];
					if ( decoration == kDecorationBinding && instrWords >= 4 )
						bindings[target] = words[offset + 3];
					else if ( decoration == kDecorationDescriptorSet && instrWords >= 4 )
						descriptorSets[target] = words[offset + 3];
					(void)descriptorSets;
				}
				else if ( opcode == kOpVariable && instrWords >= 4 )
				{
					const uint32 resultId	  = words[offset + 2];
					const uint32 storageClass = words[offset + 3];
					variables[resultId]		  = VariableInfo{ storageClass };
				}

				offset += instrWords;
			}

			for ( const auto& [id, var] : variables )
			{
				const auto nameIt	 = names.find( id );
				const auto bindingIt = bindings.find( id );
				if ( bindingIt == bindings.end() )
					continue;

				const std::string name = ( nameIt != names.end() ) ? nameIt->second : ( "Resource_" + std::to_string( id ) );

				if ( var._storageClass == kStorageClassUniform )
				{
					ShaderBufferInfo buf{};
					buf._name	   = name;
					buf._bindPoint = bindingIt->second;
					buf._totalSize = 0;
					data._constantBuffers.push_back( buf );
				}

				ShaderResourceBinding res{};
				res._name	   = name;
				res._bindPoint = bindingIt->second;
				res._bindCount = 1;
				if ( var._storageClass == kStorageClassUniform )
					res._type = "ConstantBuffer";
				else if ( var._storageClass == kStorageClassStorageBuffer )
					res._type = "StorageBuffer";
				else if ( var._storageClass == kStorageClassUniformConstant )
					res._type = "TextureOrSampler";
				else
					res._type = "OtherResource";
				data._resources.push_back( res );
			}

			SW_LOG_INFO( "[ShaderReflection SPIR-V] ConstantBuffers: %# BoundResources: %#",
						 data._constantBuffers.size(), data._resources.size() );
			return data;
		}
	} // namespace

	ShaderReflectionData ShaderReflection::reflect( const std::vector<uint8>& bytecode, ShaderTargetFormat targetFormat )
	{
		ShaderReflectionData data{};

		if ( bytecode.empty() == true )
		{
			SW_LOG_WARNING( "ShaderReflection::reflect called with empty bytecode" );
			return data;
		}

		const bool bDxBytecode = ( targetFormat == ShaderTargetFormat::DXBC_D3D11 ||
								   targetFormat == ShaderTargetFormat::DXIL_D3D12 );
		const bool bSpirv = ( targetFormat == ShaderTargetFormat::SPIRV_Vulkan ||
							  targetFormat == ShaderTargetFormat::SPIRV_OpenGL );

#if defined( SW_PLATFORM_WINDOWS )
		if ( bDxBytecode )
		{
			Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection;
			HRESULT										   hr = D3DReflect(
				  bytecode.data(),
				  bytecode.size(),
				  IID_PPV_ARGS( reflection.GetAddressOf() ) );

			if ( SUCCEEDED( hr ) && reflection != nullptr )
			{
				D3D11_SHADER_DESC shaderDesc{};
				reflection->GetDesc( &shaderDesc );

				for ( UINT cbIndex = 0; cbIndex < shaderDesc.ConstantBuffers; ++cbIndex )
				{
					ID3D11ShaderReflectionConstantBuffer* cb = reflection->GetConstantBufferByIndex( cbIndex );
					if ( cb == nullptr )
						continue;

					D3D11_SHADER_BUFFER_DESC cbDesc{};
					cb->GetDesc( &cbDesc );

					ShaderBufferInfo bufInfo{};
					bufInfo._name	   = cbDesc.Name;
					bufInfo._bindPoint = cbIndex;
					bufInfo._totalSize = cbDesc.Size;

					for ( UINT varIndex = 0; varIndex < cbDesc.Variables; ++varIndex )
					{
						ID3D11ShaderReflectionVariable* var = cb->GetVariableByIndex( varIndex );
						if ( var == nullptr )
							continue;

						D3D11_SHADER_VARIABLE_DESC varDesc{};
						var->GetDesc( &varDesc );

						ShaderVariableInfo varInfo{};
						varInfo._name	= varDesc.Name;
						varInfo._offset = varDesc.StartOffset;
						varInfo._size	= varDesc.Size;

						bufInfo._variables.push_back( varInfo );
					}

					data._constantBuffers.push_back( bufInfo );
				}

				for ( UINT resourceIndex = 0; resourceIndex < shaderDesc.BoundResources; ++resourceIndex )
				{
					D3D11_SHADER_INPUT_BIND_DESC bindDesc{};
					reflection->GetResourceBindingDesc( resourceIndex, &bindDesc );

					ShaderResourceBinding resBinding{};
					resBinding._name	  = bindDesc.Name;
					resBinding._bindPoint = bindDesc.BindPoint;
					resBinding._bindCount = bindDesc.BindCount;

					switch ( static_cast<uint32>( bindDesc.Type ) )
					{
						case D3D_SIT_TEXTURE:
							resBinding._type = "Texture";
							break;
						case D3D_SIT_SAMPLER:
							resBinding._type = "Sampler";
							break;
						case D3D_SIT_CBUFFER:
							resBinding._type = "ConstantBuffer";
							break;
						default:
							resBinding._type = "OtherResource";
							break;
					}

					data._resources.push_back( resBinding );
				}

				SW_LOG_INFO( "[ShaderReflection Success] ConstantBuffers: %# BoundResources: %#",
							 data._constantBuffers.size(), data._resources.size() );
				return data;
			}

			SW_LOG_ERROR( "[ShaderReflection] D3DReflect failed for DX bytecode." );
			return data;
		}
#else
		(void)bDxBytecode;
#endif

		if ( bSpirv )
			return reflectSpirv( bytecode );

		SW_LOG_WARNING( "[ShaderReflection] Reflection unsupported for target format %# — returning empty result.",
						static_cast<uint32>( targetFormat ) );
		return data;
	}
} // namespace sw
