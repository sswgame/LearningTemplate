#include "pch.h"

#include "Engine/Graphics/Shader/Compile/ShaderCooker.h"

#include "Core/Common/HashUtil.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Config/RHIBackendType.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingValidator.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflection.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflectionLibrary.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Serialization/Xml/XmlDocument.h"

#include "sw/config/CookContract.gen.h"

namespace sw
{
    namespace
    {
        struct ShaderCookerInternal
        {
            /** @brief 백엔드 하나의 셰이더 타깃 · 바이너리 폴더입니다. 쿠킹 표(`SW_RHI_BACKEND_TABLE`)의 줄마다 하나입니다. */
            struct BackendFolder
            {
                string_view        _folder;
                ShaderTargetFormat _format;
                bool               _bDefault; ///< 표의 기본 백엔드인가
            };

            static constexpr BackendFolder kArrBackendFolder[] = {
#define SW_SHADER_COOKER_BACKEND_ROW( Backend, ShaderFolder, ShaderTarget, Argument, CommandLineName ) { ShaderFolder, ShaderTargetFormat::ShaderTarget, RHIBackend::Backend == RHIBackend::SW_RHI_BACKEND_DEFAULT },
                SW_RHI_BACKEND_TABLE( SW_SHADER_COOKER_BACKEND_ROW )
#undef SW_SHADER_COOKER_BACKEND_ROW
            };
            static_assert( std::size( kArrBackendFolder ) == static_cast<size_t>( ShaderTargetFormat::Count ),
                           "Config/Engine/CookContract.json needs one rhi_backends row per ShaderTargetFormat" );

            static string getStemLower( string_view filePath )
            {
                const string fileName = FileUtil::getFileNamePart( filePath );
                const string stem     = FileUtil::removeExtension( fileName );
                return StringUtil::toLower( stem.c_str() );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "ShaderCooker" );

    string ShaderCooker::getStemLower( string_view filePath )
    {
        return ShaderCookerInternal::getStemLower( filePath );
    }

    uint64 ShaderCooker::computePermutationHash( const vector<string>& listPermutation )
    {
        if ( listPermutation.empty() )
            return 0;

        // `FOO` 와 `FOO=1` 은 컴파일러에게 같은 것이다. 런타임은 ShaderMacroDefine::parse 로 값 없는
        // define 에 "1" 을 채운 **뒤** 해시하므로, 여기서 원문 그대로 해시하면 같은 퍼뮤테이션이
        // 쿠킹과 런타임에서 서로 다른 해시가 된다. 그러면 쿠킹해 둔 변형을 아무도 못 찾는다(값을 적은
        // `SW_FORWARD=1` 은 맞고 값 없는 머티리얼 define 만 어긋나 눈에 덜 띈다). 두 오버로드가 같은 문자열을 보도록 여기서 맞춘다.
        vector<string> listSorted;
        listSorted.reserve( listPermutation.size() );
        for ( const string& define : listPermutation )
        {
            if ( define.empty() )
                continue;
            if ( define.find( '=' ) == string::npos )
                listSorted.push_back( define + "=1" );
            else
                listSorted.push_back( define );
        }
        if ( listSorted.empty() )
            return 0;

        std::sort( listSorted.begin(), listSorted.end() );

        uint64 hash{ HashUtil::kFnvOffset64 }; // FNV-1a 64비트 오프셋 기저값
        for ( const string& define : listSorted )
        {
            if ( define.empty() )
                continue;
            hash = StringUtil::computeHash64( define, false, hash );
        }
        return hash;
    }

    uint64 ShaderCooker::computePermutationHash( const vector<ShaderMacroDefine>& listDefine )
    {
        if ( listDefine.empty() )
            return 0;

        vector<string> listString;
        listString.reserve( listDefine.size() );
        for ( const auto& define : listDefine )
        {
            if ( define._name.empty() )
                continue;
            if ( define._value.empty() )
                listString.push_back( define._name );
            else
                listString.push_back( define._name + "=" + define._value );
        }
        return computePermutationHash( listString );
    }

    string ShaderCooker::computeBinaryFileName( string_view stemLower, ShaderStage stage,
                                                string_view entryPoint, uint64 permutationHash, string_view ext )
    {
        const string_view stageTag          = getShaderStageInfo( stage )._pTag;
        const string_view defaultEntryPoint = getShaderStageInfo( stage )._pEntryPoint;
        const bool        bStdEntry         = entryPoint.empty() || StringUtil::equals( entryPoint, defaultEntryPoint, true );

        string basePart = string( stemLower ) + "_";
        if ( bStdEntry )
            basePart += string( stageTag );
        else
            basePart += StringUtil::toLower( string( entryPoint ).c_str() );

        if ( permutationHash != 0 )
        {
            StringBuilder<constant::kMaxBuffer16> sb;
            sb.appendFormat( "_%#", Fmt( static_cast<uint32>( permutationHash & 0xFFFFFFFFu ), Format( 8, Format::Padding::Zero ).hex() ) );
            basePart += sb.view();
        }

        basePart += string( ext );
        return basePart;
    }

    string_view ShaderCooker::getSubfolderForFormat( ShaderTargetFormat format )
    {
        string_view defaultFolder;
        for ( const ShaderCookerInternal::BackendFolder& row : ShaderCookerInternal::kArrBackendFolder )
        {
            if ( row._format == format )
                return row._folder;
            if ( row._bDefault )
                defaultFolder = row._folder;
        }
        return defaultFolder;
    }

    string_view ShaderCooker::getExtensionForFormat( ShaderTargetFormat format )
    {
        switch ( format )
        {
            case ShaderTargetFormat::DXBC_D3D11:
                return ".dxbc";
            case ShaderTargetFormat::DXIL_D3D12:
                return ".dxil";
            // 둘 다 SPIR-V 라 확장자가 같다. 따로 적어 두면 "우연히 같은 값" 처럼 보여서, 한쪽만
            // 바꾸는 실수가 나기 쉽다. 같이 묶어 같아야 한다는 것을 드러낸다.
            case ShaderTargetFormat::SPIRV_Vulkan:
            case ShaderTargetFormat::SPIRV_OpenGL:
                return ".spv";
            case ShaderTargetFormat::Count:
                break;
        }
        return ".bin";
    }

    ShaderTargetFormat ShaderCooker::getFormatForSubfolder( string_view subfolder )
    {
        if ( subfolder.empty() )
            return ShaderTargetFormat::Count;
        for ( const ShaderCookerInternal::BackendFolder& row : ShaderCookerInternal::kArrBackendFolder )
        {
            if ( row._folder == subfolder )
                return row._format;
        }
        return ShaderTargetFormat::Count;
    }

    bool ShaderCooker::cookShader( string_view sourcePath, string_view outputPath, string_view entryPoint,
                                   ShaderStage stage, ShaderTargetFormat targetFormat,
                                   const vector<string>* pListPermutation,
                                   ShaderCookResult*     pOutResult )
    {
        if ( pOutResult != nullptr )
        {
            pOutResult->_sourcePath   = string( sourcePath );
            pOutResult->_outputPath   = string( outputPath );
            pOutResult->_entryPoint   = string( entryPoint );
            pOutResult->_stage        = stage;
            pOutResult->_targetFormat = targetFormat;
            pOutResult->_byteCodeSize = 0;
            pOutResult->_bSuccess     = SW_FALSE;
        }

        ShaderCompileDesc desc{};
        desc._filePath     = sourcePath;
        desc._entryPoint   = entryPoint;
        desc._stage        = stage;
        desc._targetFormat = targetFormat;

        if ( pListPermutation != nullptr )
        {
            for ( const string& permutationDefine : *pListPermutation )
            {
                if ( permutationDefine.empty() )
                    continue;
                const size_t      eqPos = permutationDefine.find( '=' );
                ShaderMacroDefine define;
                if ( eqPos != string::npos )
                {
                    define._name  = permutationDefine.substr( 0, eqPos );
                    define._value = permutationDefine.substr( eqPos + 1 );
                }
                else
                {
                    define._name  = permutationDefine;
                    define._value = "1";
                }
                desc._listDefine.push_back( std::move( define ) );
            }
        }

        ShaderCompileResult compileResult = ShaderCompiler::compileHlsl( desc );
        if ( compileResult._bSuccess == false || compileResult._bytecode.empty() )
        {
            SW_LOG_WARNING( "Failed to compile shader '%#' [%#] for %#: %#",
                            sourcePath.data(), entryPoint.data(),
                            getSubfolderForFormat( targetFormat ).data(),
                            compileResult._errorMessage.c_str() );
            return false;
        }

        const string outputDir = FileUtil::getDirectoryPart( outputPath );
        if ( outputDir.empty() == false )
            FileUtil::ensureDirectoryExists( outputDir );

        if ( FileUtil::writeFile( outputPath, compileResult._bytecode.data(), compileResult._bytecode.size() ) == false )
        {
            SW_LOG_ERROR( "Failed to write cooked bytecode to %#", outputPath );
            return false;
        }

        if ( pOutResult != nullptr )
        {
            pOutResult->_byteCodeSize = compileResult._bytecode.size();
            pOutResult->_bSuccess     = SW_TRUE;
        }

        SW_LOG_INFO( "Cooked shader '%#' [%#] -> '%#' (%zu bytes)",
                     sourcePath.data(), entryPoint.data(), outputPath.data(), compileResult._bytecode.size() );
        return true;
    }
} // namespace sw
