/**
 * @file ShaderBakeRecipe.cpp
 * @brief **무엇을 구울지** 정한다 — 파이프라인 XML 과 머티리얼을 훑어 (셰이더 · 진입점 · define) 목록을 만든다.
 * @details 굽는 일(`ShaderBaker.cpp`)과 나누는 이유는 입력이 다르기 때문이다. 여기 입력은 **에셋**(파이프라인 · 머티리얼)이고
 *          저쪽 입력은 레시피 하나다. 런타임이 만드는 퍼뮤테이션과 여기서 만드는 레시피가 어긋나면 Shipping 에서
 *          매니페스트 미스로 떨어지므로, define 을 합치는 규칙(`mergeDefines`)과 패스 기본 셰이더를 고르는 규칙이
 *          런타임과 같은 자리를 봐야 한다 — 그 대조가 이 파일의 일이다.
 */
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Config/EngineData.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPipelineResource.h"
#include "Engine/Graphics/Shader/Compile/ShaderBaker.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
    namespace
    {
        struct ShaderBakeRecipeInternal
        {
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

            static void appendRecipeUnique( vector<ShaderBakeRecipe>& outListRecipe,
                                            string_view               shaderPath,
                                            string_view               entryPoint,
                                            ShaderStage               stage,
                                            const vector<string>&     listPermutation )
            {
                if ( shaderPath.empty() || entryPoint.empty() )
                    return;

                const uint64 permHash = ShaderBaker::computePermutationHash( listPermutation );
                const string normPath = FileUtil::normalizeSeparators( shaderPath );

                for ( const ShaderBakeRecipe& existing : outListRecipe )
                {
                    if ( existing._stage == stage &&
                         existing._permHash == permHash &&
                         existing._entryPoint == entryPoint &&
                         existing._shaderPath == normPath )
                        return;
                }

                ShaderBakeRecipe recipe;
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

            static void collectAllRecipes( string_view rootDir, vector<ShaderBakeRecipe>& outListRecipe )
            {
                EngineData engineData;
                engineData.loadFromResource();

                vector<MeshPassInfo>        listMeshPass;
                vector<MaterialVariantInfo> listMaterialVariant;

                // 1) RenderPipeline XMLs (pipeline/*.xml)
                vector<string> listXmlFile;
                FileUtil::collectFiles( rootDir, ".xml", listXmlFile, true );

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
                            passInfo._bHasPixelStage      = FrameRendererUtil::hasPixelStage( pass, pipelineRes.getDesc()._listAttachment );
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

                            // 픽셀 셰이더 — 컬러 출력이 없는 패스(그림자·뎁스 프리패스)엔 없다. 예전엔 여기서 타입
                            // **문자열**을 비교했다. 런타임은 출력 선언(RT 수)으로 판정하므로 둘이 어긋날 수 있었다.
                            if ( FrameRendererUtil::hasPixelStage( pass, pipelineRes.getDesc()._listAttachment ) )
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
                    "common/shaders/computetestgeometry.hlsl",
                    "common/shaders/provokingvertex.hlsl" };

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
                    engineData._shaderMeshMorph,
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
                FileUtil::collectFiles( rootDir, ".material", listMaterialFile, true );
                for ( const string& matPath : listMaterialFile )
                {
                    // **머티리얼을 직접 읽어 런타임과 같은 define 목록을 얻는다.** 예전엔 여기서 XML 의
                    // `_alwaysDefines` 만 손으로 긁었다 — 런타임은 거기에 품질·SHADER_LOD·usage·정적
                    // 스위치·멀티컴파일까지 얹으므로, 구운 변형은 런타임이 **한 번도 요청하지 않는**
                    // 해시였다. 같은 함수를 부르면 어긋날 자리가 없다.
                    const shared_ptr<Material> material = Material::create();
                    if ( material->loadFromFile( matPath ) == false )
                        continue;
                    if ( material->getShaderPath().empty() )
                        continue;

                    MaterialVariantInfo variant;
                    variant._shaderPath = material->getShaderPath();
                    variant._listDefine = material->getCachedShaderDefines();
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

                        // 5) 그 위의 **뷰 모드** 축 — 런타임이 요구하는 조합은 (패스 x 머티리얼 x 뷰 모드) 다.
                        //
                        // Wireframe 은 래스터라이저 상태만 바꾸므로 새 바이트코드가 필요 없지만 Unlit 은
                        // 퍼뮤테이션이다. 굽지 않으면 Shipping 에서 그 PSO 생성이 실패하고 패스 PSO 로
                        // 물러나 **조용히 Lit 으로 그려진다** — 값은 바뀌는데 화면은 그대로인, 이 기능을
                        // 처음 막아 두게 만든 바로 그 증상이다.
                        // 뷰 모드를 받는 패스만이다(FrameRendererUtil::appliesViewMode 와 같은 규칙 —
                        // 그림자·뎁스는 머티리얼 셰이더를 안 쓰므로 _bUsesMaterialShader 로 갈린다).
                        if ( passInfo._bUsesMaterialShader )
                        {
                            const vector<string> listUnlit = mergeDefines( listCombined, { string( kViewModeUnlitDefine ) } );
                            appendRecipeUnique( outListRecipe, shaderPath, passInfo._vertexEntryPoint, ShaderStage::Vertex, listUnlit );
                            if ( passInfo._bHasPixelStage )
                                appendRecipeUnique( outListRecipe, shaderPath, passInfo._pixelEntryPoint, ShaderStage::Pixel, listUnlit );
                        }
                    }

                    // 머티리얼이 **없는** 배치의 뷰 모드 변형 — 런타임은 퍼뮤테이션 없는 배치에도 (패스 define + Unlit) 을
                    // 만든다(ensureMaterialPsos 의 bHasPlain). 언리얼은 모든 메시에 머티리얼(기본 머티리얼)이 있어 이 축이
                    // 없지만, 여기는 머티리얼 없는 메시가 패스 PSO 로 그려지므로 그 변형도 굽는다. 위의 머티리얼 루프
                    // 안에만 두면 이 조합이 빠져 Shipping 에서 그 메시만 Lit 으로 물러난다.
                    if ( passInfo._bUsesMaterialShader )
                    {
                        const vector<string> listPlainUnlit = mergeDefines( passInfo._listPermutation, { string( kViewModeUnlitDefine ) } );
                        appendRecipeUnique( outListRecipe, passInfo._shaderPath, passInfo._vertexEntryPoint, ShaderStage::Vertex, listPlainUnlit );
                        if ( passInfo._bHasPixelStage )
                            appendRecipeUnique( outListRecipe, passInfo._shaderPath, passInfo._pixelEntryPoint, ShaderStage::Pixel, listPlainUnlit );
                    }
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "ShaderBaker" );

    void ShaderBaker::collectAllRecipes( string_view rootDir, vector<ShaderBakeRecipe>& outListRecipe )
    {
        ShaderBakeRecipeInternal::collectAllRecipes( rootDir, outListRecipe );
    }
} // namespace sw
