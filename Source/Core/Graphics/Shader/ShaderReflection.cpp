/**
 * @file ShaderReflection.cpp
 * @brief 셰이더 리플렉션 파싱
 */
#include "ShaderReflection.h"
#include "Core/Utility/Log/Logger.h"

#if defined( SW_PLATFORM_WINDOWS )
	#include <d3dcompiler.h>
	#include <d3d11shader.h>
	#include <wrl/client.h>
#endif

namespace sw
{

	ShaderReflectionData ShaderReflection::reflect( const std::vector<uint8>& bytecode, ShaderTargetFormat targetFormat )
	{
		(void)targetFormat;
		ShaderReflectionData data{};

		if ( bytecode.empty() == true )
		{
			SW_LOG_WARNING( "ShaderReflection::reflect called with empty bytecode" );
			return data;
		}

		const bool bDxBytecode = ( targetFormat == ShaderTargetFormat::DXBC_D3D11 ||
								   targetFormat == ShaderTargetFormat::DXIL_D3D12 );

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

			SW_LOG_ERROR( "[ShaderReflection] D3DReflect failed for DX bytecode (HRESULT may be unavailable)." );
			return data;
		}
#else
		(void)bDxBytecode;
#endif

		// SPIR-V / non-DX: do not fabricate TransformBuffer metadata.
		SW_LOG_ERROR( "[ShaderReflection] Reflection unsupported for target format %# — returning empty result.",
					  static_cast<uint32>( targetFormat ) );
		return data;
	}
} // namespace sw
