#include "pch.h"

#include "Engine/Graphics/Shader/ShaderBaker.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Config/EngineData.h"
#include "Engine/Graphics/RenderPass/RenderPipelineResource.h"
#include "Engine/Graphics/Shader/ShaderCompiler.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
    namespace
    {
        struct ShaderBakerInternal
        {
            struct BakeRecipe
            {
                string         _shaderPath;
                string         _entryPoint;
                ShaderStage    _stage;
                vector<string> _listPermutation;
                uint64         _permHash{ 0 };
            };

            static string getStemLower( string_view filePath )
            {
                const string fileName = FileUtil::getFileNamePart( filePath );
                const string stem     = FileUtil::removeExtension( fileName );
                return StringUtil::toLower( stem.c_str() );
            }

            static string findDefaultShaderForPassType( string_view passType, const EngineData& engineData )
            {
                if ( passType == "Shadow" || passType == "DepthPrepass" )
                    return engineData._shaderShadowDepth;
                if ( passType == "ForwardOpaque" || passType == "ForwardOpaqueNoDepthWrite" || passType == "Transparent" )
                    return engineData._shaderForwardLit;
                if ( passType == "GBuffer" )
                    return engineData._shaderGBuffer;
                if ( passType == "GBufferAlbedo" )
                    return engineData._shaderGBufferAlbedo;
                if ( passType == "GBufferNormal" )
                    return engineData._shaderGBufferNormal;
                if ( passType == "Lighting" || passType == "Shading" )
                    return engineData._shaderDeferredLighting;
                if ( passType == "PostBloom" )
                    return engineData._shaderPostBloom;
                if ( passType == "Outline" )
                    return engineData._shaderPostOutlineCommon;
                if ( passType == "Present" )
                    return engineData._shaderFullscreenBlit;
                if ( passType == "SSAO" )
                    return engineData._shaderSsao;
                if ( passType == "TAA" )
                    return engineData._shaderTaa;
                if ( passType == "Tonemap" )
                    return engineData._shaderTonemap;
                return "";
            }

            static void appendRecipeUnique( vector<BakeRecipe>&   outListRecipe,
                                            string_view           shaderPath,
                                            string_view           entryPoint,
                                            ShaderStage           stage,
                                            const vector<string>& listPermutation )
            {
                if ( shaderPath.empty() || entryPoint.empty() )
                    return;

                const uint64 permHash = ShaderBaker::computePermutationHash( listPermutation );
                const string normPath = FileUtil::normalizeSeparators( shaderPath );

                for ( const BakeRecipe& existing : outListRecipe )
                {
                    if ( existing._stage == stage &&
                         existing._permHash == permHash &&
                         existing._entryPoint == entryPoint &&
                         existing._shaderPath == normPath )
                    {
                        return;
                    }
                }

                BakeRecipe recipe;
                recipe._shaderPath      = normPath;
                recipe._entryPoint      = string( entryPoint );
                recipe._stage           = stage;
                recipe._listPermutation = listPermutation;
                recipe._permHash        = permHash;
                outListRecipe.push_back( std::move( recipe ) );
            }

            static void collectAllRecipes( string_view rootDir, vector<BakeRecipe>& outListRecipe )
            {
                EngineData engineData;
                engineData.loadFromResource();

                // 1) RenderPipeline XMLs (pipeline/*.xml)
                vector<string> listXmlFile;
                FileUtil::collectFiles( rootDir, ".xml", listXmlFile, true, true );

                for ( const string& xmlPath : listXmlFile )
                {
                    const string normXml = FileUtil::normalizeSeparators( xmlPath );
                    if ( normXml.find( "pipeline/" ) == string::npos && normXml.find( "pipeline.xml" ) == string::npos )
                        continue;

                    RenderPipelineResource pipelineRes;
                    if ( pipelineRes.loadFromXmlFile( xmlPath ) == false )
                        continue;

                    for ( const RenderGraphPassDesc& pass : pipelineRes.getGraphPass() )
                    {
                        string shaderPath = pass._shaderPath;
                        if ( shaderPath.empty() )
                            shaderPath = findDefaultShaderForPassType( pass._type, engineData );
                        if ( shaderPath.empty() )
                            continue;

                        // Compute Shader
                        if ( pass._computeEntryPoint.empty() == false || pass._type == "Compute" )
                        {
                            const string csEntry = pass._computeEntryPoint.empty() ? "CSMain" : pass._computeEntryPoint;
                            appendRecipeUnique( outListRecipe, shaderPath, csEntry, ShaderStage::Compute, pass._listPermutation );
                        }
                        else
                        {
                            // Vertex Shader
                            const string vsEntry = pass._vertexEntryPoint.empty() ? "VSMain" : pass._vertexEntryPoint;
                            appendRecipeUnique( outListRecipe, shaderPath, vsEntry, ShaderStage::Vertex, pass._listPermutation );

                            // Pixel Shader (Shadow/DepthPrepass passes without pixel output omit PS)
                            if ( pass._type != "Shadow" && pass._type != "DepthPrepass" )
                            {
                                const string psEntry = pass._pixelEntryPoint.empty() ? "PSMain" : pass._pixelEntryPoint;
                                appendRecipeUnique( outListRecipe, shaderPath, psEntry, ShaderStage::Pixel, pass._listPermutation );
                            }

                            // Geometry Shader
                            if ( pass._geometryEntryPoint.empty() == false )
                                appendRecipeUnique( outListRecipe, shaderPath, pass._geometryEntryPoint, ShaderStage::Geometry, pass._listPermutation );

                            // Hull Shader
                            if ( pass._hullEntryPoint.empty() == false )
                                appendRecipeUnique( outListRecipe, shaderPath, pass._hullEntryPoint, ShaderStage::Hull, pass._listPermutation );

                            // Domain Shader
                            if ( pass._domainEntryPoint.empty() == false )
                                appendRecipeUnique( outListRecipe, shaderPath, pass._domainEntryPoint, ShaderStage::Domain, pass._listPermutation );

                            // Mesh Shader
                            if ( pass._meshEntryPoint.empty() == false )
                                appendRecipeUnique( outListRecipe, shaderPath, pass._meshEntryPoint, ShaderStage::Mesh, pass._listPermutation );

                            // Amplification Shader
                            if ( pass._amplificationEntryPoint.empty() == false )
                                appendRecipeUnique( outListRecipe, shaderPath, pass._amplificationEntryPoint, ShaderStage::Amplification, pass._listPermutation );
                        }
                    }
                }

                // 2) Bootstrap / EngineData default shaders
                const vector<string> listEngineShader = {
                    engineData._shaderShadowDepth,
                    engineData._shaderForwardLit,
                    engineData._shaderGBuffer,
                    engineData._shaderGBufferAlbedo,
                    engineData._shaderGBufferNormal,
                    engineData._shaderDeferredLighting,
                    engineData._shaderPostBloom,
                    engineData._shaderPostOutlineCommon,
                    engineData._shaderPostOutlineEngine,
                    engineData._shaderFullscreenBlit,
                    engineData._shaderFullscreenTriangle,
                    engineData._shaderSsao,
                    engineData._shaderTaa,
                    engineData._shaderTonemap,
                    "engine/shaders/sprite2d.hlsl",
                    "common/shaders/computetestgeometry.hlsl" };

                for ( const string& path : listEngineShader )
                {
                    if ( path.empty() )
                        continue;
                    appendRecipeUnique( outListRecipe, path, "VSMain", ShaderStage::Vertex, {} );
                    appendRecipeUnique( outListRecipe, path, "PSMain", ShaderStage::Pixel, {} );
                }

                // Bootstrap Compute Shaders
                const vector<string> listEngineComputeShader = {
                    engineData._shaderGpuCull,
                    "common/shaders/samplecompute.hlsl",
                    "common/shaders/sampleindirect.hlsl" };

                for ( const string& path : listEngineComputeShader )
                {
                    if ( path.empty() )
                        continue;
                    appendRecipeUnique( outListRecipe, path, "CSMain", ShaderStage::Compute, {} );
                }

                // 3) Material assets (.material)
                vector<string> listMaterialFile;
                FileUtil::collectFiles( rootDir, ".material", listMaterialFile, true, true );
                for ( const string& matPath : listMaterialFile )
                {
                    XmlDocument doc;
                    if ( doc.loadFile( matPath ) == false )
                        continue;
                    XmlNode root = doc.root();
                    if ( root.isValid() == false )
                        continue;
                    const utf8* pShaderPath = root.attr( "shaderPath" );
                    if ( pShaderPath == nullptr || *pShaderPath == '\0' )
                        continue;

                    vector<string> listMatDefines;
                    XmlNode        permNode = root.child( "_permutations" );
                    if ( permNode.isValid() )
                    {
                        XmlNode alwaysDefinesNode = permNode.child( "_alwaysDefines" );
                        if ( alwaysDefinesNode.isValid() )
                        {
                            for ( XmlNode item = alwaysDefinesNode.child( "item" ); item.isValid(); item = item.next( "item" ) )
                            {
                                const utf8* pText = item.text();
                                if ( pText != nullptr && *pText != '\0' )
                                    listMatDefines.push_back( pText );
                            }
                        }
                    }

                    appendRecipeUnique( outListRecipe, pShaderPath, "VSMain", ShaderStage::Vertex, listMatDefines );
                    appendRecipeUnique( outListRecipe, pShaderPath, "PSMain", ShaderStage::Pixel, listMatDefines );
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "ShaderBaker" );

    uint64 ShaderBaker::computePermutationHash( const vector<string>& listPermutation )
    {
        if ( listPermutation.empty() )
            return 0;

        vector<string> listSorted = listPermutation;
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
            fixed_string<16> hexBuf;
            std::snprintf( hexBuf.data(), hexBuf.capacity(), "_%08x", static_cast<uint32>( permHash & 0xFFFFFFFFu ) );
            basePart += hexBuf.c_str();
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
            case ShaderTargetFormat::SPIRV_Vulkan:
                return ".spv";
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

        ShaderCompileResult compileResult = ShaderCompiler::compileHLSL( desc );
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
            SW_LOG_ERROR( "Failed to write baked bytecode to %#", outputPath.data() );
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
        vector<ShaderBakerInternal::BakeRecipe> listRecipe;
        ShaderBakerInternal::collectAllRecipes( rootDir, listRecipe );

        uint32 totalBaked = 0;

        // 3) 각 레시피 및 타깃 포맷별로 베이킹
        for ( const ShaderBakerInternal::BakeRecipe& recipe : listRecipe )
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

            const string shaderDir   = normPath.substr( 0, shaderPos + sizeof( "/shaders" ) - 1 );
            const string stemLower   = ShaderBakerInternal::getStemLower( normPath );
            const uint64 sourceMtime = FileUtil::getFileTimestamp( normPath );

            for ( ShaderTargetFormat fmt : listTargetFormat )
            {
                const string_view subfolder = getSubfolderForFormat( fmt );
                const string_view ext       = getExtensionForFormat( fmt );
                const string      outDir    = FileUtil::joinPath( FileUtil::joinPath( shaderDir, "bin" ), subfolder );
                const string      fileName  = computeBinaryFileName( stemLower, recipe._stage, recipe._entryPoint, recipe._permHash, ext );
                const string      outPath   = FileUtil::joinPath( outDir, fileName );

                if ( bForceAll == false && FileUtil::fileExists( outPath ) )
                {
                    const uint64 outMtime = FileUtil::getFileTimestamp( outPath );
                    if ( outMtime >= sourceMtime )
                        continue;
                }

                ShaderBakeResult result{};
                if ( bakeShader( absPath, outPath, recipe._entryPoint, recipe._stage, fmt, &recipe._listPermutation, &result ) )
                {
                    ++totalBaked;
                }
            }
        }

        SW_LOG_INFO( "Shader baking completed: %# binaries generated/updated.", totalBaked );
        return totalBaked;
    }
} // namespace sw
