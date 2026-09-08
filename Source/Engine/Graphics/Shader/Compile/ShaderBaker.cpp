#include "pch.h"

#include "Engine/Graphics/Shader/Compile/ShaderBaker.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
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

            /** @brief 씬 메시를 그리는 패스 하나 — 머티리얼과 곱해 변형을 만들 대상이다. */
            struct MeshPassInfo
            {
                string         _shaderPath;
                string         _vertexEntryPoint;
                string         _pixelEntryPoint;
                vector<string> _listPermutation;
                bool           _bUsesMaterialShader{ false };
                bool           _bHasPixelStage{ false };
            };

            /** @brief 머티리얼 하나가 요구하는 셰이더 경로와 **런타임과 동일한** define 목록. */
            struct MaterialVariantInfo
            {
                string         _shaderPath;
                vector<string> _listDefine;
            };

            /** @brief 두 define 목록을 합칩니다(중복 제거, 앞쪽 우선). */
            static vector<string> mergeDefines( const vector<string>& listLeft, const vector<string>& listRight )
            {
                vector<string> listMerged = listLeft;
                for ( const string& define : listRight )
                {
                    if ( define.empty() )
                        continue;
                    bool bFound = false;
                    for ( const string& existing : listMerged )
                    {
                        if ( existing == define )
                        {
                            bFound = true;
                            break;
                        }
                    }
                    if ( bFound == false )
                        listMerged.push_back( define );
                }
                return listMerged;
            }

            /**
             * @brief `bin/<rhi>/bake.stamp` 에 셰이더 소스의 **내용 해시**를 적습니다.
             * @details 쿠커(CookAssets.py)가 "지금 팩에 넣으려는 바이너리가 지금 이 소스에서 나온
             *          것인가" 를 파일 시간이 아니라 내용으로 확인하기 위한 것이다. 파일 시간은
             *          `git clone` 이 전부 체크아웃 시각으로 덮어써서 비교 자체가 무의미해진다.
             *          형식은 한 줄에 `<FNV-1a 64 16자리 hex> <shaders/ 기준 상대 경로>` 다.
             * @param binDirectory 매니페스트를 쓴 폴더 (`<domain>/shaders/bin/<rhi>`)
             */
            static void writeBakeStamp( string_view binDirectory )
            {
                // <domain>/shaders/bin/<rhi> → <domain>/shaders
                const string rhiDir     = FileUtil::normalizeSeparators( binDirectory );
                const string parentDir  = FileUtil::getDirectoryPart( rhiDir );
                const string shadersDir = FileUtil::getDirectoryPart( parentDir );
                if ( shadersDir.empty() )
                    return;

                vector<string> listSource;
                FileUtil::collectFiles( shadersDir, ".hlsl", listSource, true, true );
                FileUtil::collectFiles( shadersDir, ".hlsli", listSource, true, true );

                vector<string> listLine;
                listLine.reserve( listSource.size() );
                for ( const string& sourcePath : listSource )
                {
                    const string normSource = FileUtil::normalizeSeparators( sourcePath );
                    if ( normSource.find( "/bin/" ) != string::npos )
                        continue;
                    if ( normSource.size() <= shadersDir.size() + 1 )
                        continue;

                    vector<uint8> bytes;
                    if ( FileUtil::readFile( normSource, bytes ) == false )
                        continue;

                    const uint64 hash    = StringUtil::computeHash64( reinterpret_cast<const utf8*>( bytes.data() ),
                                                                      bytes.size(), false );
                    const string relPath = StringUtil::toLower( normSource.substr( shadersDir.size() + 1 ).c_str() );

                    StringBuilder<constant::kMaxBuffer256> sb;
                    sb.appendFormat( "%#", Fmt( hash, Format( 16, Format::Padding::Zero ).hex() ) );
                    sb.append( ' ' ).append( relPath );
                    listLine.push_back( string( sb.c_str(), sb.size() ) );
                }

                std::sort( listLine.begin(), listLine.end() );

                string text = "SWBAKE 1\n";
                for ( const string& line : listLine )
                {
                    text += line;
                    text += "\n";
                }

                const string stampPath = FileUtil::joinPath( rhiDir, "bake.stamp" );
                if ( FileUtil::writeTextFile( stampPath, text ) == false )
                    SW_LOG_WARNING( "베이크 스탬프 쓰기 실패: %#", stampPath.c_str() );
            }

            static void collectAllRecipes( string_view rootDir, vector<BakeRecipe>& outListRecipe )
            {
                EngineData engineData;
                engineData.loadFromResource();

                vector<MeshPassInfo>        listMeshPass;
                vector<MaterialVariantInfo> listMaterialVariant;

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

                        // 씬 메시를 그리는 패스는 머티리얼과의 조합까지 구워야 한다 (아래 4단계).
                        if ( FrameRendererUtil::drawsSceneMeshes( pass._resolvedType ) )
                        {
                            MeshPassInfo passInfo;
                            passInfo._shaderPath          = shaderPath;
                            passInfo._vertexEntryPoint    = pass._vertexEntryPoint.empty() ? "VSMain" : pass._vertexEntryPoint;
                            passInfo._pixelEntryPoint     = pass._pixelEntryPoint.empty() ? "PSMain" : pass._pixelEntryPoint;
                            passInfo._listPermutation     = pass._listPermutation;
                            passInfo._bUsesMaterialShader = FrameRendererUtil::usesMaterialShader( pass._resolvedType );
                            passInfo._bHasPixelStage      = pass._type != "Shadow" && pass._type != "DepthPrepass";
                            listMeshPass.push_back( std::move( passInfo ) );
                        }

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
                    engineData._shaderInstanceAnim,
                    engineData._shaderInstanceSort,
                    "common/shaders/samplecompute.hlsl",
                    "common/shaders/sampleindirect.hlsl",
                    "common/shaders/computetexturewrite.hlsl" };

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
                    // **머티리얼을 직접 읽어 런타임과 같은 define 목록을 얻는다.** 예전엔 여기서 XML 의
                    // `_alwaysDefines` 만 손으로 긁었다 — 런타임은 거기에 품질·SHADER_LOD·usage·정적
                    // 스위치·멀티컴파일까지 얹으므로, 구운 변형은 런타임이 **한 번도 요청하지 않는**
                    // 해시였다. 같은 함수를 부르면 어긋날 자리가 없다.
                    Material material;
                    if ( material.loadFromFile( matPath ) == false )
                        continue;
                    if ( material.getShaderPath().empty() )
                        continue;

                    MaterialVariantInfo variant;
                    variant._shaderPath = material.getShaderPath();
                    variant._listDefine = material.getCachedShaderDefines();
                    listMaterialVariant.push_back( variant );

                    appendRecipeUnique( outListRecipe, variant._shaderPath, "VSMain", ShaderStage::Vertex, variant._listDefine );
                    appendRecipeUnique( outListRecipe, variant._shaderPath, "PSMain", ShaderStage::Pixel, variant._listDefine );
                }

                // 4) 패스 x 머티리얼 — 런타임이 실제로 요구하는 조합
                //
                // FrameRenderer::createMaterialPsoVariant 는 패스 PSO 의 define 위에 머티리얼 define 을
                // 얹어 변형 PSO 를 만든다. 즉 런타임이 찾는 것은 패스 단독도 머티리얼 단독도 아닌
                // **둘의 합집합**이다. 위의 1)/3) 만 구워두면 그림자·불투명 패스가 머티리얼 메시를
                // 그릴 때마다 미스가 나고, Shipping 은 런타임 컴파일이 없어 드로우가 통째로 사라진다.
                for ( const MeshPassInfo& passInfo : listMeshPass )
                {
                    for ( const MaterialVariantInfo& variant : listMaterialVariant )
                    {
                        // 머티리얼 셰이더를 쓰는 패스만 .hlsl 을 갈아탄다 — 그림자·뎁스는 자기 셰이더에
                        // define 만 얹는다(usesMaterialShader 와 같은 규칙).
                        const string& shaderPath = passInfo._bUsesMaterialShader ? variant._shaderPath : passInfo._shaderPath;
                        if ( shaderPath.empty() )
                            continue;

                        const vector<string> listCombined = mergeDefines( passInfo._listPermutation, variant._listDefine );
                        appendRecipeUnique( outListRecipe, shaderPath, passInfo._vertexEntryPoint, ShaderStage::Vertex, listCombined );
                        if ( passInfo._bHasPixelStage )
                            appendRecipeUnique( outListRecipe, shaderPath, passInfo._pixelEntryPoint, ShaderStage::Pixel, listCombined );
                    }
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

    uint64 ShaderBaker::getSharedHeaderTimestamp()
    {
        // getOrCompile 이 PSO 마다 부르므로 프로세스당 한 번만 훑는다. 실행 중 헤더를 고치는
        // 라이브 리로드는 어차피 명시적으로 재컴파일하는 경로를 따로 탄다.
        static const uint64 s_timestamp = []() -> uint64
        {
            const string rootDir = ResourceUtil::getRootFolderPath();
            if ( rootDir.empty() )
                return 0;
            vector<string> listHeader;
            FileUtil::collectFiles( rootDir, ".hlsli", listHeader, true, true );
            uint64 newest = 0;
            for ( const string& headerPath : listHeader )
                newest = MathUtil::max( newest, FileUtil::getFileTimestamp( headerPath ) );
            return newest;
        }();
        return s_timestamp;
    }

    uint64 ShaderBaker::computeEffectiveSourceTimestamp( string_view absShaderPath )
    {
        const uint64 sourceMtime = FileUtil::getFileTimestamp( absShaderPath );
        if ( sourceMtime == 0 )
            return 0;
        return MathUtil::max( sourceMtime, getSharedHeaderTimestamp() );
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

        // 리플렉션은 RHI 폴더마다 파일 하나로 모은다 — 셰이더마다 사이드카를 두면 팩 엔트리와
        // 압축 해제가 셰이더 수만큼 늘어난다(상용 엔진의 셰이더 라이브러리와 같은 이유).
        unordered_map<string, ShaderReflectionLibrary::EntryMap> mapManifest;
        uint32                                                   contractViolationCount{ 0 };

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
            const uint64 sourceMtime = computeEffectiveSourceTimestamp( normPath );

            for ( ShaderTargetFormat fmt : listTargetFormat )
            {
                const string_view subfolder = getSubfolderForFormat( fmt );
                const string_view ext       = getExtensionForFormat( fmt );
                const string      outDir    = FileUtil::joinPath( FileUtil::joinPath( shaderDir, "bin" ), subfolder );
                const string      fileName  = computeBinaryFileName( stemLower, recipe._stage, recipe._entryPoint, recipe._permHash, ext );
                const string      outPath   = FileUtil::joinPath( outDir, fileName );

                bool bUpToDate = false;
                if ( bForceAll == false && FileUtil::fileExists( outPath ) )
                {
                    const uint64 outMtime = FileUtil::getFileTimestamp( outPath );
                    bUpToDate             = ( outMtime >= sourceMtime );
                }

                if ( bUpToDate == false )
                {
                    ShaderBakeResult result{};
                    if ( bakeShader( absPath, outPath, recipe._entryPoint, recipe._stage, fmt, &recipe._listPermutation, &result ) )
                    {
                        ++totalBaked;
                    }
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
            ShaderBakerInternal::writeBakeStamp( manifestPair.first );
        }
        ShaderReflectionLibrary::clearCache();

        if ( contractViolationCount > 0 )
            SW_LOG_ERROR( "바인딩 계약 위반 %#건 — 위 [바인딩 계약] 로그를 보고 셰이더 선언이나 bindingslots.hlsli 를 고치십시오.", contractViolationCount );
        SW_LOG_INFO( "Shader baking completed: %# binaries generated/updated.", totalBaked );
        return totalBaked;
    }
} // namespace sw
