/**
 * @file ShaderCookRequest.cpp
 * @brief **무엇을 쿠킹할지** 정합니다. 파이프라인 XML 과 머티리얼을 훑어 (셰이더 · 진입점 · define) 목록을 만듭니다.
 * @details 쿠킹하는 일(`Shader/Compile/ShaderCooker.cpp`)과 나누는 이유는 입력이 다르기 때문입니다. 여기 입력은 **에셋**(파이프라인 · 머티리얼)이고
 *          저쪽 입력은 요청 하나입니다. 런타임이 만드는 퍼뮤테이션과 여기서 만드는 요청이 어긋나면 Shipping 에서
 *          매니페스트 미스로 떨어지므로, define 을 합치는 규칙(`mergeDefines`)과 패스 기본 셰이더를 고르는 규칙이
 *          런타임과 같은 자리를 봐야 합니다. 그 대조가 이 파일의 일입니다.
 */
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Renderer/Cook/ShaderCookDriver.h"
#include "Engine/Graphics/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPassTypeInfo.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPipelineAsset.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"
#include "Engine/Graphics/Shader/Compile/ShaderCooker.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
    namespace
    {
        struct ShaderCookRequestInternal
        {
            static void appendRequestUnique( vector<ShaderCookRequest>& outListRequest,
                                             string_view                shaderPath,
                                             string_view                entryPoint,
                                             ShaderStage                stage,
                                             const vector<string>&      listPermutation )
            {
                if ( shaderPath.empty() || entryPoint.empty() )
                    return;

                const uint64 permutationHash = ShaderCooker::computePermutationHash( listPermutation );
                const string normPath        = FileUtil::normalizeSeparators( shaderPath );

                for ( const ShaderCookRequest& existing : outListRequest )
                {
                    if ( existing._stage == stage &&
                         existing._permutationHash == permutationHash &&
                         existing._entryPoint == entryPoint &&
                         existing._shaderPath == normPath )
                        return;
                }

                ShaderCookRequest request;
                request._shaderPath      = normPath;
                request._entryPoint      = string( entryPoint );
                request._stage           = stage;
                request._listPermutation = listPermutation;
                request._permutationHash = permutationHash;
                outListRequest.push_back( std::move( request ) );
            }

            /** @brief 씬 메시를 그리는 패스 하나입니다. 머티리얼과 곱해 변형을 만들 대상입니다. */
            struct MeshPassInfo
            {
                string         _shaderPath;
                string         _vertexEntryPoint;
                string         _pixelEntryPoint;
                vector<string> _listPermutation;
                RenderPassType _passType{ RenderPassType::Invalid };
                bool           _bUsesMaterialShader{ false };
                bool           _bAppliesViewMode{ false };
                bool           _bHasPixelStage{ false };
            };

            /** @brief 메시 패스 하나가 이 셰이더 · define 으로 그릴 때의 VS(그리고 픽셀 스테이지가 있으면 PS) 요청을 더합니다. */
            static void appendMeshPassRequest( vector<ShaderCookRequest>& outListRequest, const MeshPassInfo& passInfo, string_view shaderPath,
                                               const vector<string>& listDefine )
            {
                appendRequestUnique( outListRequest, shaderPath, passInfo._vertexEntryPoint, ShaderStage::Vertex, listDefine );
                if ( passInfo._bHasPixelStage )
                    appendRequestUnique( outListRequest, shaderPath, passInfo._pixelEntryPoint, ShaderStage::Pixel, listDefine );
            }

            /** @brief 머티리얼 하나가 요구하는 셰이더 경로와 **런타임과 같은** define 목록입니다. */
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

            static void collectAllRequests( string_view rootDir, vector<ShaderCookRequest>& outListRequest )
            {
                EngineDefaultAssets engineDefaultAssets;
                if ( engineDefaultAssets.loadFromResource() == false )
                    SW_LOG_WARNING( "Engine data could not be read - cooking with the built-in default passes" );

                vector<MeshPassInfo>        listMeshPass;
                vector<MaterialVariantInfo> listMaterialVariant;

                // 1) 렌더 파이프라인 XML(pipeline/*.xml)
                vector<string> listXmlFile;
                FileUtil::collectFiles( rootDir, ".xml", listXmlFile, true );

                for ( const string& xmlPath : listXmlFile )
                {
                    const string normXml = FileUtil::normalizeSeparators( xmlPath );
                    if ( normXml.find( "pipeline/" ) == string::npos && normXml.find( "pipeline.xml" ) == string::npos )
                        continue;

                    RenderPipelineAsset pipelineResource;
                    if ( pipelineResource.loadFromXmlFile( xmlPath ) == false )
                        continue;

                    for ( const RenderGraphPassDesc& pass : pipelineResource.getGraphPass() )
                    {
                        // 셰이더 경로와 define 은 런타임 PSO 생성(createPsoForPassType)과 **같은 함수**로 정한다. 경로는 XML 의
                        // `_shaderPath`, 비어 있으면 패스 종류 표의 기본 셰이더다. define 은 XML 퍼뮤테이션 + 패스 define(G버퍼의
                        // `SW_PASS_GBUFFER=1` 처럼 C++ 이 얹는 것)이다. 패스 종류는 로드 때 enum 으로 해석한 값을 본다.
                        const RenderPassShaderSelection passShader = selectRenderPassShader( pass._resolvedType, &pass, engineDefaultAssets );
                        const string&                   shaderPath = passShader._shaderPath;
                        if ( shaderPath.empty() )
                            continue;
                        const vector<string>& listPassDefine = passShader._listDefine;

                        // 씬 메시를 그리는 패스는 머티리얼과의 조합까지 쿠킹해야 한다(아래 4단계).
                        if ( FrameRendererUtil::drawsSceneMeshes( pass._resolvedType ) )
                        {
                            MeshPassInfo passInfo;
                            passInfo._shaderPath          = shaderPath;
                            passInfo._vertexEntryPoint    = pass._vertexEntryPoint.empty() ? "VSMain" : pass._vertexEntryPoint;
                            passInfo._pixelEntryPoint     = pass._pixelEntryPoint.empty() ? "PSMain" : pass._pixelEntryPoint;
                            passInfo._listPermutation     = listPassDefine;
                            passInfo._passType            = pass._resolvedType;
                            passInfo._bUsesMaterialShader = FrameRendererUtil::usesMaterialShader( pass._resolvedType );
                            passInfo._bAppliesViewMode    = FrameRendererUtil::appliesViewMode( pass._resolvedType );
                            passInfo._bHasPixelStage      = FrameRendererUtil::hasPixelStage( pass, pipelineResource.getDesc()._listAttachment );
                            listMeshPass.push_back( std::move( passInfo ) );
                        }

                        // 컴퓨트 셰이더
                        if ( pass._computeEntryPoint.empty() == false )
                        {
                            const string& csEntry = pass._computeEntryPoint;
                            appendRequestUnique( outListRequest, shaderPath, csEntry, ShaderStage::Compute, listPassDefine );
                        }
                        else
                        {
                            // 정점 셰이더
                            const string vsEntry = pass._vertexEntryPoint.empty() ? "VSMain" : pass._vertexEntryPoint;
                            appendRequestUnique( outListRequest, shaderPath, vsEntry, ShaderStage::Vertex, listPassDefine );

                            // 픽셀 셰이더. 컬러 출력이 없는 패스(그림자 · 뎁스 프리패스)엔 없다. 런타임과 같은 판정
                            // (`hasPixelStage`, 출력 선언의 RT 수)을 써야 둘이 어긋나지 않는다.
                            if ( FrameRendererUtil::hasPixelStage( pass, pipelineResource.getDesc()._listAttachment ) )
                            {
                                const string psEntry = pass._pixelEntryPoint.empty() ? "PSMain" : pass._pixelEntryPoint;
                                appendRequestUnique( outListRequest, shaderPath, psEntry, ShaderStage::Pixel, listPassDefine );
                            }

                            // 지오메트리 셰이더
                            if ( pass._geometryEntryPoint.empty() == false )
                                appendRequestUnique( outListRequest, shaderPath, pass._geometryEntryPoint, ShaderStage::Geometry, listPassDefine );

                            // 헐 셰이더
                            if ( pass._hullEntryPoint.empty() == false )
                                appendRequestUnique( outListRequest, shaderPath, pass._hullEntryPoint, ShaderStage::Hull, listPassDefine );

                            // 도메인 셰이더
                            if ( pass._domainEntryPoint.empty() == false )
                                appendRequestUnique( outListRequest, shaderPath, pass._domainEntryPoint, ShaderStage::Domain, listPassDefine );

                            // 메시 셰이더
                            if ( pass._meshEntryPoint.empty() == false )
                                appendRequestUnique( outListRequest, shaderPath, pass._meshEntryPoint, ShaderStage::Mesh, listPassDefine );

                            // 앰플리피케이션 셰이더
                            if ( pass._amplificationEntryPoint.empty() == false )
                                appendRequestUnique( outListRequest, shaderPath, pass._amplificationEntryPoint, ShaderStage::Amplification, listPassDefine );
                        }
                    }
                }

                // 2) 패스 종류 표(RenderPassTypeInfo)의 엔진 셰이더. 런타임이 패스 서술 없이도 만드는 PSO 의 셰이더다.
                //    컴퓨트 패스는 CSMain, 나머지는 VSMain · PSMain 을 define 없이 쿠킹한다.
                //    런타임(FrameRenderer::ensurePassResources)은 로드한 파이프라인과 무관하게 표의 **모든** 패스 종류로 엔진 PSO 를 만들고,
                //    씬 메시 패스면 그 위에 머티리얼 · 뷰 모드 변형을 얹는다. 파이프라인에 그 종류가 없으면 서술 없이 표만으로 만든다 —
                //    그 메시 패스도 아래 4) 의 곱에 넣는다. XML 에 나오는 패스만 곱하면 어느 파이프라인에도 없는 종류의 변형이 빠진다.
                for ( uint32 typeIndex = 0; typeIndex < kRenderPassTypeCount; ++typeIndex )
                {
                    const RenderPassType      passType = static_cast<RenderPassType>( typeIndex );
                    const RenderPassTypeInfo& info     = getRenderPassTypeInfo( passType );
                    if ( info._pDefaultShader == nullptr )
                        continue;
                    if ( info.hasFlag( RenderPassTraitFlag::kCompute ) )
                    {
                        appendRequestUnique( outListRequest, engineDefaultAssets.*info._pDefaultShader, "CSMain", ShaderStage::Compute, {} );
                        continue;
                    }
                    if ( FrameRendererUtil::drawsSceneMeshes( passType ) )
                    {
                        const RenderPassShaderSelection passShader = selectRenderPassShader( passType, nullptr, engineDefaultAssets );
                        MeshPassInfo                    passInfo;
                        passInfo._shaderPath          = passShader._shaderPath;
                        passInfo._vertexEntryPoint    = FrameRendererUtil::Entry::kVSMain;
                        passInfo._pixelEntryPoint     = FrameRendererUtil::Entry::kPSMain;
                        passInfo._listPermutation     = passShader._listDefine;
                        passInfo._passType            = passType;
                        passInfo._bUsesMaterialShader = FrameRendererUtil::usesMaterialShader( passType );
                        passInfo._bAppliesViewMode    = FrameRendererUtil::appliesViewMode( passType );
                        passInfo._bHasPixelStage      = FrameRendererUtil::hasPixelStage( passType );
                        listMeshPass.push_back( std::move( passInfo ) );
                    }
                    appendRequestUnique( outListRequest, engineDefaultAssets.*info._pDefaultShader, "VSMain", ShaderStage::Vertex, {} );
                    appendRequestUnique( outListRequest, engineDefaultAssets.*info._pDefaultShader, "PSMain", ShaderStage::Pixel, {} );
                }

                // 패스가 아닌 엔진 · 시험 셰이더.
                const vector<string> listEngineShader = {
                    engineDefaultAssets._shaderFullscreenTriangle,
                    "engine/shaders/sprite2d.hlsl",
                    "engine/shaders/sprite2dlit.hlsl",
                    "common/shaders/provokingvertex.hlsl",
                    "common/shaders/instanceslotprobe.hlsl" };
                for ( const string& path : listEngineShader )
                {
                    appendRequestUnique( outListRequest, path, "VSMain", ShaderStage::Vertex, {} );
                    appendRequestUnique( outListRequest, path, "PSMain", ShaderStage::Pixel, {} );
                }

                const vector<string> listEngineComputeShader = {
                    "common/shaders/samplecompute.hlsl",
                    "common/shaders/computetexturewrite.hlsl",
                    "common/shaders/waterwaveprobe.hlsl" };
                for ( const string& path : listEngineComputeShader )
                {
                    appendRequestUnique( outListRequest, path, "CSMain", ShaderStage::Compute, {} );
                }

                // 3) 머티리얼 에셋(.material)
                vector<string> listMaterialFile;
                FileUtil::collectFiles( rootDir, ".material", listMaterialFile, true );
                for ( const string& matPath : listMaterialFile )
                {
                    // **머티리얼을 직접 읽어 런타임과 같은 define 목록을 얻는다.** 주의: XML 의 `_alwaysDefines` 만
                    // 손으로 긁으면 런타임이 얹는 품질 · SHADER_LOD · usage · 정적 스위치 · 멀티 컴파일이 빠져,
                    // 쿠킹된 변형이 런타임이 **한 번도 요청하지 않는** 해시가 된다. 같은 함수를 부르면 어긋날 자리가 없다.
                    const shared_ptr<Material> material = Material::create();
                    if ( material->loadFromFile( matPath ) == false )
                        continue;
                    if ( material->getShaderPath().empty() )
                        continue;

                    MaterialVariantInfo variant;
                    variant._shaderPath = material->getShaderPath();
                    variant._listDefine = material->getCachedShaderDefines();
                    listMaterialVariant.push_back( variant );

                    appendRequestUnique( outListRequest, variant._shaderPath, "VSMain", ShaderStage::Vertex, variant._listDefine );
                    appendRequestUnique( outListRequest, variant._shaderPath, "PSMain", ShaderStage::Pixel, variant._listDefine );
                    // 정의 없는 변형도 — 머티리얼이 원소 레이아웃을 읽는 자리다(`Material::ensureShaderLayout` 은 define 없이 리플렉션한다). 패스 기본
                    // 셰이더(forwardlit)는 위 1) 에서 이미 쿠킹되지만, 머티리얼만 쓰는 셰이더(지형 · 식생 · 물)는 여기서 쿠킹하지 않으면 Shipping 에서
                    // 매니페스트를 못 찾아 XML 순서 패킹으로 남는다.
                    appendRequestUnique( outListRequest, variant._shaderPath, "VSMain", ShaderStage::Vertex, {} );
                    appendRequestUnique( outListRequest, variant._shaderPath, "PSMain", ShaderStage::Pixel, {} );
                }

                // 4) 패스 x (머티리얼 없음 + 머티리얼) x 뷰 모드: 런타임이 실제로 요구하는 조합
                //
                // FrameRenderer::createMaterialPsoVariant 는 패스 PSO 의 define 위에 머티리얼 define 을, 그 위에 뷰 모드 define 을
                // 얹어 변형 PSO 를 만든다. 런타임이 찾는 것은 셋의 합집합이고, 머티리얼이 없는 배치(ensureMaterialPsos 의 퍼뮤테이션 없는
                // 요청)도 패스 define 위에 뷰 모드를 얹는다. 쿠킹하지 않은 조합은 Shipping 에서 PSO 생성이 실패하고 패스 PSO 로 물러나
                // **조용히 Lit 으로** 그려지거나(뷰 모드) 드로우가 사라진다(머티리얼).
                // 뷰 모드 define 은 런타임과 같은 FrameRendererUtil::findViewModeDefine 에서 얻는다. Wireframe 처럼 래스터라이저 상태만
                // 바꾸는 모드는 define 이 없어 새 바이트코드가 필요 없다.
                for ( const MeshPassInfo& passInfo : listMeshPass )
                {
                    vector<MaterialVariantInfo> listDrawVariant;
                    listDrawVariant.push_back( MaterialVariantInfo{ passInfo._shaderPath, passInfo._listPermutation } );
                    for ( const MaterialVariantInfo& variant : listMaterialVariant )
                    {
                        // 패스가 그리지 않는 머티리얼(외곽선 패스 · 외곽선을 켜지 않은 머티리얼)은 런타임도 변형을 만들지 않는다.
                        if ( FrameRendererUtil::drawsMaterialInPass( passInfo._passType, &variant._listDefine ) == false )
                            continue;
                        // 머티리얼 셰이더를 쓰는 패스만 .hlsl 을 갈아탄다. 그림자 · 뎁스는 자기 셰이더에
                        // define 만 얹는다(usesMaterialShader 와 같은 규칙).
                        const string& shaderPath = passInfo._bUsesMaterialShader ? variant._shaderPath : passInfo._shaderPath;
                        listDrawVariant.push_back( MaterialVariantInfo{ shaderPath, mergeDefines( passInfo._listPermutation, variant._listDefine ) } );
                    }

                    for ( const MaterialVariantInfo& drawVariant : listDrawVariant )
                    {
                        if ( drawVariant._shaderPath.empty() )
                            continue;
                        appendMeshPassRequest( outListRequest, passInfo, drawVariant._shaderPath, drawVariant._listDefine );
                        if ( passInfo._bAppliesViewMode == false )
                            continue;
                        for ( uint32 viewModeIndex = 0; viewModeIndex < static_cast<uint32>( RenderViewMode::Count ); ++viewModeIndex )
                        {
                            const utf8* pViewModeDefine = FrameRendererUtil::findViewModeDefine( static_cast<RenderViewMode>( viewModeIndex ) );
                            if ( pViewModeDefine == nullptr )
                                continue;
                            appendMeshPassRequest( outListRequest, passInfo, drawVariant._shaderPath,
                                                   mergeDefines( drawVariant._listDefine, { string( pViewModeDefine ) } ) );
                        }
                    }
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "ShaderCooker" );

    void ShaderCookDriver::collectAllRequests( string_view rootDir, vector<ShaderCookRequest>& outListRequest )
    {
        ShaderCookRequestInternal::collectAllRequests( rootDir, outListRequest );
    }
} // namespace sw
