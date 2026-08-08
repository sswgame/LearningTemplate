/**
 * @file ShaderCompiler.cpp
 * @brief HLSL→DXIL/SPIR-V 등 셰이더 컴파일
 */
#include "Core/CoreMinimal.h"

#include "ShaderCompiler.h"

#include "Core/Utility/File/FileUtil.h"
#include "Core/Utility/Log/Logger.h"
#include "Core/Utility/Resource/ResourceUtil.h"
#include "Core/Utility/String/StringUtil.h"

namespace sw
{
	namespace
	{
		const utf8* getTargetProfile( ShaderStage stage, ShaderTargetFormat targetFormat )
		{
			if ( targetFormat == ShaderTargetFormat::DXBC_D3D11 )
			{
				switch ( stage )
				{
					case ShaderStage::Vertex:
						return "vs_5_0";
					case ShaderStage::Pixel:
						return "ps_5_0";
					case ShaderStage::Compute:
						return "cs_5_0";
				}
				return "vs_5_0";
			}
			else
			{
				switch ( stage )
				{
					case ShaderStage::Vertex:
						return "vs_6_0";
					case ShaderStage::Pixel:
						return "ps_6_0";
					case ShaderStage::Compute:
						return "cs_6_0";
				}
				return "vs_6_0";
			}
		}

#if defined( SW_HAS_DXC_API )
	#if defined( SW_PLATFORM_WINDOWS )
		template <typename T>
		using DxcComPtr = Microsoft::WRL::ComPtr<T>;

		template <typename T>
		T** dxcAddressOf( DxcComPtr<T>& ptr )
		{
			return ptr.GetAddressOf();
		}

		template <typename T>
		T* dxcGet( const DxcComPtr<T>& ptr )
		{
			return ptr.Get();
		}
	#else
		template <typename T>
		using DxcComPtr = CComPtr<T>;

		template <typename T>
		T** dxcAddressOf( DxcComPtr<T>& ptr )
		{
			return &ptr;
		}

		template <typename T>
		T* dxcGet( const DxcComPtr<T>& ptr )
		{
			return ptr;
		}
	#endif
#endif
	} // namespace

	ShaderCompileResult ShaderCompiler::compileHLSL( const ShaderCompileDesc& desc )
	{
		ShaderCompileResult result{};
		result._bSuccess = false;
		result._bytecode.reserve( 4096 );

		std::string absPathStr = ResourceUtil::getResourcePath( desc._filePath );
		if ( absPathStr.empty() == true || FileUtil::isFileExist( absPathStr ) == false )
		{
			result._errorMessage = "Shader source file not found: " + desc._filePath;
			SW_LOG_ERROR( "[ShaderCompiler Error] %#", result._errorMessage.c_str() );
			return result;
		}

		std::string profile = getTargetProfile( desc._stage, desc._targetFormat );

#if defined( SW_PLATFORM_WINDOWS )
		BLOCK( "DXBC (D3D11) Compilation Path" )
		{
			if ( desc._targetFormat == ShaderTargetFormat::DXBC_D3D11 )
			{
				UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_PACK_MATRIX_ROW_MAJOR;
	#if defined( SW_DEBUG )
				compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
	#endif

				Microsoft::WRL::ComPtr<ID3DBlob> codeBlob;
				Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
				std::wstring					 wPath = StringUtil::utf8ToUtf16( absPathStr );

				std::vector<D3D_SHADER_MACRO> macros;
				macros.reserve( desc._defines.size() + 2 );
				macros.push_back( { "DX11", "1" } );
				for ( const ShaderMacroDefine& def : desc._defines )
				{
					if ( def._name.empty() )
						continue;
					macros.push_back( { def._name.c_str(), def._value.c_str() } );
				}
				macros.push_back( { nullptr, nullptr } );

				HRESULT hr = D3DCompileFromFile(
					wPath.c_str(),
					macros.data(),
					D3D_COMPILE_STANDARD_FILE_INCLUDE,
					desc._entryPoint.c_str(),
					profile.c_str(),
					compileFlags,
					0,
					codeBlob.GetAddressOf(),
					errorBlob.GetAddressOf() );

				if ( FAILED( hr ) )
				{
					if ( errorBlob != nullptr )
					{
						result._errorMessage = static_cast<const utf8*>( errorBlob->GetBufferPointer() );
					}
					else
					{
						result._errorMessage = "Unknown DXBC compilation error.";
					}
					SW_LOG_ERROR( "[ShaderCompiler DXBC Error] %#", result._errorMessage.c_str() );
					return result;
				}

				const uint8* pData = static_cast<const uint8*>( codeBlob->GetBufferPointer() );
				result._bytecode.assign( pData, pData + codeBlob->GetBufferSize() );
				result._bSuccess = true;

				SW_LOG_INFO( "[ShaderCompiler DXBC Success] Target: D3D11 (DXBC Row-Major), Size: %# bytes", static_cast<uint32>( result._bytecode.size() ) );
				return result;
			}
		}
#endif // SW_PLATFORM_WINDOWS

#if defined( SW_HAS_DXC_API )
		BLOCK( "DXC Compiler (DXIL / SPIR-V) Path" )
		{
			static auto s_getDxCompilerHandle = []() -> void*
			{
				std::string				 libName	= FileUtil::formatSharedLibraryName( "dxcompiler" );
				std::vector<std::string> candidates = {
					FileUtil::getDirectoryPart( FileUtil::getExecutablePath() ) + "/" + libName,
					libName };
				for ( const auto& cand : candidates )
				{
					if ( FileUtil::isFileExist( cand ) )
					{
						void* h = FileUtil::loadDynamicLibrary( cand );
						if ( h != nullptr )
							return h;
						SW_LOG_ERROR( "[ShaderCompiler] Failed to load %#", cand.c_str() );
					}
				}
				return FileUtil::loadDynamicLibrary( libName );
			};
			static void*				  s_hDxCompiler		   = s_getDxCompilerHandle();
			static DxcCreateInstanceProc s_fnDxcCreateInstance = ( s_hDxCompiler != nullptr )
																	 ? reinterpret_cast<DxcCreateInstanceProc>( FileUtil::getDynamicSymbol( s_hDxCompiler, "DxcCreateInstance" ) )
																	 : nullptr;

			DxcCreateInstanceProc fnDxcCreateInstance = s_fnDxcCreateInstance;
			if ( fnDxcCreateInstance == nullptr )
			{
				SW_LOG_ERROR( "[ShaderCompiler] DxcCreateInstance unavailable (libdxcompiler not loaded)" );
			}

			DxcComPtr<IDxcUtils>	 utils;
			DxcComPtr<IDxcCompiler3> compiler;

			if ( fnDxcCreateInstance != nullptr )
			{
				HRESULT hrInit = fnDxcCreateInstance( CLSID_DxcUtils, IID_PPV_ARGS( dxcAddressOf( utils ) ) );
				if ( SUCCEEDED( hrInit ) )
				{
					fnDxcCreateInstance( CLSID_DxcCompiler, IID_PPV_ARGS( dxcAddressOf( compiler ) ) );
				}
				else
				{
					SW_LOG_ERROR( "[ShaderCompiler] DxcCreateInstance(CLSID_DxcUtils) failed: 0x%X", hrInit );
				}
			}

			if ( utils != nullptr && compiler != nullptr )
			{
				DxcComPtr<IDxcIncludeHandler> includeHandler;
				utils->CreateDefaultIncludeHandler( dxcAddressOf( includeHandler ) );

				std::wstring			   wPath = StringUtil::utf8ToUtf16( absPathStr );
				DxcComPtr<IDxcBlobEncoding> sourceBlob;
				HRESULT					   hrLoad = utils->LoadFile( wPath.c_str(), nullptr, dxcAddressOf( sourceBlob ) );

				if ( SUCCEEDED( hrLoad ) )
				{
					std::wstring wEntryPoint = StringUtil::utf8ToUtf16( desc._entryPoint );
					std::wstring wProfile	 = StringUtil::utf8ToUtf16( profile );

					std::vector<LPCWSTR>	  arguments;
					std::vector<std::wstring> defineArgs;
					defineArgs.reserve( desc._defines.size() );

					arguments.push_back( wPath.c_str() );
					arguments.push_back( L"-E" );
					arguments.push_back( wEntryPoint.c_str() );
					arguments.push_back( L"-T" );
					arguments.push_back( wProfile.c_str() );
					arguments.push_back( L"-Zpr" );

					if ( desc._targetFormat == ShaderTargetFormat::SPIRV_Vulkan || desc._targetFormat == ShaderTargetFormat::SPIRV_OpenGL )
					{
						arguments.push_back( L"-spirv" );
						arguments.push_back( L"-fspv-target-env=vulkan1.2" );
						arguments.push_back( L"-fvk-use-dx-position-w" );
					}

					arguments.push_back( L"-D" );
					if ( desc._targetFormat == ShaderTargetFormat::DXIL_D3D12 )
					{
						arguments.push_back( L"DX12=1" );
						// Graphics uses bindless UAV heap; compute RS (SampleIndirect) uses explicit u0/u1 tables.
						// Defining BINDLESS_UAV on CS forces cbuffer b1 which is not in the compute root signature.
						if ( desc._stage != ShaderStage::Compute )
						{
							arguments.push_back( L"-D" );
							arguments.push_back( L"BINDLESS_UAV=1" );
						}
					}
					else if ( desc._targetFormat == ShaderTargetFormat::SPIRV_Vulkan )
					{
						arguments.push_back( L"VULKAN=1" );
						if ( desc._stage != ShaderStage::Compute )
						{
							arguments.push_back( L"-D" );
							arguments.push_back( L"BINDLESS_UAV=1" );
						}
					}
					else if ( desc._targetFormat == ShaderTargetFormat::SPIRV_OpenGL )
						arguments.push_back( L"OPENGL=1" );
					else if ( desc._targetFormat == ShaderTargetFormat::DXBC_D3D11 )
						arguments.push_back( L"DX11=1" );

					for ( const ShaderMacroDefine& def : desc._defines )
					{
						if ( def._name.empty() )
							continue;
						std::string defineStr = def._name;
						defineStr += "=";
						defineStr += def._value;
						defineArgs.push_back( StringUtil::utf8ToUtf16( defineStr ) );
						arguments.push_back( L"-D" );
						arguments.push_back( defineArgs.back().c_str() );
					}

	#if defined( SW_DEBUG )
					arguments.push_back( L"-Zi" );
					arguments.push_back( L"-Od" );
	#endif

					DxcBuffer sourceBuffer{};
					sourceBuffer.Ptr	  = sourceBlob->GetBufferPointer();
					sourceBuffer.Size	  = sourceBlob->GetBufferSize();
					sourceBuffer.Encoding = DXC_CP_UTF8;

					DxcComPtr<IDxcResult> compileResult;
					HRESULT				  hrCompile = compiler->Compile(
						 &sourceBuffer,
						 arguments.data(),
						 static_cast<uint32>( arguments.size() ),
						 dxcGet( includeHandler ),
						 IID_PPV_ARGS( dxcAddressOf( compileResult ) ) );

					if ( FAILED( hrCompile ) )
					{
						SW_LOG_ERROR( "[ShaderCompiler] DXC compiler->Compile failed with HRESULT: 0x%X", hrCompile );
					}

					if ( SUCCEEDED( hrCompile ) )
					{
						HRESULT status = S_OK;
						compileResult->GetStatus( &status );

						DxcComPtr<IDxcBlobUtf8> errors;
						compileResult->GetOutput( DXC_OUT_ERRORS, IID_PPV_ARGS( dxcAddressOf( errors ) ), nullptr );

						if ( errors != nullptr && errors->GetStringLength() > 0 )
						{
							result._errorMessage = errors->GetStringPointer();
						}

						if ( FAILED( status ) )
						{
							SW_LOG_ERROR( "[ShaderCompiler DXC Error] %#", result._errorMessage.c_str() );
							return result;
						}

						DxcComPtr<IDxcBlob> shaderBlob;
						compileResult->GetOutput( DXC_OUT_OBJECT, IID_PPV_ARGS( dxcAddressOf( shaderBlob ) ), nullptr );

						if ( shaderBlob != nullptr && shaderBlob->GetBufferSize() > 0 )
						{
							const uint8* pData = static_cast<const uint8*>( shaderBlob->GetBufferPointer() );
							result._bytecode.assign( pData, pData + shaderBlob->GetBufferSize() );
							result._bSuccess = true;

							const utf8* formatName = ( desc._targetFormat == ShaderTargetFormat::SPIRV_Vulkan ) ? "Vulkan (SPIR-V Row-Major Y-Inverted)"
												   : ( desc._targetFormat == ShaderTargetFormat::SPIRV_OpenGL ) ? "OpenGL (SPIR-V Row-Major Y-Inverted)"
																												: "D3D12 (DXIL Row-Major)";
							SW_LOG_INFO( "[ShaderCompiler DXC Success] Target: %#", formatName );
							return result;
						}
					}
				}
				else
				{
					SW_LOG_ERROR( "[ShaderCompiler] DXC LoadFile failed: 0x%X (%#)", hrLoad, absPathStr.c_str() );
				}
			}

	#if defined( SW_PLATFORM_WINDOWS )
			if ( desc._targetFormat != ShaderTargetFormat::SPIRV_Vulkan )
			{
				UINT							 compileFlags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_PACK_MATRIX_ROW_MAJOR;
				Microsoft::WRL::ComPtr<ID3DBlob> codeBlob;
				Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
				std::wstring					 wPath( absPathStr.begin(), absPathStr.end() );

				std::vector<D3D_SHADER_MACRO> macros;
				macros.reserve( desc._defines.size() + 1 );
				for ( const ShaderMacroDefine& def : desc._defines )
				{
					if ( def._name.empty() )
						continue;
					macros.push_back( { def._name.c_str(), def._value.c_str() } );
				}
				macros.push_back( { nullptr, nullptr } );

				HRESULT hrFallback = D3DCompileFromFile(
					wPath.c_str(),
					macros.data(),
					D3D_COMPILE_STANDARD_FILE_INCLUDE,
					desc._entryPoint.c_str(),
					profile.c_str(),
					compileFlags,
					0,
					codeBlob.GetAddressOf(),
					errorBlob.GetAddressOf() );

				if ( SUCCEEDED( hrFallback ) )
				{
					const uint8* pData = static_cast<const uint8*>( codeBlob->GetBufferPointer() );
					result._bytecode.assign( pData, pData + codeBlob->GetBufferSize() );
					result._bSuccess = true;

					SW_LOG_INFO( "[ShaderCompiler Fallback Success] Target: D3D12/11 (Row-Major)" );
					return result;
				}
			}
	#endif // SW_PLATFORM_WINDOWS
		}
#endif // SW_HAS_DXC_API

		if ( result._errorMessage.empty() )
			result._errorMessage = "Failed to compile shader with DXC and D3DCompiler (hrCompile/hrLoad failed)";
		SW_LOG_ERROR( "[ShaderCompiler Error] %# (Path: %#)", result._errorMessage.c_str(), desc._filePath.c_str() );
		return result;
	}
} // namespace sw
