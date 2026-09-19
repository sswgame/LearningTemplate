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
#include "Engine/Graphics/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPipelineResource.h"
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
        // 베이크와 런타임에서 서로 다른 해시가 된다 — 구워둔 변형을 아무도 못 찾는다. 파이프라인
        // XML 은 `SW_FORWARD=1` 처럼 값을 적어 우연히 맞았고, 값이 없는 머티리얼 define 은 전부
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

        uint64 hash{ 14695981039346656037ull }; // FNV-1a 64-bit offset basis
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
        const string_view stageTag  = getStageTag( stage );
        const string_view defEntry  = getDefaultEntryPointForStage( stage );
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

    string_view ShaderBaker::getDefaultEntryPointForStage( ShaderStage stage )
    {
        switch ( stage )
        {
            case ShaderStage::Vertex:
                return "VSMain";
            case ShaderStage::Pixel:
                return "PSMain";
            case ShaderStage::Compute:
                return "CSMain";
            case ShaderStage::Geometry:
                return "GSMain";
            case ShaderStage::Hull:
                return "HSMain";
            case ShaderStage::Domain:
                return "DSMain";
            case ShaderStage::Mesh:
                return "MSMain";
            case ShaderStage::Amplification:
                return "ASMain";
            case ShaderStage::Count:
            default:
                break;
        }
        return "Main";
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
            // 바꾸는 실수가 나기 쉽다 — 같이 묶어 같아야 한다는 것을 드러낸다.
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

    string_view ShaderBaker::getStageTag( ShaderStage stage )
    {
        switch ( stage )
        {
            case ShaderStage::Vertex:
                return "vs";
            case ShaderStage::Pixel:
                return "ps";
            case ShaderStage::Compute:
                return "cs";
            case ShaderStage::Geometry:
                return "gs";
            case ShaderStage::Hull:
                return "hs";
            case ShaderStage::Domain:
                return "ds";
            case ShaderStage::Mesh:
                return "ms";
            case ShaderStage::Amplification:
                return "as";
            case ShaderStage::Count:
            default:
                break;
        }
        return "vs";
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

    uint32 ShaderBaker::bakeAllShaders( string_view        resourceRoot,
                                        ShaderTargetFormat targetFormat,
                                        bool               bForceAll )
    {
        string rootDir = string( resourceRoot );
        if ( rootDir.empty() )
        {
            rootDir = ResourceUtil::getRootFolderPath();
            if ( rootDir.empty() )
                rootDir = "Resource";
        }

        if ( FileUtil::directoryExists( rootDir ) == false )
        {
            SW_LOG_ERROR( "Resource root directory does not exist: %#", rootDir.c_str() );
            return 0;
        }

        SW_LOG_INFO( "Starting batch shader bake across all domains in '%#'...", rootDir.c_str() );

        // 1) 컴파일 대상 타깃 포맷 목록 구성
        vector<ShaderTargetFormat> listTargetFormat;
        if ( targetFormat == ShaderTargetFormat::Count )
        {
            listTargetFormat.push_back( ShaderTargetFormat::DXBC_D3D11 );
            listTargetFormat.push_back( ShaderTargetFormat::DXIL_D3D12 );
            listTargetFormat.push_back( ShaderTargetFormat::SPIRV_Vulkan );
            listTargetFormat.push_back( ShaderTargetFormat::SPIRV_OpenGL );
        }
        else
        {
            listTargetFormat.push_back( targetFormat );
        }

        // 2) 렌더 파이프라인 에셋 및 엔진 데이터 기반 레시피 일괄 수집
        vector<ShaderBakeRecipe> listRecipe;
        ShaderBaker::collectAllRecipes( rootDir, listRecipe );

        uint32 totalBaked = 0;

        // 리플렉션은 RHI 폴더마다 파일 하나로 모은다 — 셰이더마다 사이드카를 두면 팩 엔트리와
        // 압축 해제가 셰이더 수만큼 늘어난다(상용 엔진의 셰이더 라이브러리와 같은 이유).
        unordered_map<string, ShaderReflectionLibrary::EntryMap> mapManifest;
        uint32                                                   contractViolationCount{ 0 };

        // 3) 각 레시피 및 타깃 포맷별로 베이킹
        for ( const ShaderBakeRecipe& recipe : listRecipe )
        {
            string absPath;
            if ( FileUtil::fileExists( recipe._shaderPath ) )
                absPath = recipe._shaderPath;
            else
                absPath = ResourceUtil::getResourcePath( recipe._shaderPath );

            if ( FileUtil::fileExists( absPath ) == false )
                continue;

            const string normPath  = FileUtil::normalizeSeparators( absPath );
            const size_t shaderPos = normPath.find( "/shaders/" );
            if ( shaderPos == string::npos )
                continue;

            const string shaderDir = normPath.substr( 0, shaderPos + sizeof( "/shaders" ) - 1 );
            const string stemLower = ShaderBakerInternal::getStemLower( normPath );

            for ( ShaderTargetFormat fmt : listTargetFormat )
            {
                const string_view subfolder = getSubfolderForFormat( fmt );
                const string_view ext       = getExtensionForFormat( fmt );
                const string      outDir    = FileUtil::joinPath( FileUtil::joinPath( shaderDir, "bin" ), subfolder );
                const string      fileName  = computeBinaryFileName( stemLower, recipe._stage, recipe._entryPoint, recipe._permHash, ext );
                const string      outPath   = FileUtil::joinPath( outDir, fileName );

                // **파일 시간이 아니라 내용 해시로 판정한다.** 이 저장소는 구운 바이너리까지 커밋하므로
                // `git pull` 이 소스와 산출물의 mtime 을 임의의 순서로 덮어쓴다 — 소스가 바뀌었는데도
                // "산출물이 더 새것" 이 되어 그대로 넘어간다. 실제로 `forwardlit` 이 라이트 버퍼 이전
                // 바이너리로 커밋됐고, Vulkan 만 다른 그림을 내는 것을 백엔드 버그로 오인했다.
                const bool bUpToDate = ( bForceAll == false ) && FileUtil::fileExists( outPath ) &&
                                       isBakedOutputCurrent( outDir, normPath );

                if ( bUpToDate == false )
                {
                    ShaderBakeResult result{};
                    if ( bakeShader( absPath, outPath, recipe._entryPoint, recipe._stage, fmt, &recipe._listPermutation, &result ) )
                        ++totalBaked;
                }

                // 새로 구웠든 이미 최신이든 매니페스트에는 항상 넣는다 — 바이너리만 최신이고
                // 리플렉션이 빠지면 런타임이 조용히 폴백하고, 배포 빌드에서는 그대로 실패한다.
                if ( mapManifest[outDir].find( fileName ) == mapManifest[outDir].end() )
                {
                    vector<uint8> bytecode;
                    if ( FileUtil::readFile( outPath, bytecode ) && bytecode.empty() == false )
                    {
                        ShaderReflectionData reflection = ShaderReflection::reflect( bytecode, fmt );
                        // 구운 바이너리를 계약과 대조한다 — 셰이더 헤더와 백엔드 상수가 한쪽만 바뀌면 여기서 이름·숫자로 드러난다.
                        contractViolationCount += ShaderBindingContract::validate( reflection, fmt, outPath );
                        mapManifest[outDir].emplace( fileName, std::move( reflection ) );
                    }
                }
            }
        }

        // 4) RHI 폴더별 리플렉션 매니페스트 기록
        for ( const auto& manifestPair : mapManifest )
        {
            ShaderReflectionLibrary::save( manifestPair.second, manifestPair.first );
            ShaderBaker::writeBakeStamp( manifestPair.first );
        }
        ShaderReflectionLibrary::clearCache();

        if ( contractViolationCount > 0 )
            SW_LOG_ERROR( "바인딩 계약 위반 %#건 — 위 [바인딩 계약] 로그를 보고 셰이더 선언이나 bindingslots.hlsli 를 고치십시오.", contractViolationCount );
        SW_LOG_INFO( "Shader baking completed: %# binaries generated/updated.", totalBaked );
        return totalBaked;
    }
} // namespace sw
