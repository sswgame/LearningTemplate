#include "pch.h"

#include "Engine/Graphics/Shader/Compile/ShaderBaker.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Config/EngineData.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingContract.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflection.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflectionLibrary.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
    namespace
    {
        struct ShaderBakerInternal
        {
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
    SW_LOG_CALLER( "ShaderBaker" );

    string ShaderBaker::getStemLower( string_view filePath )
    {
        return ShaderBakerInternal::getStemLower( filePath );
    }

    uint64 ShaderBaker::computePermutationHash( const vector<string>& listPermutation )
    {
        if ( listPermutation.empty() )
            return 0;

        // `FOO` 와 `FOO=1` 은 컴파일러에게 같은 것이다. 런타임은 ShaderMacroDefine::parse 로 값 없는
        // define 에 "1" 을 채운 **뒤** 해시하므로, 여기서 원문 그대로 해시하면 같은 퍼뮤테이션이
        // 베이크와 런타임에서 서로 다른 해시가 된다. 그러면 구워둔 변형을 아무도 못 찾는다. 파이프라인
        // XML 은 `SW_FORWARD=1` 처럼 값을 적어 우연히 맞았고, 값이 없는 머티리얼 define 은 모두
        // 어긋나 있었다. 두 오버로드가 같은 문자열을 보도록 여기서 맞춘다.
        vector<string> listSorted;
        listSorted.reserve( listPermutation.size() );
        for ( const string& def : listPermutation )
        {
            if ( def.empty() )
                continue;
            if ( def.find( '=' ) == string::npos )
                listSorted.push_back( def + "=1" );
            else
                listSorted.push_back( def );
        }
        if ( listSorted.empty() )
            return 0;

        std::sort( listSorted.begin(), listSorted.end() );

        uint64 hash{ 14695981039346656037ull }; // FNV-1a 64비트 오프셋 기저값
        for ( const string& def : listSorted )
        {
            if ( def.empty() )
                continue;
            hash = StringUtil::computeHash64( def, false, hash );
        }
        return hash;
    }

    uint64 ShaderBaker::computePermutationHash( const vector<ShaderMacroDefine>& listDefine )
    {
        if ( listDefine.empty() )
            return 0;

        vector<string> listString;
        listString.reserve( listDefine.size() );
        for ( const auto& def : listDefine )
        {
            if ( def._name.empty() )
                continue;
            if ( def._value.empty() )
                listString.push_back( def._name );
            else
                listString.push_back( def._name + "=" + def._value );
        }
        return computePermutationHash( listString );
    }

    string ShaderBaker::computeBinaryFileName( string_view stemLower, ShaderStage stage,
                                               string_view entryPoint, uint64 permHash, string_view ext )
    {
        const string_view stageTag  = getShaderStageInfo( stage )._pTag;
        const string_view defEntry  = getShaderStageInfo( stage )._pEntryPoint;
        const bool        bStdEntry = entryPoint.empty() || StringUtil::equals( entryPoint, defEntry, true );

        string basePart = string( stemLower ) + "_";
        if ( bStdEntry )
            basePart += string( stageTag );
        else
            basePart += StringUtil::toLower( string( entryPoint ).c_str() );

        if ( permHash != 0 )
        {
            StringBuilder<constant::kMaxBuffer16> sb;
            sb.appendFormat( "_%#", Fmt( static_cast<uint32>( permHash & 0xFFFFFFFFu ), Format( 8, Format::Padding::Zero ).hex() ) );
            basePart += sb.view();
        }

        basePart += string( ext );
        return basePart;
    }

    string_view ShaderBaker::getSubfolderForFormat( ShaderTargetFormat format )
    {
        switch ( format )
        {
            case ShaderTargetFormat::DXBC_D3D11:
                return "dx11";
            case ShaderTargetFormat::DXIL_D3D12:
                return "dx12";
            case ShaderTargetFormat::SPIRV_Vulkan:
                return "vulkan";
            case ShaderTargetFormat::SPIRV_OpenGL:
                return "opengl";
            case ShaderTargetFormat::Count:
            default:
                break;
        }
        return "dx12";
    }

    string_view ShaderBaker::getExtensionForFormat( ShaderTargetFormat format )
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
            default:
                break;
        }
        return ".bin";
    }

    ShaderTargetFormat ShaderBaker::getFormatForSubfolder( string_view subfolder )
    {
        if ( subfolder == "dx11" || subfolder == "d3d11" || subfolder == "directx11" )
            return ShaderTargetFormat::DXBC_D3D11;
        if ( subfolder == "dx12" || subfolder == "d3d12" || subfolder == "directx12" )
            return ShaderTargetFormat::DXIL_D3D12;
        if ( subfolder == "vulkan" || subfolder == "vk" || subfolder == "spirv" )
            return ShaderTargetFormat::SPIRV_Vulkan;
        if ( subfolder == "opengl" || subfolder == "gl" )
            return ShaderTargetFormat::SPIRV_OpenGL;
        return ShaderTargetFormat::Count;
    }

    bool ShaderBaker::bakeShader( string_view sourcePath, string_view outputPath, string_view entryPoint,
                                  ShaderStage stage, ShaderTargetFormat targetFormat,
                                  const vector<string>* pListPermutation,
                                  ShaderBakeResult*     pOutResult )
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
            for ( const string& permStr : *pListPermutation )
            {
                if ( permStr.empty() )
                    continue;
                const size_t      eqPos = permStr.find( '=' );
                ShaderMacroDefine def;
                if ( eqPos != string::npos )
                {
                    def._name  = permStr.substr( 0, eqPos );
                    def._value = permStr.substr( eqPos + 1 );
                }
                else
                {
                    def._name  = permStr;
                    def._value = "1";
                }
                desc._listDefine.push_back( std::move( def ) );
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
            SW_LOG_ERROR( "Failed to write baked bytecode to %#", outputPath );
            return false;
        }

        if ( pOutResult != nullptr )
        {
            pOutResult->_byteCodeSize = compileResult._bytecode.size();
            pOutResult->_bSuccess     = SW_TRUE;
        }

        SW_LOG_INFO( "Baked shader '%#' [%#] -> '%#' (%zu bytes)",
                     sourcePath.data(), entryPoint.data(), outputPath.data(), compileResult._bytecode.size() );
        return true;
    }
} // namespace sw
