#include "pch.h"

#include "Engine/Graphics/Shader/ShaderCompiler.h"

#include "Core/Concurrency/atomic.h"
#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"

#include "Engine/Resource/ResourceUtil.h"

#if defined( SW_HAS_DXC_API )
    #include <dxcapi.h>
#endif

namespace sw
{
    namespace
    {
        struct ShaderCompilerInternal
        {
            /** @brief 셰이더 #include 검색 디렉터리 (engine/shaders, common/shaders, 소스 폴더). */
            static void collectShaderIncludeDirs( const string& shaderAbsPath, vector<string>& outListDir )
            {
                outListDir.clear();
                const string shaderDir = FileUtil::getDirectoryPart( shaderAbsPath );
                if ( shaderDir.empty() == false )
                    outListDir.push_back( FileUtil::normalizeSeparators( shaderDir ) );

                const string engineShaders = ResourceUtil::getDomainFolderPath( "engine", "shaders" );
                if ( engineShaders.empty() == false )
                    outListDir.push_back( engineShaders );

                const string commonShaders = ResourceUtil::getDomainFolderPath( "common", "shaders" );
                if ( commonShaders.empty() == false )
                    outListDir.push_back( commonShaders );
            }

#if defined( SW_PLATFORM_WINDOWS )
            /** @brief 다중 루트 ID3DInclude — common/shaders → engine/shaders/bindless.hlsli 등. */
            class MultiRootD3DInclude final : public ID3DInclude
            {
            public:
                explicit MultiRootD3DInclude( vector<string> listRoot )
                    : _listRoot{ std::move( listRoot ) }
                {
                }

                STDMETHOD( Open )( D3D_INCLUDE_TYPE includeType, LPCSTR pFileName, LPCVOID pParentData,
                                   LPCVOID* ppData, UINT* pBytes ) override
                {
                    (void)includeType;
                    (void)pParentData;
                    if ( pFileName == nullptr || ppData == nullptr || pBytes == nullptr )
                        return E_FAIL;

                    for ( const string& root : _listRoot )
                    {
                        const string candidate = FileUtil::normalizeSeparators( FileUtil::joinPath( root, pFileName ) );
                        if ( FileUtil::fileExists( candidate ) == false )
                            continue;

                        vector<uint8> bytes;
                        if ( FileUtil::readFile( candidate, bytes ) == false || bytes.empty() )
                            continue;

                        uint8* pHeap = static_cast<uint8*>( Memory::allocMemory( bytes.size() ) );
                        if ( pHeap == nullptr )
                            return E_OUTOFMEMORY;
                        Memory::copy( pHeap, bytes.data(), bytes.size() );
                        *ppData = pHeap;
                        *pBytes = static_cast<UINT>( bytes.size() );
                        return S_OK;
                    }
                    return E_FAIL;
                }

                STDMETHOD( Close )( LPCVOID pData ) override
                {
                    if ( pData != nullptr )
                        Memory::freeMemory( const_cast<void*>( pData ) );
                    return S_OK;
                }

            private:
                vector<string> _listRoot;
            };
#endif

            /**
             * @brief 셰이더 단계(Stage) 및 타깃 포맷에 해당하는 프로파일 문자열을 반환합니다.
             * @details DX11은 SM5.0, DX12/Vulkan/OpenGL은 Native Bindless 및 Descriptor Indexing을 위해 SM6.6을 반환합니다.
             */
            static const utf8* getTargetProfile( ShaderStage stage, ShaderTargetFormat targetFormat )
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
                        case ShaderStage::Geometry:
                            return "gs_5_0";
                        case ShaderStage::Hull:
                            return "hs_5_0";
                        case ShaderStage::Domain:
                            return "ds_5_0";
                        case ShaderStage::Mesh:
                        case ShaderStage::Amplification:
                        case ShaderStage::Count:
                        default:
                            break;
                    }
                    return "vs_5_0";
                }
                // DX12, Vulkan, OpenGL: 최신 표준 SM6.6 Native Bindless 및 힙 인덱싱 지원
                switch ( stage )
                {
                    case ShaderStage::Vertex:
                        return "vs_6_6";
                    case ShaderStage::Pixel:
                        return "ps_6_6";
                    case ShaderStage::Compute:
                        return "cs_6_6";
                    case ShaderStage::Geometry:
                        return "gs_6_6";
                    case ShaderStage::Hull:
                        return "hs_6_6";
                    case ShaderStage::Domain:
                        return "ds_6_6";
                    case ShaderStage::Mesh:
                        return "ms_6_6";
                    case ShaderStage::Amplification:
                        return "as_6_6";
                    case ShaderStage::Count:
                    default:
                        break;
                }
                return "vs_6_6";
            }

#if defined( SW_HAS_DXC_API )
    #if defined( SW_PLATFORM_WINDOWS )
            template <typename T>
            using DxcComPtr = Microsoft::WRL::ComPtr<T>;

            template <typename T>
            static T** dxcAddressOf( DxcComPtr<T>& ptr )
            {
                return ptr.GetAddressOf();
            }

            template <typename T>
            static T* dxcGet( const DxcComPtr<T>& ptr )
            {
                return ptr.Get();
            }
    #else
            template <typename T>
            using DxcComPtr = CComPtr<T>;

            template <typename T>
            static T** dxcAddressOf( DxcComPtr<T>& ptr )
            {
                return &ptr;
            }

            template <typename T>
            static T* dxcGet( const DxcComPtr<T>& ptr )
            {
                return ptr;
            }
    #endif
#endif

            static inline atomic<bool> s_bDiskCacheEnabled{ true };

            static string getShaderCacheDirectory()
            {
                const string& root     = ResourceUtil::getRootFolderPath();
                const string  baseRoot = root.empty() ? FileUtil::joinPath( ResourceUtil::getProjectFolderPath(), "Resource" ) : root;
                return FileUtil::normalizeSeparators( FileUtil::joinPath( baseRoot, "cache/shaders" ) );
            }

            static string computeCachePath( const ShaderCompileDesc& desc, const string& absPathStr )
            {
                const string cacheDir = getShaderCacheDirectory();
                if ( cacheDir.empty() )
                    return "";

                vector<uint8> sourceBytes;
                FileUtil::readFile( absPathStr, sourceBytes );

                uint64 hash = StringUtil::computeHash64( desc._filePath, false );
                hash        = StringUtil::computeHash64( desc._entryPoint, false, hash );
                hash        = StringUtil::computeHash64( to_string( static_cast<uint32>( desc._stage ) ), false, hash );
                hash        = StringUtil::computeHash64( to_string( static_cast<uint32>( desc._targetFormat ) ), false, hash );
                for ( const auto& def : desc._listDefine )
                {
                    hash = StringUtil::computeHash64( def._name, false, hash );
                    hash = StringUtil::computeHash64( def._value, false, hash );
                }
                if ( sourceBytes.empty() == false )
                {
                    const string_view sourceStr( reinterpret_cast<const utf8*>( sourceBytes.data() ), sourceBytes.size() );
                    hash = StringUtil::computeHash64( sourceStr, false, hash );
                }

                fixed_string<constant::kMaxBuffer64> buf;
                formatstring( buf.data(), buf.capacity(), "%#.bin", Fmt( static_cast<uint64>( hash ), Format( 16, Format::Padding::Zero ).hex() ) );
                return FileUtil::joinPath( cacheDir, buf.c_str() );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "ShaderCompiler" );

    void ShaderCompiler::enableDiskCache( bool bEnable )
    {
        ShaderCompilerInternal::s_bDiskCacheEnabled.store( bEnable, std::memory_order_relaxed );
    }

    bool ShaderCompiler::isDiskCacheEnabled()
    {
        return ShaderCompilerInternal::s_bDiskCacheEnabled.load( std::memory_order_relaxed );
    }

    void ShaderCompiler::clearDiskCache()
    {
        const string cacheDir = ShaderCompilerInternal::getShaderCacheDirectory();
        if ( cacheDir.empty() == false && FileUtil::directoryExists( cacheDir ) )
        {
            vector<string> listFile;
            FileUtil::collectFiles( cacheDir, "", listFile, false );
            for ( const string& file : listFile )
            {
                FileUtil::removeFile( file );
            }
        }
    }

    ShaderCompileResult ShaderCompiler::compileHLSL( const ShaderCompileDesc& desc )
    {
        ShaderCompileResult result{};
        result._bSuccess = false;
        result._bytecode.reserve( 4096 );

        string absPathStr;
        if ( FileUtil::fileExists( desc._filePath ) )
            absPathStr = desc._filePath;
        else
            absPathStr = ResourceUtil::getResourcePath( desc._filePath );

        if ( absPathStr.empty() || FileUtil::fileExists( absPathStr ) == false )
        {
            result._errorMessage = "Shader source file not found: " + desc._filePath;
            // 호출부가 존재 여부를 먼저 검사하는 것이 정상. 없는 파일은 ERROR가 아니라 조용히 실패.
            return result;
        }

        string cachePath;
        if ( ShaderCompilerInternal::s_bDiskCacheEnabled.load( std::memory_order_relaxed ) )
        {
            cachePath = ShaderCompilerInternal::computeCachePath( desc, absPathStr );
            if ( cachePath.empty() == false && FileUtil::fileExists( cachePath ) )
            {
                vector<uint8> cachedBytes;
                if ( FileUtil::readFile( cachePath, cachedBytes ) && cachedBytes.empty() == false )
                {
                    result._bytecode = std::move( cachedBytes );
                    result._bSuccess = true;
                    return result;
                }
            }
        }

        auto saveToCacheIfEnabled = [&]( const vector<uint8>& bytecode )
        {
            if ( ShaderCompilerInternal::s_bDiskCacheEnabled.load( std::memory_order_relaxed ) && cachePath.empty() == false && bytecode.empty() == false )
            {
                FileUtil::ensureDirectoryExists( FileUtil::getDirectoryPart( cachePath ) );
                FileUtil::writeFile( cachePath, bytecode.data(), bytecode.size() );
            }
        };

        string profile = ShaderCompilerInternal::getTargetProfile( desc._stage, desc._targetFormat );

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
                wstring                          wPath = StringUtil::utf8ToUtf16( absPathStr.c_str() );

                vector<D3D_SHADER_MACRO> listMacro;
                listMacro.reserve( desc._listDefine.size() + 2 );
                listMacro.push_back( { "DX11", "1" } );
                for ( const ShaderMacroDefine& def : desc._listDefine )
                {
                    if ( def._name.empty() )
                        continue;
                    listMacro.push_back( { def._name.c_str(), def._value.c_str() } );
                }
                listMacro.push_back( { nullptr, nullptr } );

                vector<string> listIncludeDir;
                ShaderCompilerInternal::collectShaderIncludeDirs( absPathStr, listIncludeDir );
                ShaderCompilerInternal::MultiRootD3DInclude includeHandler( std::move( listIncludeDir ) );

                HRESULT hr = D3DCompileFromFile(
                    wPath.c_str(),
                    listMacro.data(),
                    &includeHandler,
                    desc._entryPoint.c_str(),
                    profile.c_str(),
                    compileFlags,
                    0,
                    codeBlob.GetAddressOf(),
                    errorBlob.GetAddressOf() );

                if ( FAILED( hr ) )
                {
                    if ( errorBlob != nullptr )
                        result._errorMessage = static_cast<const utf8*>( errorBlob->GetBufferPointer() );
                    else
                        result._errorMessage = "Unknown DXBC compilation error.";
                    SW_LOG_ERROR( "%#", result._errorMessage.c_str() );
                    return result;
                }

                const uint8* pData = static_cast<const uint8*>( codeBlob->GetBufferPointer() );
                result._bytecode.assign( pData, pData + codeBlob->GetBufferSize() );
                result._bSuccess = true;
                saveToCacheIfEnabled( result._bytecode );

                SW_LOG_TRACE( "Target: D3D11 (DXBC Row-Major), Size: %# bytes", static_cast<uint32>( result._bytecode.size() ) );
                return result;
            }
        }
#endif // SW_PLATFORM_WINDOWS

#if defined( SW_HAS_DXC_API )
        BLOCK( "DXC Compiler (DXIL / SPIR-V) Path" )
        {
            static auto s_getDxCompilerHandle = []() -> void*
            {
                string         libName       = FileUtil::formatSharedLibraryName( "dxcompiler" );
                const string   execDir       = FileUtil::getDirectoryPart( FileUtil::getExecutablePath() );
                vector<string> listCandidate = {
                    execDir.empty() ? libName : FileUtil::joinPath( execDir, libName ),
                    libName };
                for ( const string& cand : listCandidate )
                {
                    if ( FileUtil::fileExists( cand ) )
                    {
                        void* pH = FileUtil::loadDynamicLibrary( cand );
                        if ( pH != nullptr )
                            return pH;
                        SW_LOG_ERROR( "Failed to load %#", cand.c_str() );
                    }
                }
                return FileUtil::loadDynamicLibrary( libName );
            };
            static void*                 s_pDxCompiler         = s_getDxCompilerHandle();
            static DxcCreateInstanceProc s_fnDxcCreateInstance = ( s_pDxCompiler != nullptr )
                                                                   ? reinterpret_cast<DxcCreateInstanceProc>( FileUtil::getDynamicSymbol( s_pDxCompiler, "DxcCreateInstance" ) )
                                                                   : nullptr;

            DxcCreateInstanceProc fnDxcCreateInstance = s_fnDxcCreateInstance;
            if ( fnDxcCreateInstance == nullptr )
                SW_LOG_ERROR( "DxcCreateInstance unavailable (libdxcompiler not loaded)" );

            ShaderCompilerInternal::DxcComPtr<IDxcUtils>     utils;
            ShaderCompilerInternal::DxcComPtr<IDxcCompiler3> compiler;

            if ( fnDxcCreateInstance != nullptr )
            {
                HRESULT hrInit = fnDxcCreateInstance( CLSID_DxcUtils, IID_PPV_ARGS( ShaderCompilerInternal::dxcAddressOf( utils ) ) );
                if ( SUCCEEDED( hrInit ) )
                    fnDxcCreateInstance( CLSID_DxcCompiler, IID_PPV_ARGS( ShaderCompilerInternal::dxcAddressOf( compiler ) ) );
                else
                    SW_LOG_ERROR( "DxcCreateInstance(CLSID_DxcUtils) failed: 0x%#", hrInit );
            }

            if ( utils != nullptr && compiler != nullptr )
            {
                ShaderCompilerInternal::DxcComPtr<IDxcIncludeHandler> includeHandler;
                utils->CreateDefaultIncludeHandler( ShaderCompilerInternal::dxcAddressOf( includeHandler ) );

                wstring                                             wPath = StringUtil::utf8ToUtf16( absPathStr.c_str() );
                ShaderCompilerInternal::DxcComPtr<IDxcBlobEncoding> sourceBlob;
                HRESULT                                             hrLoad = utils->LoadFile( wPath.c_str(), nullptr, ShaderCompilerInternal::dxcAddressOf( sourceBlob ) );

                if ( SUCCEEDED( hrLoad ) )
                {
                    wstring wEntryPoint = StringUtil::utf8ToUtf16( desc._entryPoint.c_str() );
                    wstring wProfile    = StringUtil::utf8ToUtf16( profile.c_str() );

                    vector<LPCWSTR> listArgument;
                    vector<wstring> listDefineArg;
                    listDefineArg.reserve( desc._listDefine.size() );

                    listArgument.push_back( wPath.c_str() );
                    listArgument.push_back( L"-E" );
                    listArgument.push_back( wEntryPoint.c_str() );
                    listArgument.push_back( L"-T" );
                    listArgument.push_back( wProfile.c_str() );
                    listArgument.push_back( L"-Zpr" );

                    vector<string>  listIncludeDir;
                    vector<wstring> listIncludeDirW;
                    ShaderCompilerInternal::collectShaderIncludeDirs( absPathStr, listIncludeDir );
                    listIncludeDirW.reserve( listIncludeDir.size() );
                    for ( const string& dir : listIncludeDir )
                    {
                        listIncludeDirW.push_back( StringUtil::utf8ToUtf16( dir.c_str() ) );
                        listArgument.push_back( L"-I" );
                        listArgument.push_back( listIncludeDirW.back().c_str() );
                    }

                    if ( desc._targetFormat == ShaderTargetFormat::SPIRV_Vulkan )
                    {
                        listArgument.push_back( L"-spirv" );
                        listArgument.push_back( L"-fspv-target-env=vulkan1.3" );
                        listArgument.push_back( L"-fvk-use-dx-position-w" );
                    }
                    else if ( desc._targetFormat == ShaderTargetFormat::SPIRV_OpenGL )
                    {
                        listArgument.push_back( L"-spirv" );
                        listArgument.push_back( L"-fspv-target-env=vulkan1.1" );
                        listArgument.push_back( L"-fvk-use-dx-position-w" );
                        listArgument.push_back( L"-fvk-b-shift" );
                        listArgument.push_back( L"16" );
                        listArgument.push_back( L"0" );
                        listArgument.push_back( L"-fvk-u-shift" );
                        listArgument.push_back( L"48" );
                        listArgument.push_back( L"0" );
                    }

                    listArgument.push_back( L"-D" );
                    if ( desc._targetFormat == ShaderTargetFormat::DXIL_D3D12 )
                    {
                        listArgument.push_back( L"DX12=1" );
                        // Graphics: fully-bindless heap sampling + optional bindless UAV array.
                        // Compute RS (SampleIndirect) uses explicit u0/u1 — skip BINDLESS_UAV on CS.
                        if ( desc._stage != ShaderStage::Compute )
                        {
                            listArgument.push_back( L"-D" );
                            listArgument.push_back( L"SW_BINDLESS=1" );
                            listArgument.push_back( L"-D" );
                            listArgument.push_back( L"BINDLESS_UAV=1" );
                        }
                    }
                    else if ( desc._targetFormat == ShaderTargetFormat::SPIRV_Vulkan )
                    {
                        listArgument.push_back( L"VULKAN=1" );
                        if ( desc._stage != ShaderStage::Compute )
                        {
                            listArgument.push_back( L"-D" );
                            listArgument.push_back( L"SW_BINDLESS=1" );
                            listArgument.push_back( L"-D" );
                            listArgument.push_back( L"BINDLESS_UAV=1" );
                        }
                    }
                    else if ( desc._targetFormat == ShaderTargetFormat::SPIRV_OpenGL )
                        listArgument.push_back( L"OPENGL=1" );
                    else if ( desc._targetFormat == ShaderTargetFormat::DXBC_D3D11 )
                        listArgument.push_back( L"DX11=1" );

                    for ( const ShaderMacroDefine& def : desc._listDefine )
                    {
                        if ( def._name.empty() )
                            continue;
                        string defineStr = def._name;
                        defineStr += "=";
                        defineStr += def._value;
                        listDefineArg.push_back( StringUtil::utf8ToUtf16( defineStr.c_str() ) );
                        listArgument.push_back( L"-D" );
                        listArgument.push_back( listDefineArg.back().c_str() );
                    }

    #if defined( SW_DEBUG )
                    listArgument.push_back( L"-Zi" );
                    listArgument.push_back( L"-Od" );
    #endif

                    DxcBuffer sourceBuffer{};
                    sourceBuffer.Ptr      = sourceBlob->GetBufferPointer();
                    sourceBuffer.Size     = sourceBlob->GetBufferSize();
                    sourceBuffer.Encoding = DXC_CP_UTF8;

                    ShaderCompilerInternal::DxcComPtr<IDxcResult> compileResult;
                    HRESULT                                       hrCompile = compiler->Compile(
                        &sourceBuffer,
                        listArgument.data(),
                        static_cast<uint32>( listArgument.size() ),
                        ShaderCompilerInternal::dxcGet( includeHandler ),
                        IID_PPV_ARGS( ShaderCompilerInternal::dxcAddressOf( compileResult ) ) );

                    if ( FAILED( hrCompile ) )
                        SW_LOG_ERROR( "DXC compiler->Compile failed with HRESULT: 0x%#", hrCompile );

                    if ( SUCCEEDED( hrCompile ) )
                    {
                        HRESULT status = S_OK;
                        compileResult->GetStatus( &status );

                        ShaderCompilerInternal::DxcComPtr<IDxcBlobUtf8> errors;
                        compileResult->GetOutput( DXC_OUT_ERRORS, IID_PPV_ARGS( ShaderCompilerInternal::dxcAddressOf( errors ) ), nullptr );

                        if ( errors != nullptr && errors->GetStringLength() > 0 )
                            result._errorMessage = errors->GetStringPointer();

                        if ( FAILED( status ) )
                        {
                            SW_LOG_ERROR( "%#", result._errorMessage.c_str() );
                            return result;
                        }

                        ShaderCompilerInternal::DxcComPtr<IDxcBlob> shaderBlob;
                        compileResult->GetOutput( DXC_OUT_OBJECT, IID_PPV_ARGS( ShaderCompilerInternal::dxcAddressOf( shaderBlob ) ), nullptr );

                        if ( shaderBlob != nullptr && shaderBlob->GetBufferSize() > 0 )
                        {
                            const uint8* pData = static_cast<const uint8*>( shaderBlob->GetBufferPointer() );
                            result._bytecode.assign( pData, pData + shaderBlob->GetBufferSize() );
                            result._bSuccess = true;
                            saveToCacheIfEnabled( result._bytecode );

    #if defined( SW_DEBUG )
                            const utf8* pFormatName = ( desc._targetFormat == ShaderTargetFormat::SPIRV_Vulkan )
                                                        ? "Vulkan (SPIR-V vulkan1.2)"
                                                    : ( desc._targetFormat == ShaderTargetFormat::SPIRV_OpenGL )
                                                        ? "OpenGL (SPIR-V universal1.5)"
                                                        : "D3D12 (DXIL Row-Major)";
                            SW_LOG_TRACE( "Target: %#", pFormatName );
    #endif
                            return result;
                        }
                    }
                }
                else
                    SW_LOG_ERROR( "DXC LoadFile failed: 0x%# (%#)", hrLoad, absPathStr.c_str() );
            }

    #if defined( SW_PLATFORM_WINDOWS )
            // FXC는 DXBC만 생성 — SPIR-V 타깃(Vulkan/OpenGL)에는 절대 폴백하지 않음.
            if ( desc._targetFormat != ShaderTargetFormat::SPIRV_Vulkan && desc._targetFormat != ShaderTargetFormat::SPIRV_OpenGL )
            {
                UINT                             compileFlags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_PACK_MATRIX_ROW_MAJOR;
                Microsoft::WRL::ComPtr<ID3DBlob> codeBlob;
                Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
                wstring                          wPath( absPathStr.begin(), absPathStr.end() );

                vector<D3D_SHADER_MACRO> listMacro;
                listMacro.reserve( desc._listDefine.size() + 1 );
                for ( const ShaderMacroDefine& def : desc._listDefine )
                {
                    if ( def._name.empty() )
                        continue;
                    listMacro.push_back( { def._name.c_str(), def._value.c_str() } );
                }
                listMacro.push_back( { nullptr, nullptr } );

                HRESULT hrFallback = D3DCompileFromFile(
                    wPath.c_str(),
                    listMacro.data(),
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
                    saveToCacheIfEnabled( result._bytecode );

                    SW_LOG_TRACE( "Target: D3D12/11 (Row-Major)" );
                    return result;
                }
            }
    #endif // SW_PLATFORM_WINDOWS
        }
#endif // SW_HAS_DXC_API

        if ( result._errorMessage.empty() )
            result._errorMessage = "Failed to compile shader with DXC and D3DCompiler (hrCompile/hrLoad failed)";
        SW_LOG_ERROR( "%# (Path: %#)", result._errorMessage.c_str(), desc._filePath.c_str() );
        return result;
    }
} // namespace sw
