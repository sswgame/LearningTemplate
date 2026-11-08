#include "pch.h"

#include "Engine/Renderer/Pipeline/RenderPipelineAsset.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Asset/AssetFormat.h"
#include "Engine/Asset/AssetManager.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Material/RenderBlendPreset.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Renderer/Pipeline/GBufferLayout.h"
#include "Engine/Renderer/Pipeline/RenderPassAsset.h"
#include "Engine/Renderer/Pipeline/RenderPassInputSignature.h"
#include "Engine/Renderer/Pipeline/RenderPassTypeInfo.h"
#include "Engine/Serialization/Format/ReflectedXMLFile.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

namespace sw
{
    bool RenderPipelineAsset::loadFromXMLFile( const AssetID& assetID )
    {
        const string_view assetRelativePath = assetID.view();
        _desc                               = {};
        if ( ReflectedXMLFile::loadDesc( assetID, _desc ) == false )
            return false;
        return finishLoad( assetRelativePath );
    }

    bool RenderPipelineAsset::loadFromDiskFile( string_view diskPath )
    {
        _desc = {};
        if ( ReflectedXMLFile::loadDescFromDiskFile( diskPath, _desc ) == false )
            return false;
        return finishLoad( diskPath );
    }

    bool RenderPipelineAsset::finishLoad( string_view sourcePath )
    {
        if ( mergeRenderPassRefs( sourcePath ) == false )
            return false;
        // 검증 오류가 하나라도 있으면 로드 실패다. 선언하지 않은 첨부 · 모르는 패스 타입이 렌더러까지 가면 렌더러가 코드의 이름 · 포맷으로 짐작해야 한다.
        const uint32 issueCount = validate( sourcePath );
        if ( issueCount > 0 )
        {
            SW_LOG_ERROR_ID( "RenderPipelineAsset.PipelineIssueNotLoaded", "'%#' has %# pipeline issue(s) - not loaded", sourcePath, issueCount );
            return false;
        }
        SW_LOG_INFO_ID( "RenderPipelineAsset.LoadedModelAttachmentsPasses", "Loaded '%#' (model=%#, attachments=%#, passes=%#)", _desc._name, _desc._shadingModel, _desc._listAttachment.size(),
                        _desc._listPass.size() );
        return true;
    }

    bool RenderPipelineAsset::hasFramePass( RenderPassType type ) const
    {
        for ( const RenderGraphPassDesc& pass : _desc._listFramePass )
        {
            if ( pass._type == type )
                return true;
        }
        return false;
    }

    bool RenderPipelineAsset::mergeRenderPassRefs( string_view sourcePath )
    {
        // 묶음의 원소가 앞, 이 파이프라인의 원소가 뒤다 — 세 목록이 같은 규칙이다. 이름이 겹치면 덮어쓰지 않고 검증이 알린다.
        vector<RenderPassAttachment>    listAttachment;
        vector<RenderGraphResourceDesc> listResource;
        vector<RenderGraphPassDesc>     listFramePass;
        for ( const string& passRef : _desc._listRenderPassRef )
        {
            RenderPassAsset renderPass;
            if ( renderPass.loadFromXMLFile( AssetID{ passRef } ) == false )
            {
                SW_LOG_ERROR_ID( "RenderPipelineAsset.RenderPassRefNotLoaded", "[%#] render pass '%#' could not be loaded", sourcePath, passRef );
                return false;
            }
            const RenderPassDesc& refDesc = renderPass.getDesc();
            listAttachment.insert( listAttachment.end(), refDesc._listAttachment.begin(), refDesc._listAttachment.end() );
            listResource.insert( listResource.end(), refDesc._listResource.begin(), refDesc._listResource.end() );
            listFramePass.insert( listFramePass.end(), refDesc._listFramePass.begin(), refDesc._listFramePass.end() );
        }
        listAttachment.insert( listAttachment.end(), std::make_move_iterator( _desc._listAttachment.begin() ), std::make_move_iterator( _desc._listAttachment.end() ) );
        listResource.insert( listResource.end(), std::make_move_iterator( _desc._listResource.begin() ), std::make_move_iterator( _desc._listResource.end() ) );
        listFramePass.insert( listFramePass.end(), std::make_move_iterator( _desc._listFramePass.begin() ), std::make_move_iterator( _desc._listFramePass.end() ) );
        _desc._listAttachment = std::move( listAttachment );
        _desc._listResource   = std::move( listResource );
        _desc._listFramePass  = std::move( listFramePass );
        return true;
    }

    namespace
    {
        /**
         * @brief 파이프라인 검증 규칙들입니다. 규칙마다 찾은 문제를 오류 로그로 남기고 그 수를 돌려줍니다.
         * @details `RenderPipelineAsset::validate` 가 순서대로 부르고 합을 냅니다. 패스의 해석 값(`_resolved*`)은 `resolveAndCheckPassAttachments` 가
         *          채우고, 그 뒤 규칙들이 그 값을 읽습니다 — 실행도 같은 해석을 쓴다.
         */
        struct RenderPipelineValidationInternal
        {
            static const RenderPassAttachment* findAttachment( const RenderPipelineDesc& desc, string_view name )
            {
                for ( const RenderPassAttachment& attachment : desc._listAttachment )
                {
                    if ( attachment._name == name )
                        return &attachment;
                }
                return nullptr;
            }

            static bool isDepthAttachment( const RenderPassAttachment* pAttachment )
            {
                return pAttachment != nullptr && pAttachment->_format == constant::render::kDepthStencilFormat;
            }

            /** @brief 1) 첨부: 포맷이 렌더 타깃이 될 수 있는가, 이름이 겹치지 않는가, 나눗수 · 역할 표기가 맞는가. */
            static uint32 checkAttachments( const RenderPipelineDesc& desc, string_view sourcePath )
            {
                uint32 issueCount{ 0 };
                for ( uint32 attachmentIndex = 0; attachmentIndex < desc._listAttachment.size(); ++attachmentIndex )
                {
                    const RenderPassAttachment& attachment = desc._listAttachment[attachmentIndex];
                    if ( isRHIFormatRenderable( attachment._format ) == false )
                    {
                        // 이름은 맞지만 첨부가 될 수 없는 포맷(Unknown · 블록 압축 · R32G32B32_FLOAT) — 트랜지언트를 만들 때 백엔드가 실패한다.
                        SW_LOG_ERROR_ID( "RenderPipelineAsset.AttachmentFormatCannotRender", "[%#] attachment '%#': format '%#' cannot be a render target", sourcePath, attachment._name, getEnumName( attachment._format ) );
                        ++issueCount;
                    }
                    for ( uint32 otherIndex = 0; otherIndex < attachmentIndex; ++otherIndex )
                    {
                        // 대소문자만 달라도 겹침이다 — 실행 때 첨부는 `hashed_string` 이름(대소문자 무시)으로 찾는다.
                        if ( StringUtil::equals( desc._listAttachment[otherIndex]._name, attachment._name, true ) )
                        {
                            SW_LOG_ERROR_ID( "RenderPipelineAsset.AttachmentNameDuplicated", "[%#] attachment '%#' name is duplicated", sourcePath, attachment._name );
                            ++issueCount;
                            break;
                        }
                    }
                    if ( isSupportedResolutionDivisor( attachment._resolutionDivisor ) == false )
                    {
                        SW_LOG_ERROR_ID( "RenderPipelineAsset.AttachmentResolutionDivisorOnlyAccepted", "[%#] attachment '%#': _resolutionDivisor %# - only 1, 2 and 4 are accepted", sourcePath, attachment._name, attachment._resolutionDivisor );
                        ++issueCount;
                    }
                    // 역할 선언은 아는 글이어야 한다. 모르는 글을 조용히 이름 · 포맷 규칙으로 넘기면 선언한 사람이 왜 안 걸리는지 모른다.
                    RenderPassInputRole declaredRole{ RenderPassInputRole::Invalid };
                    if ( attachment._role.empty() == false && tryParseRenderPassInputRole( attachment._role, declaredRole ) == false )
                    {
                        SW_LOG_ERROR_ID( "RenderPipelineAsset.AttachmentUnknownRoleMust", "[%#] attachment '%#': unknown role '%#' - must be one of SourceColor, SceneDepth, GBufferAlbedo, GBufferNormal, GBufferMaterial, "
                                                                                          "GBufferCustomData, ShadowMap, AmbientOcclusion, BloomBright, SceneColorCopy, SceneDepthCopy, Velocity, Distortion, ContactShadow",
                                         sourcePath, attachment._name, attachment._role );
                        ++issueCount;
                    }
                }
                return issueCount;
            }

            /** @brief 셰이딩 방식이 아는 글이고 패스 구성과 맞는가. 이름만 Deferred 인 파이프라인을 막는다. */
            static uint32 checkShadingModel( const RenderPipelineDesc& desc, string_view sourcePath )
            {
                const auto hasPassType = [&desc]( RenderPassType type )
                {
                    for ( const RenderGraphPassDesc& pass : desc._listPass )
                    {
                        if ( pass._type == type )
                            return true;
                    }
                    return false;
                };
                const bool bForward  = desc._shadingModel == "Forward";
                const bool bDeferred = desc._shadingModel == "Deferred";
                if ( bForward == false && bDeferred == false && desc._shadingModel != "Custom" )
                {
                    SW_LOG_ERROR_ID( "RenderPipelineAsset.ShadingModelUnknown", "[%#] _shadingModel '%#' - must be Forward, Deferred or Custom", sourcePath, desc._shadingModel );
                    return 1;
                }
                const bool bHasGBuffer = hasPassType( RenderPassType::GBuffer );
                if ( ( bForward && bHasGBuffer ) || ( bDeferred && ( bHasGBuffer == false || hasPassType( RenderPassType::Lighting ) == false ) ) )
                {
                    SW_LOG_ERROR_ID( "RenderPipelineAsset.ShadingModelPassMismatch", "[%#] _shadingModel '%#' does not match its passes - Deferred needs GBuffer and Lighting passes, Forward has no GBuffer pass", sourcePath,
                                     desc._shadingModel );
                    return 1;
                }
                return 0;
            }

            /** @brief 패스의 칸과 블렌드 선언을 PSO 값(`_resolvedRenderState`)으로 합칩니다. 받아 줄 수 없는 블렌드 선언의 수를 돌려줍니다. */
            static uint32 resolvePassRenderState( RenderGraphPassDesc& pass, string_view sourcePath )
            {
                uint32 issueCount{ 0 };
                pass._resolvedRenderState                                      = RHIRenderState{};
                static_cast<RHIFixedRenderState&>( pass._resolvedRenderState ) = pass;
                if ( pass._blendPreset.empty() == false && pass._listBlendTarget.empty() == false )
                {
                    SW_LOG_ERROR_ID( "RenderPipelineAsset.PassBlendPresetAndTargets", "[%#] pass '%#': _blendPreset and _listBlendTarget go separately - write one of them", sourcePath, pass._name );
                    ++issueCount;
                }
                if ( pass._blendPreset.empty() == false )
                {
                    const RenderBlendPreset* const pPreset = RenderBlendPresetTable::find( pass._blendPreset );
                    if ( pPreset != nullptr )
                    {
                        pass._resolvedRenderState.setAllBlendTargets( *pPreset );
                    }
                    else
                    {
                        SW_LOG_ERROR_ID( "RenderPipelineAsset.PassBlendPresetUnknown", "[%#] pass '%#': _blendPreset '%#' is not in %#", sourcePath, pass._name, pass._blendPreset, RenderBlendPresetTable::kAssetPath );
                        ++issueCount;
                    }
                }
                const size_t targetCount = pass._listBlendTarget.size();
                // 스왑체인 출력은 컬러 출력 목록에 들지 않는다 — 그 패스는 타깃이 하나다.
                const size_t colorCount = VectorUtil::contains( pass._listOutput, string( kSwapchainOutputName ) ) ? 1u : pass._listResolvedColorOutput.size();
                if ( targetCount > 1 && targetCount != colorCount )
                {
                    SW_LOG_ERROR_ID( "RenderPipelineAsset.PassBlendTargetCount", "[%#] pass '%#': %# blend targets for %# color outputs - write one (for all) or one per output", sourcePath, pass._name,
                                     static_cast<uint32>( targetCount ), static_cast<uint32>( colorCount ) );
                    ++issueCount;
                }
                for ( size_t targetIndex = 0; targetIndex < targetCount && targetIndex < kMaxColorAttachments; ++targetIndex )
                {
                    const RHIBlendTargetState& target  = pass._listBlendTarget[targetIndex];
                    const utf8* const          pReason = target.findInvalidReason();
                    if ( pReason != nullptr )
                    {
                        SW_LOG_ERROR_ID( "RenderPipelineAsset.PassBlendTargetInvalid", "[%#] pass '%#': blend target %#: %#", sourcePath, pass._name, static_cast<uint32>( targetIndex ), pReason );
                        ++issueCount;
                    }
                    pass._resolvedRenderState._arrBlendTarget[targetIndex] = target;
                }
                if ( targetCount == 1 )
                    pass._resolvedRenderState.setAllBlendTargets( pass._listBlendTarget[0] );
                return issueCount;
            }

            /**
             * @brief 3-8) 저장하지 않는 첨부(`_storeOp` DontCare)는 쓰는 패스가 하나이고 읽는 패스가 없어야 한다. 그렇지 않으면 다음 패스가 정의되지 않은 값을 읽는다.
             */
            static uint32 checkAttachmentStoreOps( const RenderPipelineDesc& desc, string_view sourcePath )
            {
                uint32 issueCount{ 0 };
                for ( const RenderPassAttachment& attachment : desc._listAttachment )
                {
                    if ( attachment._storeOp != RHIRenderPassStoreOp::DontCare )
                        continue;
                    uint32 useCount{ 0 };
                    bool   bRead{ false };
                    for ( const RenderGraphPassDesc& pass : desc._listPass )
                    {
                        bRead = bRead || VectorUtil::contains( pass._listInput, attachment._name );
                        if ( VectorUtil::contains( pass._listOutput, attachment._name ) || pass._depthAttachment == attachment._name )
                            ++useCount;
                    }
                    if ( bRead || useCount > 1 )
                    {
                        SW_LOG_ERROR_ID( "RenderPipelineAsset.AttachmentDiscardedButUsed", "[%#] attachment '%#': _storeOp DontCare leaves its contents undefined, but a pass reads it or more than one pass opens it", sourcePath, attachment._name );
                        ++issueCount;
                    }
                }
                return issueCount;
            }

            /**
             * @brief 패스의 렌더 상태 조합을 실행이 받아 줄 수 있는가. XML 값이 곧 PSO 값이라, 실행과 어긋나면 무시하지 않고 오류다.
             * @details 깊이 테스트는 그 패스 실행이 깊이를 붙일 때만 켤 수 있고(표의 kDepthTest), 깊이를 시험하지 않으면 붙이지 않는다 — 어긋나면 PSO 와 렌더 패스의 깊이 형식이 달라 백엔드 검증 오류가 난다.
             *          풀스크린 삼각형에는 앞뒤 의미가 없어 컬링이 화면을 비운다. 반투명 패스의 블렌드를 끄는 것은 고를 수 있지만 대개 실수라 경고한다.
             */
            static uint32 checkPassRenderState( RenderPipelineDesc& desc, string_view sourcePath )
            {
                uint32 issueCount{ 0 };
                for ( RenderGraphPassDesc& pass : desc._listPass )
                {
                    const RenderPassTypeInfo& info   = getRenderPassTypeInfo( pass._type );
                    const bool                bDepth = pass._depthAttachment.empty() == false;
                    issueCount += resolvePassRenderState( pass, sourcePath );
                    if ( info.hasFlag( RenderPassTraitFlag::kDrawsSceneMeshes ) == false && pass._cullMode != RHICullMode::None )
                    {
                        SW_LOG_ERROR_ID( "RenderPipelineAsset.PassFullscreenCull", "[%#] pass '%#' draws a fullscreen triangle - _cullMode must be None, not '%#'", sourcePath, pass._name, getEnumName( pass._cullMode ) );
                        ++issueCount;
                    }
                    if ( pass._bEnableDepthTest && ( bDepth == false || info.hasFlag( RenderPassTraitFlag::kDepthTest ) == false ) )
                    {
                        SW_LOG_ERROR_ID( "RenderPipelineAsset.PassDepthTestNoDepth", "[%#] pass '%#': _bEnableDepthTest needs a _depthAttachment and a pass type that binds depth (%#)", sourcePath, pass._name, getEnumName( pass._type ) );
                        ++issueCount;
                    }
                    if ( pass._bEnableDepthTest == false && bDepth )
                    {
                        SW_LOG_ERROR_ID( "RenderPipelineAsset.PassDepthWithoutTest", "[%#] pass '%#' binds _depthAttachment '%#' without _bEnableDepthTest - remove the attachment or turn the test on", sourcePath, pass._name, pass._depthAttachment );
                        ++issueCount;
                    }
                    if ( pass._bEnableDepthWrite && pass._bEnableDepthTest == false )
                    {
                        SW_LOG_ERROR_ID( "RenderPipelineAsset.PassDepthWriteNoTest", "[%#] pass '%#': _bEnableDepthWrite needs _bEnableDepthTest", sourcePath, pass._name );
                        ++issueCount;
                    }
                    // 같은 규칙을 머티리얼을 합친 값에도 건다(`RHIFixedRenderState::findInvalidReason`). 깊이 쓰기는 위가 알렸다.
                    const utf8* const pStateReason = pass._bEnableDepthWrite && pass._bEnableDepthTest == false ? nullptr : pass.findInvalidReason();
                    if ( pStateReason != nullptr )
                    {
                        SW_LOG_ERROR_ID( "RenderPipelineAsset.PassRenderStateInvalid", "[%#] pass '%#': %#", sourcePath, pass._name, pStateReason );
                        ++issueCount;
                    }
                    if ( pass._resolvedRenderState.isAnyBlendEnabled() == false && info.hasFlag( RenderPassTraitFlag::kBlend ) )
                        SW_LOG_WARNING_ID( "RenderPipelineAsset.PassBlendOff", "[%#] pass '%#': blending is off on a %# pass - its colors overwrite the scene", sourcePath, pass._name, getEnumName( pass._type ) );
                }
                return issueCount;
            }

            static const RenderGraphResourceDesc* findResource( const RenderPipelineDesc& desc, string_view name )
            {
                for ( const RenderGraphResourceDesc& resource : desc._listResource )
                {
                    if ( StringUtil::equals( resource._name, name, true ) )
                        return &resource;
                }
                return nullptr;
            }

            /**
             * @brief 프레임 그래프(`_listResource` · `_listFramePass`)가 실행과 맞는가.
             * @details 자원: 이름이 겹치지 않고 첨부와도 겹치지 않으며 종류가 Persistent · External 이다(Transient 는 첨부다).
             *          패스: 타입이 프레임 패스이고 한 번만 나오며(실행이 종류마다 한 번 모든 뷰를 돈다), 입출력이 선언한 자원이고, 이름이 뷰 패스와 겹치지 않는다.
             *          셰이더 · 진입점 · 퍼뮤테이션 · 깊이 첨부 칸은 비어 있어야 한다 — 셰이더는 표(`RenderPassTypeInfo`)의 것이고 쿠커 · 엔진 PSO 가 같은 표를 본다. 적어도 실행이 읽지 않으면 XML 이 거짓말을 한다.
             */
            static uint32 checkFrameGraph( const RenderPipelineDesc& desc, string_view sourcePath )
            {
                uint32     issueCount{ 0 };
                const auto report = [&]( const utf8* pID, string_view name, const utf8* pReason )
                {
                    fixed_string<constant::buffer::kMax512> text;
                    formatstring( text.data(), text.capacity(), "[%#] frame graph '%#': %#", sourcePath, name, pReason );
                    Logger::writeLogGlobal( LogLevel::Error, SW_LOG_TAG, text.c_str(), __FILE__, __LINE__, pID );
                    ++issueCount;
                };
                for ( size_t resourceIndex = 0; resourceIndex < desc._listResource.size(); ++resourceIndex )
                {
                    const RenderGraphResourceDesc& resource = desc._listResource[resourceIndex];
                    if ( resource._name.empty() )
                        report( "RenderPipelineAsset.FrameResourceNoName", resource._name, "a resource needs a _name" );
                    if ( resource._kind == RenderGraphResourceKind::Transient )
                        report( "RenderPipelineAsset.FrameResourceTransient", resource._name, "_kind must be Persistent or External - transient images are attachments" );
                    if ( findAttachment( desc, resource._name ) != nullptr )
                        report( "RenderPipelineAsset.FrameResourceIsAttachment", resource._name, "the name is also an attachment" );
                    for ( size_t otherIndex = 0; otherIndex < resourceIndex; ++otherIndex )
                    {
                        if ( StringUtil::equals( desc._listResource[otherIndex]._name, resource._name, true ) )
                        {
                            report( "RenderPipelineAsset.FrameResourceDuplicated", resource._name, "the resource is declared twice" );
                            break;
                        }
                    }
                }
                for ( size_t passIndex = 0; passIndex < desc._listFramePass.size(); ++passIndex )
                {
                    const RenderGraphPassDesc& pass = desc._listFramePass[passIndex];
                    if ( isFramePassType( pass._type ) == false )
                        report( "RenderPipelineAsset.FramePassType", pass._name, "_type must be a frame pass type (DepthHistory, OcclusionTiles, VFXSimulation, InstanceAnimation, MeshMorph, MeshSkin, "
                                                                                 "GPUCull, LightGrid, VolumetricFog, ProbeUpdate, CanvasMarker)" );
                    const bool bTableOwnsShader = pass._shaderPath.empty() && pass._vertexEntryPoint.empty() && pass._pixelEntryPoint.empty() && pass._computeEntryPoint.empty() &&
                                                  pass._geometryEntryPoint.empty() && pass._hullEntryPoint.empty() && pass._domainEntryPoint.empty() && pass._listPermutation.empty();
                    if ( bTableOwnsShader == false )
                        report( "RenderPipelineAsset.FramePassShader", pass._name, "a frame pass takes its shader from the pass type table - leave shader, entry point and permutation fields empty" );
                    if ( pass._depthAttachment.empty() == false || pass._minPostQuality != 0 )
                        report( "RenderPipelineAsset.FramePassViewField", pass._name, "_depthAttachment and _minPostQuality are view pass fields" );
                    // 컴퓨트 큐는 드로우 · 렌더 타깃 상태를 모른다 — 표의 컴퓨트 종류만 비동기 큐에 둘 수 있다(깊이 히스토리는 풀스크린 드로우다).
                    if ( pass._queue == RHIQueueType::AsyncCompute && getRenderPassTypeInfo( pass._type ).hasFlag( RenderPassTraitFlag::kCompute ) == false )
                        report( "RenderPipelineAsset.FramePassAsyncNotCompute", pass._name, "_queue AsyncCompute needs a compute pass type - this type draws on the graphics queue" );
                    for ( const string& name : pass._listInput )
                    {
                        if ( findResource( desc, name ) == nullptr )
                            report( "RenderPipelineAsset.FramePassInputNotResource", pass._name, "an input is not in _listResource" );
                    }
                    for ( const string& name : pass._listOutput )
                    {
                        if ( findResource( desc, name ) == nullptr )
                            report( "RenderPipelineAsset.FramePassOutputNotResource", pass._name, "an output is not in _listResource" );
                    }
                    for ( size_t otherIndex = 0; otherIndex < passIndex; ++otherIndex )
                    {
                        const RenderGraphPassDesc& other = desc._listFramePass[otherIndex];
                        if ( other._name == pass._name )
                            report( "RenderPipelineAsset.FramePassNameDuplicated", pass._name, "the frame pass is declared twice" );
                        else if ( other._type == pass._type )
                            report( "RenderPipelineAsset.FramePassTypeDuplicated", pass._name, "the pass type is declared twice - a frame pass already covers every view" );
                    }
                    for ( const RenderGraphPassDesc& viewPass : desc._listPass )
                    {
                        if ( viewPass._name == pass._name )
                            report( "RenderPipelineAsset.FramePassNameIsViewPass", pass._name, "a view pass has the same name" );
                    }
                }
                return issueCount;
            }

            /** @brief 선택 스테이지 칸과 컴퓨트 패스의 선언이 실행과 맞는가. 컬러 출력 하나, 깊이 형식이 아닌 입력 최대 둘(RW 슬롯 — 에뮬 백엔드의 슬롯이 셋이다), 고른 셰이더, 블렌드 없음. 진입점 칸은 컴퓨트 패스만 쓴다. */
            static uint32 checkComputePasses( const RenderPipelineDesc& desc, string_view sourcePath )
            {
                static constexpr size_t kMaxInputCount = 2;
                uint32                  issueCount{ 0 };
                const auto              report = [&]( const utf8* pID, const RenderGraphPassDesc& pass, const utf8* pReason )
                {
                    fixed_string<constant::buffer::kMax512> text;
                    formatstring( text.data(), text.capacity(), "[%#] pass '%#': %#", sourcePath, pass._name, pReason );
                    Logger::writeLogGlobal( LogLevel::Error, SW_LOG_TAG, text.c_str(), __FILE__, __LINE__, pID );
                    ++issueCount;
                };
                for ( const RenderGraphPassDesc& pass : desc._listPass )
                {
                    const bool bHasOptionalStage = pass._geometryEntryPoint.empty() == false || pass._hullEntryPoint.empty() == false || pass._domainEntryPoint.empty() == false;
                    if ( pass._hullEntryPoint.empty() != pass._domainEntryPoint.empty() )
                        report( "RenderPipelineAsset.PassHullNeedsDomain", pass, "_hullEntryPoint and _domainEntryPoint go together" );
                    // 머티리얼 변형은 머티리얼 셰이더의 정점 출력으로 만들어져 패스 셰이더의 스테이지와 신호가 맞지 않는다.
                    if ( bHasOptionalStage && ( pass._type == RenderPassType::Compute || getRenderPassTypeInfo( pass._type ).hasFlag( RenderPassTraitFlag::kUsesMaterialShader ) ) )
                        report( "RenderPipelineAsset.PassOptionalStageNotAllowed", pass, "geometry, hull and domain stages need a pass that draws with its own shader (not a material shader, not a Compute pass)" );
                    if ( pass._type != RenderPassType::Compute )
                    {
                        if ( pass._computeEntryPoint.empty() == false )
                            report( "RenderPipelineAsset.PassComputeEntryOnCompute", pass, "_computeEntryPoint is only for a Compute pass" );
                        continue;
                    }
                    if ( pass._shaderPath.empty() )
                        report( "RenderPipelineAsset.PassComputeNeedsShader", pass, "a Compute pass needs _shaderPath - there is no default compute shader" );
                    if ( pass._listOutput.size() != 1 || pass._listResolvedColorOutput.size() != 1 )
                        report( "RenderPipelineAsset.PassComputeOneOutput", pass, "a Compute pass writes exactly one color attachment (not Swapchain, not depth)" );
                    if ( pass._listInput.size() > kMaxInputCount )
                        report( "RenderPipelineAsset.PassComputeTooManyInputs", pass, "a Compute pass reads at most two attachments" );
                    for ( const string& inputName : pass._listInput )
                    {
                        if ( isDepthAttachment( findAttachment( desc, inputName ) ) )
                            report( "RenderPipelineAsset.PassComputeDepthInput", pass, "a Compute pass cannot bind a depth format as a RW texture - read a SceneDepthCopy instead" );
                        if ( VectorUtil::contains( pass._listOutput, inputName ) )
                            report( "RenderPipelineAsset.PassComputeInputIsOutput", pass, "a Compute pass cannot read the attachment it writes" );
                    }
                    if ( pass._resolvedRenderState.isAnyBlendEnabled() )
                        report( "RenderPipelineAsset.PassComputeBlend", pass, "a Compute pass has no blend state" );
                }
                return issueCount;
            }

            /** @brief 2-0) 패스 이름은 하나뿐이어야 한다 — 이름이 패스의 열쇠라 뒤의 것은 버려진다. */
            static uint32 checkPassNamesUnique( const RenderPipelineDesc& desc, string_view sourcePath )
            {
                uint32 issueCount{ 0 };
                for ( size_t passIndex = 0; passIndex < desc._listPass.size(); ++passIndex )
                {
                    for ( size_t earlierIndex = 0; earlierIndex < passIndex; ++earlierIndex )
                    {
                        if ( desc._listPass[earlierIndex]._name == desc._listPass[passIndex]._name )
                        {
                            SW_LOG_ERROR_ID( "RenderPipelineAsset.PassDeclaredTwiceName", "[%#] pass '%#' is declared twice - the name is the pass key, so the later one is dropped", sourcePath,
                                             desc._listPass[passIndex]._name );
                            ++issueCount;
                            break;
                        }
                    }
                }
                return issueCount;
            }

            /** @brief 패스 하나의 타입 · 뎁스 · 입출력 역할을 해석해 `_resolved*` 에 채웁니다. 역할은 첨부의 선언(`_role`)이 먼저, 그다음 원본 이름 · 포맷이다. */
            static void resolvePass( const RenderPipelineDesc& desc, RenderGraphPassDesc& pass )
            {
                pass._resolvedDepthAttachment =
                    pass._depthAttachment.empty() ? hashed_string{} : hashed_string( pass._depthAttachment.c_str() );
                pass._listResolvedInput.clear();
                pass._listResolvedOutput.clear();
                pass._listResolvedColorOutput.clear();
                auto resolveAttachment = [&desc]( const string& attachmentName ) -> RenderGraphPassDesc::ResolvedAttachment
                {
                    const RenderPassAttachment*             pAttachment = findAttachment( desc, attachmentName );
                    RenderGraphPassDesc::ResolvedAttachment resolved{};
                    resolved._attachment = hashed_string( attachmentName.c_str() );
                    resolved._role       = static_cast<uint8>(
                        resolveRenderPassInputRole( attachmentName, isDepthAttachment( pAttachment ), pAttachment != nullptr ? string_view( pAttachment->_role ) : string_view{} ) );
                    return resolved;
                };
                for ( const string& inputName : pass._listInput )
                {
                    pass._listResolvedInput.push_back( resolveAttachment( inputName ) );
                }
                for ( const string& outputName : pass._listOutput )
                {
                    pass._listResolvedOutput.emplace_back( outputName.c_str() );
                    const RenderPassAttachment* pOutput = findAttachment( desc, outputName );
                    if ( pOutput != nullptr && isDepthAttachment( pOutput ) == false )
                        pass._listResolvedColorOutput.push_back( resolveAttachment( outputName ) );
                }
            }

            /** @brief 2-1) 패스 하나의 타입 표기가 해석되는가, 입출력이 선언된 첨부를 가리키는가. */
            static uint32 checkPassTypeAndAttachments( const RenderPipelineDesc& desc, const RenderGraphPassDesc& pass, string_view sourcePath )
            {
                uint32 issueCount{ 0 };
                if ( isViewPassType( pass._type ) == false )
                {
                    SW_LOG_ERROR_ID( "RenderPipelineAsset.PassUnknownTypeNot", "[%#] pass '%#': type '%#' cannot be a view pass - write a view RenderPassType (frame pass types go in _listFramePass, internal slots nowhere)",
                                     sourcePath, pass._name, getEnumName( pass._type ) );
                    ++issueCount;
                }
                // 컴퓨트 구현은 표가 가진 종류만 고를 수 있다 — 없는 구현을 적으면 실행이 읽지 않는 칸이다.
                if ( pass._implementation == RenderPassExecutionMode::Compute && getRenderPassTypeInfo( pass._type ).hasFlag( RenderPassTraitFlag::kComputeImplementation ) == false )
                {
                    SW_LOG_ERROR_ID( "RenderPipelineAsset.PassNoComputeImplementation", "[%#] pass '%#': type '%#' has no compute implementation - _implementation must be Pixel", sourcePath, pass._name,
                                     getEnumName( pass._type ) );
                    ++issueCount;
                }
                // 후처리 머티리얼의 자리는 그 패스의 칸이다 — 다른 패스가 적으면 실행이 읽지 않고, 그 패스가 비우면 그릴 블렌더블을 고를 수 없다. 셰이더는 머티리얼이 준다.
                const bool bPostMaterialPass = pass._type == RenderPassType::PostProcessMaterial;
                if ( bPostMaterialPass != ( pass._blendableLocation != PostProcessBlendableLocation::None ) )
                {
                    SW_LOG_ERROR_ID( "RenderPipelineAsset.PassBlendableLocation", "[%#] pass '%#' (%#): _blendableLocation %# - PostProcessMaterial passes name one location, other passes leave it None", sourcePath,
                                     pass._name, getEnumName( pass._type ), getEnumName( pass._blendableLocation ) );
                    ++issueCount;
                }
                if ( bPostMaterialPass && pass._shaderPath.empty() == false )
                {
                    SW_LOG_ERROR_ID( "RenderPipelineAsset.PassPostMaterialShader", "[%#] pass '%#': a PostProcessMaterial pass draws the shaders of its blendable materials - leave _shaderPath empty", sourcePath, pass._name );
                    ++issueCount;
                }
                // 뷰 그래프는 뷰마다 다시 돌고 병렬로 기록한다 — 비동기 큐는 프레임 패스만 받는다(포크 · 조인을 뷰마다 되풀이할 자리가 없다).
                if ( pass._queue != RHIQueueType::Graphics )
                {
                    SW_LOG_ERROR_ID( "RenderPipelineAsset.PassAsyncNotFramePass", "[%#] pass '%#': _queue AsyncCompute is only for frame passes (_listFramePass)", sourcePath, pass._name );
                    ++issueCount;
                }
                for ( const string& inputName : pass._listInput )
                {
                    if ( findAttachment( desc, inputName ) == nullptr )
                    {
                        SW_LOG_ERROR_ID( "RenderPipelineAsset.PassInputNotAttachments", "[%#] pass '%#': input '%#' is not in _attachments", sourcePath, pass._name, inputName );
                        ++issueCount;
                    }
                }
                for ( const string& outputName : pass._listOutput )
                {
                    // 스왑체인은 파이프라인이 선언하는 첨부가 아니라 디바이스가 주는 백버퍼다. 포맷이 대상마다 달라 PSO 를 대상 포맷마다 만드는
                    // 출력 패스(kOutputPass)만 쓸 수 있다 — 다른 패스가 쓰면 PSO 의 렌더 타깃 포맷을 정할 근거가 없다(짐작하지 않는다).
                    if ( outputName == kSwapchainOutputName )
                    {
                        if ( getRenderPassTypeInfo( pass._type ).hasFlag( RenderPassTraitFlag::kOutputPass ) == false )
                        {
                            SW_LOG_ERROR_ID( "RenderPipelineAsset.PassOnlyOutputPasses", "[%#] pass '%#': only output passes (Present, Canvas) can write the Swapchain (type '%#')", sourcePath, pass._name, getEnumName( pass._type ) );
                            ++issueCount;
                        }
                        continue;
                    }
                    if ( findAttachment( desc, outputName ) == nullptr )
                    {
                        SW_LOG_ERROR_ID( "RenderPipelineAsset.PassOutputNotAttachments", "[%#] pass '%#': output '%#' is not in _attachments", sourcePath, pass._name, outputName );
                        ++issueCount;
                    }
                }
                return issueCount;
            }

            /**
             * @brief 2-2) 뎁스 첨부: 비어 있는 것은 "일부러 뎁스를 안 쓴다" 는 뜻이라 정상이다. 다만 이름을 적었으면 그 첨부가 실재하고 뎁스 포맷이어야 한다.
             * @details 컬러 첨부를 뎁스로 바인딩하면 렌더 패스가 통째로 비호환이 된다. 뎁스를 출력에 적어 놓고 바인딩하지 않는 것도 앞뒤가 맞지 않는다.
             */
            static uint32 checkPassDepth( const RenderPipelineDesc& desc, const RenderGraphPassDesc& pass, string_view sourcePath )
            {
                uint32 issueCount{ 0 };
                if ( pass._depthAttachment.empty() == false )
                {
                    const RenderPassAttachment* pDepthAttachment = findAttachment( desc, pass._depthAttachment );
                    if ( pDepthAttachment == nullptr )
                    {
                        SW_LOG_ERROR_ID( "RenderPipelineAsset.PassDepthAttachmentNot", "[%#] pass '%#': depth attachment '%#' is not in _attachments",
                                         sourcePath, pass._name, pass._depthAttachment );
                        ++issueCount;
                    }
                    else if ( isDepthAttachment( pDepthAttachment ) == false )
                    {
                        SW_LOG_ERROR_ID( "RenderPipelineAsset.PassDepthAttachmentFormat", "[%#] pass '%#': depth attachment '%#' has format '%#' - it must be a depth format",
                                         sourcePath, pass._name, pass._depthAttachment, getEnumName( pDepthAttachment->_format ) );
                        ++issueCount;
                    }
                    return issueCount;
                }
                for ( const string& outputName : pass._listOutput )
                {
                    if ( isDepthAttachment( findAttachment( desc, outputName ) ) == false )
                        continue;
                    SW_LOG_ERROR_ID( "RenderPipelineAsset.PassDepthAttachmentDeclared", "[%#] pass '%#': depth attachment '%#' is declared as an output but _depthAttachment is empty "
                                                                                        "- as is, it draws without depth",
                                     sourcePath, pass._name, outputName );
                    ++issueCount;
                }
                return issueCount;
            }

            /** @brief 2) 패스마다 해석 값을 채우고 타입 · 입출력 · 뎁스를 검사합니다. */
            static uint32 resolveAndCheckPassAttachments( RenderPipelineDesc& desc, string_view sourcePath )
            {
                uint32 issueCount{ 0 };
                for ( RenderGraphPassDesc& pass : desc._listPass )
                {
                    resolvePass( desc, pass );
                    issueCount += checkPassTypeAndAttachments( desc, pass, sourcePath );
                    issueCount += checkPassDepth( desc, pass, sourcePath );
                }
                return issueCount;
            }

            /** @brief 2-2) 모듈 셰이더 목록의 경로는 비지 않고 겹치지 않아야 한다 — 쿠커가 경로마다 한 번 굽고, 런타임이 이 목록으로 모듈 패스의 프로그램을 거른다. */
            static uint32 checkModuleShaderList( const RenderPipelineDesc& desc, string_view sourcePath )
            {
                uint32 issueCount{ 0 };
                for ( size_t shaderIndex = 0; shaderIndex < desc._listExtensionShader.size(); ++shaderIndex )
                {
                    if ( desc._listExtensionShader[shaderIndex].empty() )
                    {
                        SW_LOG_ERROR_ID( "RenderPipelineAsset.ListExtensionShaderEmpty", "[%#] _listExtensionShader[%#] is empty", sourcePath, static_cast<uint32>( shaderIndex ) );
                        ++issueCount;
                        continue;
                    }
                    for ( size_t earlierIndex = 0; earlierIndex < shaderIndex; ++earlierIndex )
                    {
                        if ( desc._listExtensionShader[earlierIndex] == desc._listExtensionShader[shaderIndex] )
                        {
                            SW_LOG_ERROR_ID( "RenderPipelineAsset.ListExtensionShaderListsTwice", "[%#] _listExtensionShader lists '%#' twice", sourcePath, desc._listExtensionShader[shaderIndex] );
                            ++issueCount;
                            break;
                        }
                    }
                }
                return issueCount;
            }

            /** @brief 3) 컬러 첨부가 여러 개인 패스는 MRT 로 묶인다. 포맷은 서로 달라도 되지만 개수에는 한계가 있다. */
            static uint32 checkColorOutputLimit( const RenderPipelineDesc& desc, string_view sourcePath )
            {
                uint32 issueCount{ 0 };
                for ( const RenderGraphPassDesc& pass : desc._listPass )
                {
                    uint32 colorCount{ 0 };
                    for ( const string& outputName : pass._listOutput )
                    {
                        const RenderPassAttachment* pAttachment = findAttachment( desc, outputName );
                        if ( pAttachment == nullptr )
                            continue;
                        if ( pAttachment->_format != RHIFormat::D24_UNORM_S8_UINT )
                            ++colorCount;
                    }
                    if ( colorCount > kMaxColorAttachments )
                    {
                        SW_LOG_ERROR_ID( "RenderPipelineAsset.PassColorOutputsExceed", "[%#] pass '%#': %# color outputs exceed the limit (%#)",
                                         sourcePath, pass._name, colorCount, static_cast<uint32>( kMaxColorAttachments ) );
                        ++issueCount;
                    }
                }
                return issueCount;
            }

            /**
             * @brief 3-0) 한 패스의 출력(컬러 · 깊이)은 크기가 같아야 한다 — 렌더 패스의 타깃은 한 크기다.
             * @details 나눗수가 다르면 D3D 는 렌더 패스를 거부하고 Vulkan 은 프레임버퍼를 못 만든다.
             */
            static uint32 checkOutputDivisors( const RenderPipelineDesc& desc, string_view sourcePath )
            {
                uint32 issueCount{ 0 };
                for ( const RenderGraphPassDesc& pass : desc._listPass )
                {
                    const RenderPassAttachment* pFirst           = nullptr;
                    auto                        checkSameDivisor = [&]( string_view attachmentName )
                    {
                        const RenderPassAttachment* pAttachment = findAttachment( desc, attachmentName );
                        if ( pAttachment == nullptr )
                            return;
                        if ( pFirst == nullptr )
                        {
                            pFirst = pAttachment;
                            return;
                        }
                        if ( pAttachment->_resolutionDivisor == pFirst->_resolutionDivisor )
                            return;
                        SW_LOG_ERROR_ID( "RenderPipelineAsset.PassOutputsDivisorDivisor", "[%#] pass '%#': outputs '%#' (divisor %#) and '%#' (divisor %#) differ in size - all outputs of one pass must have the same size",
                                         sourcePath, pass._name, pFirst->_name, pFirst->_resolutionDivisor, pAttachment->_name, pAttachment->_resolutionDivisor );
                        ++issueCount;
                    };
                    for ( const string& outputName : pass._listOutput )
                    {
                        checkSameDivisor( outputName );
                    }
                    if ( pass._depthAttachment.empty() == false )
                        checkSameDivisor( pass._depthAttachment );
                }
                return issueCount;
            }

            /**
             * @brief 3-1 · 3-2) 컬러를 그리는 패스(kDrawsDeclaredColor)는 선언한 컬러 출력에 그리고, G버퍼는 알베도 · 노멀 · 머티리얼을 한 MRT 패스로 쓴다.
             * @details 컬러 출력이 없으면 그릴 곳이 없다(없는 첨부의 핸들 0 은 백버퍼라, 그대로 두면 화면에 그린다). G버퍼의 컬러 수가 표의 값과 다르면
             *          PSO 와 렌더 패스가 어긋나거나, 노멀이 없어 Lighting · SSAO 가 읽을 것이 없다.
             */
            static uint32 checkGeometryColorOutputs( const RenderPipelineDesc& desc, string_view sourcePath )
            {
                uint32       issueCount{ 0 };
                const uint32 expectedGBufferColorCount = getRenderPassTypeInfo( RenderPassType::GBuffer )._colorTargetCount;
                for ( const RenderGraphPassDesc& pass : desc._listPass )
                {
                    // 컬러를 그리는 패스(표의 kDrawsDeclaredColor — 지오메트리 · 하늘 · 외곽선 껍질 · TAA · 일반 풀스크린)는 선언한 컬러 출력에 그린다.
                    // 실행은 이름으로 타깃을 짐작하지 않으므로 선언이 없으면 그릴 곳이 없다(옛 짐작은 없는 첨부의 핸들 0 — 백버퍼 — 를 열었다).
                    const bool bDrawsColor = isViewPassType( pass._type ) &&
                                             getRenderPassTypeInfo( pass._type ).hasFlag( RenderPassTraitFlag::kDrawsDeclaredColor );
                    if ( bDrawsColor && pass._listResolvedColorOutput.empty() )
                    {
                        SW_LOG_ERROR_ID( "Renderer.PassNoColorOutput", "[%#] pass '%#' (%#): no color output - this pass draws to the color attachments declared in _listOutput",
                                         sourcePath, pass._name, getEnumName( pass._type ) );
                        ++issueCount;
                    }
                    const bool bSkipGBufferOutputCheck = pass._type != RenderPassType::GBuffer || pass._listResolvedColorOutput.empty() ||
                                                         pass._listResolvedColorOutput.size() == expectedGBufferColorCount;
                    if ( bSkipGBufferOutputCheck )
                        continue;
                    SW_LOG_ERROR_ID( "RenderPipelineAsset.PassColorOutputsG", "[%#] pass '%#' (%#): %# color outputs - the G-buffer writes albedo, normal, material and velocity (%#) in one MRT pass",
                                     sourcePath, pass._name, getEnumName( pass._type ), static_cast<uint32>( pass._listResolvedColorOutput.size() ), expectedGBufferColorCount );
                    ++issueCount;
                }
                issueCount += checkGBufferLayout( desc, sourcePath );
                return issueCount;
            }

            /**
             * @brief 3-1) G버퍼 패스의 출력이 명세(`GBufferAttachmentSpec` 판 `kVersion`)와 같은가 — MRT 슬롯마다 첨부의 포맷 · 클리어 값.
             * @details 셰이더는 명세의 슬롯 순서 · 인코딩으로 쓰고(SV_TARGET0 ..) PSO 포맷도 명세에서 온다. 첨부 이름은 파이프라인이 고른다(역할로 읽는다).
             *          XML 의 포맷이 다르면 PSO 와 첨부가 어긋나고, 클리어 값이 다르면 "그려지지 않은 픽셀" 표시(알파 0)가 깨진다 — 로드 오류로 막는다.
             */
            static uint32 checkGBufferLayout( const RenderPipelineDesc& desc, string_view sourcePath )
            {
                uint32 issueCount{ 0 };
                for ( const RenderGraphPassDesc& pass : desc._listPass )
                {
                    if ( pass._type != RenderPassType::GBuffer || pass._listResolvedColorOutput.size() != GBufferLayout::kAttachmentCount )
                        continue;
                    for ( uint32 slot = 0; slot < GBufferLayout::kAttachmentCount; ++slot )
                    {
                        const GBufferAttachmentSpec& spec           = GBufferLayout::kArrAttachment[slot];
                        const hashed_string&         outputName     = pass._listResolvedColorOutput[slot]._attachment;
                        const RenderPassAttachment*  pAttachment    = findAttachment( desc, outputName.view() );
                        const bool                   bFormatMatches = pAttachment != nullptr && pAttachment->_format == spec._format;
                        const float4                 clear          = pAttachment != nullptr ? pAttachment->_clearColor : float4{};
                        // 장면 색 칸(자체 발광)의 클리어는 파이프라인의 배경색이다 — 표가 정하지 않는다.
                        const bool bClearMatches = pAttachment != nullptr && ( spec._bPipelineClear != 0 || ( clear._x == spec._clearColor._x && clear._y == spec._clearColor._y &&
                                                                                                              clear._z == spec._clearColor._z && clear._w == spec._clearColor._w ) );
                        if ( bFormatMatches && bClearMatches )
                            continue;
                        SW_LOG_ERROR_ID( "RenderPipelineAsset.GBufferLayoutMismatch", "[%#] pass '%#': G-buffer slot %# ('%#', %#) must be %# cleared to 0 by the G-buffer layout v%#%#",
                                         sourcePath, pass._name, slot, outputName.c_str(), spec._pName, getEnumName( spec._format ), GBufferLayout::kVersion,
                                         pAttachment == nullptr ? " - the attachment is not declared" : ( bFormatMatches ? " - its clear color differs" : " - its format differs" ) );
                        ++issueCount;
                    }
                }
                return issueCount;
            }

            /**
             * @brief 3-3) 화면 속도 출력(Velocity 역할)은 깊이를 쓰는 불투명 지오메트리 패스(`kWritesVelocity`)만 선언하고, 첨부는 명세의 포맷 · 클리어 값(`GBufferLayout::kVelocitySlot`)이다.
             * @details 포워드 패스의 셰이더는 색을 `SV_TARGET0`, 속도를 `SV_TARGET1` 에 쓴다(binding_output.hlsli) — 컬러 출력이 [색, 속도] 둘이 아니면 PSO 와 타깃이 어긋난다.
             *          G버퍼는 명세의 슬롯 순서를 `checkGBufferLayout` 이 본다. 클리어 0 은 "그리지 않은 픽셀 = 움직임 없음" 이고 읽는 쪽(TAA · 모션 블러)은 깊이 1 로 그 픽셀을 가린다.
             */
            static uint32 checkVelocityOutputs( const RenderPipelineDesc& desc, string_view sourcePath )
            {
                uint32                       issueCount{ 0 };
                const GBufferAttachmentSpec& velocitySpec = GBufferLayout::kArrAttachment[GBufferLayout::kVelocitySlot];
                for ( const RenderGraphPassDesc& pass : desc._listPass )
                {
                    const vector<RenderGraphPassDesc::ResolvedAttachment>& listColor = pass._listResolvedColorOutput;
                    for ( size_t outputIndex = 0; outputIndex < listColor.size(); ++outputIndex )
                    {
                        if ( static_cast<RenderPassInputRole>( listColor[outputIndex]._role ) != RenderPassInputRole::Velocity )
                            continue;
                        const hashed_string& outputName = listColor[outputIndex]._attachment;
                        if ( getRenderPassTypeInfo( pass._type ).hasFlag( RenderPassTraitFlag::kWritesVelocity ) == false )
                        {
                            SW_LOG_ERROR_ID( "RenderPipelineAsset.VelocityWriterType", "[%#] pass '%#' (%#) writes velocity '%#' - only opaque geometry passes that write depth (GBuffer, ForwardOpaque, ForwardOnlyOpaque) do",
                                             sourcePath, pass._name, getEnumName( pass._type ), outputName.c_str() );
                            ++issueCount;
                        }
                        const RenderPassAttachment* pAttachment = findAttachment( desc, outputName.view() );
                        const float4                clear       = pAttachment != nullptr ? pAttachment->_clearColor : float4{};
                        const bool                  bSpecMatch  = pAttachment != nullptr && pAttachment->_format == velocitySpec._format && clear._x == 0.0f && clear._y == 0.0f;
                        if ( bSpecMatch == false )
                        {
                            SW_LOG_ERROR_ID( "RenderPipelineAsset.VelocityFormat", "[%#] pass '%#': velocity '%#' must be %# cleared to 0 (G-buffer layout v%#)", sourcePath, pass._name,
                                             outputName.c_str(), getEnumName( velocitySpec._format ), GBufferLayout::kVersion );
                            ++issueCount;
                        }
                        const bool bForwardShape = pass._type == RenderPassType::GBuffer || ( listColor.size() == 2 && outputIndex == 1 );
                        if ( bForwardShape == false )
                        {
                            SW_LOG_ERROR_ID( "RenderPipelineAsset.VelocityOutputSlot", "[%#] pass '%#' (%#): velocity must be the second of exactly two color outputs (color, velocity) - the shader writes SV_TARGET1",
                                             sourcePath, pass._name, getEnumName( pass._type ) );
                            ++issueCount;
                        }
                    }
                }
                return issueCount;
            }

            /**
             * @brief 4) 풀스크린 패스의 입력이 그 타입의 계약과 맞는가. "선언만 있고 아무도 안 읽는 입력" 을 여기서 잡는다.
             * @details 그런 입력은 오류 없이 지나가고, 그것을 만드는 패스(예: SSAO)만 매 프레임 돌고 버려진다. 실행은 같은 해석(_listResolvedInput)을 그대로 건다.
             */
            static uint32 checkInputContracts( const RenderPipelineDesc& desc, string_view sourcePath )
            {
                uint32 issueCount{ 0 };
                for ( const RenderGraphPassDesc& pass : desc._listPass )
                {
                    const RenderPassInputSignature* pContract = findRenderPassInputSignature( pass._type );
                    if ( pContract == nullptr )
                        continue;

                    uint32 sourceColorCount{ 0 };
                    for ( const RenderGraphPassDesc::ResolvedAttachment& input : pass._listResolvedInput )
                    {
                        const RenderPassInputRole role = static_cast<RenderPassInputRole>( input._role );
                        if ( role == RenderPassInputRole::SourceColor )
                            ++sourceColorCount;
                        if ( pContract->reads( role ) )
                            continue;
                        SW_LOG_ERROR_ID( "RenderPipelineAsset.PassInputRoleNot", "[%#] pass '%#' (%#): input '%#' (role %#) is not read by this pass type - it is declared but never bound",
                                         sourcePath, pass._name, getEnumName( pass._type ), input._attachment.c_str(), getRenderPassInputRoleName( role ) );
                        ++issueCount;
                    }
                    if ( sourceColorCount > 1 )
                    {
                        SW_LOG_ERROR_ID( "RenderPipelineAsset.PassColorInputsProcess", "[%#] pass '%#' (%#): %# color inputs to process (SourceColor role) - the shader reads only one",
                                         sourcePath, pass._name, getEnumName( pass._type ), sourceColorCount );
                        ++issueCount;
                    }
                    for ( uint32 requiredIndex = 0; requiredIndex < pContract->_requiredCount; ++requiredIndex )
                    {
                        const RenderPassInputRole required = pContract->_arrRequired[requiredIndex];
                        const bool                bFound   = VectorUtil::containsIf( pass._listResolvedInput, [required]( const RenderGraphPassDesc::ResolvedAttachment& input )
                                         { return static_cast<RenderPassInputRole>( input._role ) == required; } );
                        if ( bFound )
                            continue;
                        SW_LOG_ERROR_ID( "RenderPipelineAsset.PassRequiredInputRole", "[%#] pass '%#' (%#): required input (role %#) is not declared - the shader reads kInvalidIndex",
                                         sourcePath, pass._name, getEnumName( pass._type ), getRenderPassInputRoleName( required ) );
                        ++issueCount;
                    }
                }
                return issueCount;
            }

            /**
             * @brief 3-3 · 3-4) Canvas(화면 2D)는 주 출력(Swapchain)에 그리고 Swapchain 을 쓰는 **마지막** 패스여야 하며, 하늘은 뎁스 첨부가 있어야 한다.
             * @details 뒤에 Swapchain 을 쓰는 패스가 있으면 UI 가 덮이고, 스크린샷 캡처 → 백버퍼 복사(마지막 Swapchain 쓰기 패스 끝)가 UI 앞에서 일어난다.
             *          UI 가 없는 파이프라인(Canvas 없음)은 허용한다. 하늘은 깊이 테스트로 빈 곳만 칠한다 — 뎁스 첨부가 없으면 장면 위를 통째로 덮는다.
             */
            static uint32 checkCanvasAndSky( const RenderPipelineDesc& desc, string_view sourcePath )
            {
                uint32                     issueCount{ 0 };
                const RenderGraphPassDesc* pCanvas = nullptr;
                for ( const RenderGraphPassDesc& pass : desc._listPass )
                {
                    const bool bWritesSwapchain = VectorUtil::contains( pass._listOutput, string( kSwapchainOutputName ) );
                    if ( pass._type == RenderPassType::Canvas )
                    {
                        if ( bWritesSwapchain == false )
                        {
                            SW_LOG_ERROR_ID( "RenderPipelineAsset.PassCanvasOutputNot", "[%#] pass '%#' (Canvas): the output is not the Swapchain - Canvas draws to the main output", sourcePath, pass._name );
                            ++issueCount;
                        }
                        pCanvas = &pass;
                        continue;
                    }
                    if ( pCanvas != nullptr && bWritesSwapchain )
                    {
                        SW_LOG_ERROR_ID( "RenderPipelineAsset.PassWritesSwapchainAfter", "[%#] pass '%#': writes the Swapchain after Canvas '%#' - Canvas must be the last pass that writes the Swapchain (the UI would be covered)",
                                         sourcePath, pass._name, pCanvas->_name );
                        ++issueCount;
                    }
                }
                for ( const RenderGraphPassDesc& pass : desc._listPass )
                {
                    if ( pass._type != RenderPassType::Sky || pass._depthAttachment.empty() == false )
                        continue;
                    SW_LOG_ERROR_ID( "RenderPipelineAsset.PassSkyDepthAttachmentEmpty", "[%#] pass '%#' (Sky): _depthAttachment is empty - the sky paints only where nothing was drawn, using the depth test", sourcePath, pass._name );
                    ++issueCount;
                }
                return issueCount;
            }

            /**
             * @brief 3-5) 후처리 품질로 건너뛰는 패스(`_minPostQuality`)는 후처리를 끈 뷰처럼 건너뛸 수 있는 타입(kSkipsWithoutPostProcess — 원본을 넘기거나 기본 클리어)만이다.
             * @details 다른 타입을 건너뛰면 출력 첨부에 지난 프레임 값이 남는다.
             */
            static uint32 checkMinPostQuality( const RenderPipelineDesc& desc, string_view sourcePath )
            {
                uint32 issueCount{ 0 };
                for ( const RenderGraphPassDesc& pass : desc._listPass )
                {
                    const bool bSkippable = getRenderPassTypeInfo( pass._type ).hasFlag( RenderPassTraitFlag::kSkipsWithoutPostProcess );
                    if ( pass._minPostQuality < 0 )
                    {
                        SW_LOG_ERROR_ID( "RenderPipelineAsset.PassMinPostQualityMustGreater", "[%#] pass '%#': _minPostQuality %# - must be 0 or greater", sourcePath, pass._name, pass._minPostQuality );
                        ++issueCount;
                    }
                    else if ( pass._minPostQuality > 0 && bSkippable == false )
                    {
                        SW_LOG_ERROR_ID( "RenderPipelineAsset.PassMinPostQualityAcceptedOnly", "[%#] pass '%#' (%#): _minPostQuality is accepted only for types whose post-processing can be skipped (SSAO, Bloom, Outline, MotionBlur)", sourcePath, pass._name, getEnumName( pass._type ) );
                        ++issueCount;
                    }
                }
                return issueCount;
            }

            /**
             * @brief 3-6) 꺼지면 원본을 가리키는 패스(kBypassesToSource)는 원본(SourceColor 입력) 하나와 컬러 출력 하나를 갖고, 둘은 형식 · 나눗수가 같다.
             *        그 출력을 쓰는 패스는 자기 하나이고, 뒤의 패스가 원본을 다시 쓰지 않는다.
             * @details 꺼진 뷰에서는 출력 이름이 원본 텍스처를 가리킨다(`TransientAttachmentPool::forward`). 형식 · 크기가 다르면 읽는 쪽 PSO · 텍셀 크기와 어긋나고,
             *          출력을 다른 패스도 쓰면 그 쓰기가 원본을 덮고, 뒤에서 원본을 쓰면 출력을 읽는 쪽이 나중 판을 읽는다.
             */
            static uint32 checkBypassPasses( const RenderPipelineDesc& desc, string_view sourcePath )
            {
                uint32 issueCount{ 0 };
                for ( size_t passIndex = 0; passIndex < desc._listPass.size(); ++passIndex )
                {
                    const RenderGraphPassDesc& pass = desc._listPass[passIndex];
                    if ( getRenderPassTypeInfo( pass._type ).hasFlag( RenderPassTraitFlag::kBypassesToSource ) == false )
                        continue;
                    // 톤맵 대신 자리는 원본을 가리키는 끄기가 없다 — 톤맵 출력 위에 그리므로 꺼지면 아무것도 하지 않는다(checkPostProcessMaterialPasses 가 본다).
                    if ( pass._blendableLocation == PostProcessBlendableLocation::ReplacingTonemap )
                        continue;
                    const RenderGraphPassDesc::ResolvedAttachment* pSource     = nullptr;
                    uint32                                         sourceCount = 0;
                    for ( const RenderGraphPassDesc::ResolvedAttachment& input : pass._listResolvedInput )
                    {
                        if ( static_cast<RenderPassInputRole>( input._role ) != RenderPassInputRole::SourceColor )
                            continue;
                        pSource = &input;
                        ++sourceCount;
                    }
                    if ( sourceCount != 1 )
                        continue; // 원본이 없거나 둘이면 입력 계약 검사(4)가 이미 알렸다(이 타입들은 원본이 필수 입력이다)
                    if ( pass._listResolvedColorOutput.empty() )
                        continue; // 컬러 출력이 없으면 3-1 · 3-2 가 이미 알렸다(이 타입들은 모두 kDrawsDeclaredColor 다)
                    if ( pass._listResolvedColorOutput.size() != 1 )
                    {
                        SW_LOG_ERROR_ID( "RenderPipelineAsset.PassNeedsOneColor", "[%#] pass '%#' (%#): needs one color output - when the effect is off the output names the source",
                                         sourcePath, pass._name, getEnumName( pass._type ) );
                        ++issueCount;
                        continue;
                    }
                    const string                outputName = pass._listResolvedColorOutput[0]._attachment.c_str();
                    const string                sourceName = pSource->_attachment.c_str();
                    const RenderPassAttachment* pOutput    = findAttachment( desc, outputName );
                    const RenderPassAttachment* pSourceAtt = findAttachment( desc, sourceName );
                    const bool                  bSameShape = pOutput != nullptr && pSourceAtt != nullptr && pOutput->_format == pSourceAtt->_format && pOutput->_resolutionDivisor == pSourceAtt->_resolutionDivisor;
                    if ( bSameShape == false )
                    {
                        SW_LOG_ERROR_ID( "RenderPipelineAsset.PassOutputSourceDiffer", "[%#] pass '%#' (%#): output '%#' and source '%#' differ in format or divisor - when the effect is off the output names the source",
                                         sourcePath, pass._name, getEnumName( pass._type ), outputName, sourceName );
                        ++issueCount;
                    }
                    for ( size_t otherIndex = 0; otherIndex < desc._listPass.size(); ++otherIndex )
                    {
                        const RenderGraphPassDesc& other = desc._listPass[otherIndex];
                        if ( otherIndex != passIndex && VectorUtil::contains( other._listOutput, outputName ) )
                        {
                            SW_LOG_ERROR_ID( "RenderPipelineAsset.PassAlsoWritesOutput", "[%#] pass '%#' also writes '%#', the output of '%#' (%#) - that output may name the source texture", sourcePath, other._name, outputName,
                                             pass._name, getEnumName( pass._type ) );
                            ++issueCount;
                        }
                        if ( otherIndex > passIndex && VectorUtil::contains( other._listOutput, sourceName ) )
                        {
                            SW_LOG_ERROR_ID( "RenderPipelineAsset.PassWritesAfterReads", "[%#] pass '%#' writes '%#' after '%#' (%#) reads it - readers of '%#' would see the later version when the effect is off", sourcePath,
                                             other._name, sourceName, pass._name, getEnumName( pass._type ), outputName );
                            ++issueCount;
                        }
                    }
                }
                return issueCount;
            }

            /**
             * @brief 3-9) 데칼 패스의 컬러 출력은 G버퍼 알베도 · 노멀 · 머티리얼 셋이고 이 차례다 — 셰이더가 SV_TARGET0..2 에 그 칸의 인코딩으로 쓴다(decal.hlsl).
             * @details 다른 첨부(장면 색 · 포워드 색)에 쓰면 데칼 색이 그대로 장면에 덮인다 — 포워드 파이프라인에 데칼이 없는 까닭이다(덮을 G버퍼가 없다).
             */
            static uint32 checkDecalPasses( const RenderPipelineDesc& desc, string_view sourcePath )
            {
                uint32                    issueCount{ 0 };
                const RenderPassInputRole arrExpected[] = { RenderPassInputRole::GBufferAlbedo, RenderPassInputRole::GBufferNormal, RenderPassInputRole::GBufferMaterial };
                for ( const RenderGraphPassDesc& pass : desc._listPass )
                {
                    if ( pass._type != RenderPassType::Decal )
                        continue;
                    bool bShape = pass._listResolvedColorOutput.size() == std::size( arrExpected );
                    for ( size_t index = 0; bShape && index < std::size( arrExpected ); ++index )
                    {
                        bShape = static_cast<RenderPassInputRole>( pass._listResolvedColorOutput[index]._role ) == arrExpected[index];
                    }
                    if ( bShape )
                        continue;
                    SW_LOG_ERROR_ID( "RenderPipelineAsset.PassDecalOutputs", "[%#] pass '%#' (Decal): outputs must be the G-buffer albedo, normal and material attachments in that order - a decal modifies the G-buffer",
                                     sourcePath, pass._name );
                    ++issueCount;
                }
                return issueCount;
            }

            /**
             * @brief 3-8) 후처리 머티리얼 패스는 자리마다 하나다. 톤맵 대신 자리는 출력 하나를 앞의 패스(톤맵)가 이미 썼어야 한다 — 그 위에 무게만큼 덮는다.
             * @details 같은 자리의 패스가 둘이면 같은 블렌더블을 두 번 그린다. 톤맵 대신 자리의 출력을 아무도 앞에서 쓰지 않으면 무게 1 미만에서 지난 프레임 값과 섞인다.
             */
            static uint32 checkPostProcessMaterialPasses( const RenderPipelineDesc& desc, string_view sourcePath )
            {
                uint32 issueCount{ 0 };
                for ( size_t passIndex = 0; passIndex < desc._listPass.size(); ++passIndex )
                {
                    const RenderGraphPassDesc& pass = desc._listPass[passIndex];
                    if ( pass._type != RenderPassType::PostProcessMaterial || pass._blendableLocation == PostProcessBlendableLocation::None )
                        continue;
                    for ( size_t earlierIndex = 0; earlierIndex < passIndex; ++earlierIndex )
                    {
                        const RenderGraphPassDesc& earlier = desc._listPass[earlierIndex];
                        if ( earlier._type == RenderPassType::PostProcessMaterial && earlier._blendableLocation == pass._blendableLocation )
                        {
                            SW_LOG_ERROR_ID( "RenderPipelineAsset.PassPostMaterialLocationTwice", "[%#] passes '%#' and '%#' both take blendable location %# - one PostProcessMaterial pass per location", sourcePath,
                                             earlier._name, pass._name, getEnumName( pass._blendableLocation ) );
                            ++issueCount;
                        }
                    }
                    if ( pass._blendableLocation != PostProcessBlendableLocation::ReplacingTonemap )
                        continue;
                    if ( pass._listResolvedColorOutput.size() != 1 )
                    {
                        SW_LOG_ERROR_ID( "RenderPipelineAsset.PassPostMaterialReplacingOutputs", "[%#] pass '%#': a ReplacingTonemap pass draws over one tonemapped output", sourcePath, pass._name );
                        ++issueCount;
                        continue;
                    }
                    const string outputName = pass._listResolvedColorOutput[0]._attachment.c_str();
                    bool         bWritten   = false;
                    for ( size_t earlierIndex = 0; earlierIndex < passIndex; ++earlierIndex )
                    {
                        bWritten = bWritten || VectorUtil::contains( desc._listPass[earlierIndex]._listOutput, outputName );
                    }
                    if ( bWritten == false )
                    {
                        SW_LOG_ERROR_ID( "RenderPipelineAsset.PassPostMaterialReplacingBase", "[%#] pass '%#': output '%#' is not written by an earlier pass - ReplacingTonemap draws over the tonemap output", sourcePath,
                                         pass._name, outputName );
                        ++issueCount;
                    }
                }
                return issueCount;
            }

            /**
             * @brief 3-7) 꺼지면 출력이 없는 패스(kOmittedWhenOff)는 컬러 출력 하나를 혼자 쓰고, 그 출력을 읽는 패스는 모두 그 역할을 선택 입력으로만 받는다.
             * @details 꺼진 뷰에서는 출력 이름이 아무 첨부도 가리키지 않는다(`TransientAttachmentPool::markAbsent`). 읽는 쪽은 그 역할을 바인딩하지 않아 셰이더 기본값(AO = 1)을 쓴다 —
             *          필수 입력이거나 계약이 없는 패스가 읽으면 그 기본값이 없다.
             */
            static uint32 checkOmittedPasses( const RenderPipelineDesc& desc, string_view sourcePath )
            {
                uint32 issueCount{ 0 };
                for ( size_t passIndex = 0; passIndex < desc._listPass.size(); ++passIndex )
                {
                    const RenderGraphPassDesc& pass = desc._listPass[passIndex];
                    if ( getRenderPassTypeInfo( pass._type ).hasFlag( RenderPassTraitFlag::kOmittedWhenOff ) == false )
                        continue;
                    if ( pass._listResolvedColorOutput.size() != 1 )
                    {
                        SW_LOG_ERROR_ID( "RenderPipelineAsset.PassNeedsOneColor2", "[%#] pass '%#' (%#): needs one color output - when the effect is off that output is absent", sourcePath, pass._name, getEnumName( pass._type ) );
                        ++issueCount;
                        continue;
                    }
                    const hashed_string& output     = pass._listResolvedColorOutput[0]._attachment;
                    const string         outputName = output.c_str();
                    for ( size_t otherIndex = 0; otherIndex < desc._listPass.size(); ++otherIndex )
                    {
                        if ( otherIndex == passIndex )
                            continue;
                        const RenderGraphPassDesc& other = desc._listPass[otherIndex];
                        if ( VectorUtil::contains( other._listOutput, outputName ) )
                        {
                            SW_LOG_ERROR_ID( "RenderPipelineAsset.PassAlsoWritesOutput2", "[%#] pass '%#' also writes '%#', the output of '%#' (%#) - that output may be absent", sourcePath, other._name, outputName, pass._name,
                                             getEnumName( pass._type ) );
                            ++issueCount;
                        }
                        const RenderPassInputSignature* pContract = findRenderPassInputSignature( other._type );
                        for ( const RenderGraphPassDesc::ResolvedAttachment& input : other._listResolvedInput )
                        {
                            if ( input._attachment != output )
                                continue;
                            const RenderPassInputRole role      = static_cast<RenderPassInputRole>( input._role );
                            bool                      bRequired = false;
                            for ( uint32 requiredIndex = 0; pContract != nullptr && requiredIndex < pContract->_requiredCount; ++requiredIndex )
                            {
                                bRequired = bRequired || pContract->_arrRequired[requiredIndex] == role;
                            }
                            const bool bOptionalRead = pContract != nullptr && pContract->reads( role ) && bRequired == false;
                            if ( bOptionalRead )
                                continue;
                            SW_LOG_ERROR_ID( "RenderPipelineAsset.PassReadsRoleNot", "[%#] pass '%#' reads '%#' (role %#) not as an optional input - when '%#' (%#) is off that input is absent", sourcePath, other._name,
                                             outputName, getRenderPassInputRoleName( role ), pass._name, getEnumName( pass._type ) );
                            ++issueCount;
                        }
                    }
                }
                return issueCount;
            }
        };
    } // namespace

    uint32 RenderPipelineAsset::validate( string_view sourcePath )
    {
        using Internal = RenderPipelineValidationInternal;
        // 순서가 있다 — 패스 해석(2)이 채운 `_resolved*` 를 뒤 규칙들이 읽는다.
        uint32 issueCount = Internal::checkAttachments( _desc, sourcePath );
        issueCount += Internal::checkShadingModel( _desc, sourcePath );
        issueCount += Internal::checkPassNamesUnique( _desc, sourcePath );
        issueCount += Internal::resolveAndCheckPassAttachments( _desc, sourcePath );
        issueCount += Internal::checkPassRenderState( _desc, sourcePath );
        issueCount += Internal::checkComputePasses( _desc, sourcePath );
        issueCount += Internal::checkFrameGraph( _desc, sourcePath );
        issueCount += Internal::checkModuleShaderList( _desc, sourcePath );
        issueCount += Internal::checkColorOutputLimit( _desc, sourcePath );
        issueCount += Internal::checkOutputDivisors( _desc, sourcePath );
        issueCount += Internal::checkGeometryColorOutputs( _desc, sourcePath );
        issueCount += Internal::checkVelocityOutputs( _desc, sourcePath );
        issueCount += Internal::checkInputContracts( _desc, sourcePath );
        issueCount += Internal::checkCanvasAndSky( _desc, sourcePath );
        issueCount += Internal::checkMinPostQuality( _desc, sourcePath );
        issueCount += Internal::checkBypassPasses( _desc, sourcePath );
        issueCount += Internal::checkOmittedPasses( _desc, sourcePath );
        issueCount += Internal::checkPostProcessMaterialPasses( _desc, sourcePath );
        issueCount += Internal::checkDecalPasses( _desc, sourcePath );
        issueCount += Internal::checkAttachmentStoreOps( _desc, sourcePath );
        if ( issueCount > 0 )
            SW_LOG_ERROR_ID( "RenderPipelineAsset.PipelineValidationFoundIssues", "[%#] Pipeline validation found %# issues - the render result may be wrong or the GPU may die",
                             sourcePath, issueCount );
        return issueCount;
    }

} // namespace sw
