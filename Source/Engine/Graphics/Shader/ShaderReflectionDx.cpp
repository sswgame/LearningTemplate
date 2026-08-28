#include "pch.h"

#include "Engine/Graphics/Shader/ShaderReflectionInternal.h"

#if defined( SW_PLATFORM_WINDOWS )
namespace sw::shader_reflection_detail
{
	SW_LOG_CALLER( "ShaderReflection" );

	namespace
	{

		const utf8* resourceTypeName( uint32 sit )
		{
			switch ( sit )
			{
				case static_cast<uint32>( D3D_SIT_TEXTURE ):
					return "Texture";
				case static_cast<uint32>( D3D_SIT_SAMPLER ):
					return "Sampler";
				case static_cast<uint32>( D3D_SIT_CBUFFER ):
					return "ConstantBuffer";
				case static_cast<uint32>( D3D_SIT_UAV_RWTYPED ):
				case static_cast<uint32>( D3D_SIT_UAV_RWSTRUCTURED ):
				case static_cast<uint32>( D3D_SIT_UAV_RWBYTEADDRESS ):
					return "UAV";
				default:
					return "OtherResource";
			}
		}

		string hlslVariableTypeName( D3D_SHADER_VARIABLE_CLASS varClass, D3D_SHADER_VARIABLE_TYPE varType,
									 uint32 rows, uint32 columns )
		{
			const utf8* pBase = "Float";
			switch ( static_cast<uint32>( varType ) )
			{
				case static_cast<uint32>( D3D_SVT_FLOAT ):
					pBase = "Float";
					break;
				case static_cast<uint32>( D3D_SVT_INT ):
					pBase = "Int";
					break;
				case static_cast<uint32>( D3D_SVT_UINT ):
				case static_cast<uint32>( D3D_SVT_UINT8 ):
					pBase = "Uint";
					break;
				case static_cast<uint32>( D3D_SVT_BOOL ):
					return "Bool";
				default:
					break;
			}

			if ( varClass == D3D_SVC_MATRIX_ROWS || varClass == D3D_SVC_MATRIX_COLUMNS )
			{
				if ( rows == 4 && columns == 4 && varType == D3D_SVT_FLOAT )
					return string( "Float4x4" );
				return string( pBase ) + to_string( rows ) + "x" + to_string( columns );
			}
			if ( varClass == D3D_SVC_VECTOR )
			{
				if ( columns <= 1 )
					return string( pBase );
				return string( pBase ) + to_string( columns );
			}
			return string( pBase );
		}

		ShaderReflectionData fillFromId3d11Reflection( ID3D11ShaderReflection* pReflection )
		{
			ShaderReflectionData data{};
			D3D11_SHADER_DESC	 shaderDesc{};
			pReflection->GetDesc( &shaderDesc );

			for ( UINT cbIndex = 0; cbIndex < shaderDesc.ConstantBuffers; ++cbIndex )
			{
				ID3D11ShaderReflectionConstantBuffer* pCb = pReflection->GetConstantBufferByIndex( cbIndex );
				if ( pCb == nullptr )
					continue;

				D3D11_SHADER_BUFFER_DESC cbDesc{};
				pCb->GetDesc( &cbDesc );

				ShaderBufferInfo bufInfo{};
				bufInfo._name		   = cbDesc.Name != nullptr ? cbDesc.Name : "";
				bufInfo._registerSpace = 0;
				bufInfo._bindPoint	   = cbIndex;
				bufInfo._totalSize	   = cbDesc.Size;

				for ( UINT varIndex = 0; varIndex < cbDesc.Variables; ++varIndex )
				{
					ID3D11ShaderReflectionVariable* pVar = pCb->GetVariableByIndex( varIndex );
					if ( pVar == nullptr )
						continue;

					D3D11_SHADER_VARIABLE_DESC varDesc{};
					pVar->GetDesc( &varDesc );

					ShaderVariableInfo varInfo{};
					varInfo._name					= varDesc.Name != nullptr ? varDesc.Name : "";
					varInfo._offset					= varDesc.StartOffset;
					varInfo._size					= varDesc.Size;
					ID3D11ShaderReflectionType* pTy = pVar->GetType();
					if ( pTy != nullptr )
					{
						D3D11_SHADER_TYPE_DESC typeDesc{};
						pTy->GetDesc( &typeDesc );
						varInfo._type = hlslVariableTypeName( typeDesc.Class, typeDesc.Type, typeDesc.Rows, typeDesc.Columns );
					}
					bufInfo._listVariable.push_back( varInfo );
				}

				data._listConstantBuffer.push_back( std::move( bufInfo ) );
			}

			for ( UINT resourceIndex = 0; resourceIndex < shaderDesc.BoundResources; ++resourceIndex )
			{
				D3D11_SHADER_INPUT_BIND_DESC bindDesc{};
				pReflection->GetResourceBindingDesc( resourceIndex, &bindDesc );

				ShaderResourceBinding resBinding{};
				resBinding._name		  = bindDesc.Name != nullptr ? bindDesc.Name : "";
				resBinding._registerSpace = 0;
				resBinding._bindPoint	  = bindDesc.BindPoint;
				resBinding._bindCount	  = bindDesc.BindCount;
				resBinding._type		  = resourceTypeName( static_cast<uint32>( bindDesc.Type ) );
				data._listResource.push_back( std::move( resBinding ) );
			}

			return data;
		}

		ShaderReflectionData fillFromId3d12Reflection( ID3D12ShaderReflection* pReflection )
		{
			ShaderReflectionData data{};
			D3D12_SHADER_DESC	 shaderDesc{};
			pReflection->GetDesc( &shaderDesc );

			for ( UINT cbIndex = 0; cbIndex < shaderDesc.ConstantBuffers; ++cbIndex )
			{
				ID3D12ShaderReflectionConstantBuffer* pCb = pReflection->GetConstantBufferByIndex( cbIndex );
				D3D12_SHADER_BUFFER_DESC			  cbDesc{};
				pCb->GetDesc( &cbDesc );

				ShaderBufferInfo bufInfo{};
				bufInfo._name		   = cbDesc.Name != nullptr ? cbDesc.Name : "";
				bufInfo._registerSpace = 0;
				bufInfo._bindPoint	   = 0;
				bufInfo._totalSize	   = cbDesc.Size;

				for ( UINT varIndex = 0; varIndex < cbDesc.Variables; ++varIndex )
				{
					ID3D12ShaderReflectionVariable* pVar = pCb->GetVariableByIndex( varIndex );
					D3D12_SHADER_VARIABLE_DESC		varDesc{};
					pVar->GetDesc( &varDesc );

					ShaderVariableInfo varInfo{};
					varInfo._name					= varDesc.Name != nullptr ? varDesc.Name : "";
					varInfo._offset					= varDesc.StartOffset;
					varInfo._size					= varDesc.Size;
					ID3D12ShaderReflectionType* pTy = pVar->GetType();
					if ( pTy != nullptr )
					{
						D3D12_SHADER_TYPE_DESC typeDesc{};
						pTy->GetDesc( &typeDesc );
						varInfo._type = hlslVariableTypeName( typeDesc.Class, typeDesc.Type, typeDesc.Rows, typeDesc.Columns );
					}
					bufInfo._listVariable.push_back( varInfo );
				}

				data._listConstantBuffer.push_back( std::move( bufInfo ) );
			}

			for ( UINT resourceIndex = 0; resourceIndex < shaderDesc.BoundResources; ++resourceIndex )
			{
				D3D12_SHADER_INPUT_BIND_DESC bindDesc{};
				pReflection->GetResourceBindingDesc( resourceIndex, &bindDesc );

				ShaderResourceBinding resBinding{};
				resBinding._name		  = bindDesc.Name != nullptr ? bindDesc.Name : "";
				resBinding._registerSpace = bindDesc.Space;
				resBinding._bindPoint	  = bindDesc.BindPoint;
				resBinding._bindCount	  = bindDesc.BindCount;
				resBinding._type		  = resourceTypeName( static_cast<uint32>( bindDesc.Type ) );
				data._listResource.push_back( std::move( resBinding ) );

				if ( bindDesc.Type == D3D_SIT_CBUFFER )
				{
					for ( ShaderBufferInfo& cb : data._listConstantBuffer )
					{
						if ( cb._name == resBinding._name )
						{
							cb._registerSpace = bindDesc.Space;
							cb._bindPoint	  = bindDesc.BindPoint;
							break;
						}
					}
				}
			}

			return data;
		}

		ShaderReflectionData reflectDxbc( const vector<uint8>& bytecode )
		{
			Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection;
			const HRESULT								   hr = D3DReflect( bytecode.data(), bytecode.size(), IID_PPV_ARGS( reflection.GetAddressOf() ) );
			if ( FAILED( hr ) || reflection == nullptr )
			{
				SW_LOG_ERROR( "D3DReflect failed for DXBC (hr=0x%#).", static_cast<uint32>( hr ) );
				return ShaderReflectionData{};
			}

			ShaderReflectionData data = fillFromId3d11Reflection( reflection.Get() );
			SW_LOG_TRACE( "ConstantBuffers: %# BoundResources: %#",
						  data._listConstantBuffer.size(), data._listResource.size() );
			return data;
		}

	#if defined( SW_HAS_DXC_API )
		DxcCreateInstanceProc loadDxcCreateInstance()
		{
			static void*				 s_hDxCompiler{ nullptr };
			static DxcCreateInstanceProc s_fnDxcCreateInstance{ nullptr };
			static bool					 s_bTried{ false };
			if ( s_bTried == false )
			{
				s_bTried	  = true;
				HMODULE hDll  = LoadLibraryA( "dxcompiler.dll" );
				s_hDxCompiler = static_cast<void*>( hDll );
				if ( s_hDxCompiler != nullptr )
				{
					s_fnDxcCreateInstance = reinterpret_cast<DxcCreateInstanceProc>(
						GetProcAddress( hDll, "DxcCreateInstance" ) );
				}
				if ( s_fnDxcCreateInstance == nullptr )
				{
					SW_LOG_WARNING( "dxcompiler.dll loaded but DxcCreateInstance not found." );
				}
			}
			return s_fnDxcCreateInstance;
		}

		ShaderReflectionData reflectDxil( const vector<uint8>& bytecode )
		{
			DxcCreateInstanceProc createInstance = loadDxcCreateInstance();
			if ( createInstance == nullptr )
			{
				SW_LOG_WARNING( "DXIL reflection unavailable: dxcompiler.dll not found or DxcCreateInstance failed." );
				return ShaderReflectionData{};
			}

			Microsoft::WRL::ComPtr<IDxcUtils> utils;
			if ( FAILED( createInstance( CLSID_DxcUtils, IID_PPV_ARGS( utils.GetAddressOf() ) ) ) || utils == nullptr )
			{
				SW_LOG_ERROR( "Failed to create IDxcUtils for reflection." );
				return ShaderReflectionData{};
			}

			DxcBuffer buffer{};
			buffer.Ptr		= bytecode.data();
			buffer.Size		= bytecode.size();
			buffer.Encoding = 0;

			ShaderReflectionData data{};

			Microsoft::WRL::ComPtr<ID3D12ShaderReflection> reflection12;
			HRESULT										   hr = utils->CreateReflection( &buffer, IID_PPV_ARGS( reflection12.GetAddressOf() ) );
			if ( SUCCEEDED( hr ) && reflection12 != nullptr )
			{
				data = fillFromId3d12Reflection( reflection12.Get() );
				SW_LOG_TRACE( "ConstantBuffers: %# BoundResources: %#",
							  data._listConstantBuffer.size(), data._listResource.size() );
			}
			else
			{
				// Some DXC builds expose DXIL reflection as ID3D11ShaderReflection.
				Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection11;
				hr = utils->CreateReflection( &buffer, IID_PPV_ARGS( reflection11.GetAddressOf() ) );
				if ( SUCCEEDED( hr ) && reflection11 != nullptr )
				{
					data = fillFromId3d11Reflection( reflection11.Get() );
					SW_LOG_TRACE( "ConstantBuffers: %# BoundResources: %#",
								  data._listConstantBuffer.size(), data._listResource.size() );
				}
				else
					SW_LOG_ERROR( "IDxcUtils::CreateReflection failed for DXIL (hr=0x%#).", static_cast<uint32>( hr ) );
			}

			return data;
		}
	#endif

	} // namespace

	ShaderReflectionData reflectDx( const vector<uint8>& bytecode, ShaderTargetFormat targetFormat )
	{
		if ( targetFormat == ShaderTargetFormat::DXBC_D3D11 )
			return reflectDxbc( bytecode );

	#if defined( SW_HAS_DXC_API )
		if ( targetFormat == ShaderTargetFormat::DXIL_D3D12 )
			return reflectDxil( bytecode );
	#endif

		SW_LOG_ERROR( "Unsupported DX target format %#.", static_cast<uint32>( targetFormat ) );
		return {};
	}
} // namespace sw::shader_reflection_detail
#endif
