#include "pch.h"

#include "Engine/Graphics/Renderer/Pipeline/RenderPipelineResource.h"

#include "Core/String/StringUtil.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPassInputContract.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPassTypeTraits.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Resource/AssetFormat.h"
#include "Engine/Resource/ResourceManager.h"
#include "Engine/Serialization/Format/ReflectedXmlFile.h"
#include "Engine/Serialization/Format/XmlSerializer.h"

namespace sw
{
    SW_LOG_CALLER( "RenderPipelineResource" );

    bool RenderPipelineResource::loadFromXmlFile( string_view assetRelativePath )
    {
        _desc = {};
        if ( ReflectedXmlFile::loadDesc( assetRelativePath, _desc ) == false )
            return false;

        validate( assetRelativePath );

        SW_LOG_INFO( "Loaded '%#' (model=%#, attachments=%#, passes=%#)",
                     _desc._name, _desc._shadingModel, _desc._listAttachment.size(), _desc._listPass.size() );
        return true;
    }

    uint32 RenderPipelineResource::validate( string_view sourcePath )
    {
        uint32 issueCount{ 0 };

        auto findAttachment = [this]( string_view name ) -> const RenderPassAttachment*
        {
            for ( const RenderPassAttachment& attachment : _desc._listAttachment )
            {
                if ( attachment._name == name )
                    return &attachment;
            }
            return nullptr;
        };

        // 1) 첨부: 포맷 표기가 RHIFormat 으로 해석되는가, 이름이 겹치지 않는가.
        for ( uint32 attachmentIndex = 0; attachmentIndex < _desc._listAttachment.size(); ++attachmentIndex )
        {
            const RenderPassAttachment& attachment = _desc._listAttachment[attachmentIndex];
            RHIFormat                   parsed{};
            if ( engine::getTypeRegistry().enumFromString( attachment._format, parsed ) == false )
            {
                SW_LOG_ERROR( "[%#] attachment '%#': 알 수 없는 포맷 '%#'", sourcePath, attachment._name, attachment._format );
                ++issueCount;
            }
            else if ( isRhiFormatRenderable( parsed ) == false )
            {
                // 이름은 맞지만 첨부가 될 수 없는 포맷(Unknown · 블록 압축 · R32G32B32_FLOAT) — 트랜지언트를 만들 때 백엔드가 실패한다.
                SW_LOG_ERROR( "[%#] attachment '%#': 포맷 '%#' 은 렌더 타깃이 될 수 없습니다", sourcePath, attachment._name, attachment._format );
                ++issueCount;
            }
            for ( uint32 otherIndex = 0; otherIndex < attachmentIndex; ++otherIndex )
            {
                if ( _desc._listAttachment[otherIndex]._name == attachment._name )
                {
                    SW_LOG_ERROR( "[%#] attachment '%#' 이름이 중복됐습니다", sourcePath, attachment._name );
                    ++issueCount;
                    break;
                }
            }
            if ( isSupportedResolutionDivisor( attachment._resolutionDivisor ) == false )
            {
                SW_LOG_ERROR( "[%#] attachment '%#': _resolutionDivisor %# — 1 · 2 · 4 만 받습니다", sourcePath, attachment._name, attachment._resolutionDivisor );
                ++issueCount;
            }
            // 역할 선언은 아는 글이어야 한다. 모르는 글을 조용히 이름 · 포맷 규칙으로 넘기면 선언한 사람이 왜 안 걸리는지 모른다.
            RenderPassInputRole declaredRole{ RenderPassInputRole::Invalid };
            if ( attachment._role.empty() == false && tryParseRenderPassInputRole( attachment._role, declaredRole ) == false )
            {
                SW_LOG_ERROR( "[%#] attachment '%#': 알 수 없는 역할 '%#' — SourceColor · SceneDepth · GBufferAlbedo · GBufferNormal · ShadowMap · "
                              "AmbientOcclusion 중 하나입니다",
                              sourcePath, attachment._name, attachment._role );
                ++issueCount;
            }
        }

        auto isDepthAttachment = [&]( const RenderPassAttachment* pAttachment ) -> bool
        {
            return pAttachment != nullptr && engine::getTypeRegistry().enumFromString<RHIFormat>( pAttachment->_format ) == constant::kDepthStencilFormat;
        };

        // 2) 패스: 이름이 하나뿐인가, 타입 표기가 해석되는가, 입출력이 선언된 첨부를 가리키는가.
        for ( size_t passIndex = 0; passIndex < _desc._listPass.size(); ++passIndex )
        {
            for ( size_t earlierIndex = 0; earlierIndex < passIndex; ++earlierIndex )
            {
                if ( _desc._listPass[earlierIndex]._name == _desc._listPass[passIndex]._name )
                {
                    SW_LOG_ERROR( "[%#] pass '%#' 가 두 번 선언됐습니다 — 이름은 패스의 열쇠라 뒤의 것은 버려집니다", sourcePath,
                                  _desc._listPass[passIndex]._name );
                    ++issueCount;
                    break;
                }
            }
        }
        for ( RenderGraphPassDesc& pass : _desc._listPass )
        {
            pass._resolvedType = engine::getTypeRegistry().enumFromString<RenderPassType>( pass._type );
            pass._resolvedDepthAttachment =
                pass._depthAttachment.empty() ? hashed_string{} : hashed_string( pass._depthAttachment.c_str() );
            pass._listResolvedInput.clear();
            pass._listResolvedOutput.clear();
            pass._listResolvedColorOutput.clear();
            // 역할은 첨부의 선언(`_role`)이 먼저, 그다음 정본 이름 · 포맷이다(resolveRenderPassInputRole).
            auto resolveAttachment = [&]( const string& attachmentName ) -> RenderGraphPassDesc::ResolvedAttachment
            {
                const RenderPassAttachment*             pAttachment = findAttachment( attachmentName );
                RenderGraphPassDesc::ResolvedAttachment resolved{};
                resolved._attachment = hashed_string( attachmentName.c_str() );
                resolved._role       = static_cast<uint8>(
                    resolveRenderPassInputRole( attachmentName, isDepthAttachment( pAttachment ), pAttachment != nullptr ? string_view( pAttachment->_role ) : string_view{} ) );
                return resolved;
            };
            for ( const string& inputName : pass._listInput )
                pass._listResolvedInput.push_back( resolveAttachment( inputName ) );
            for ( const string& outputName : pass._listOutput )
            {
                pass._listResolvedOutput.emplace_back( outputName.c_str() );
                const RenderPassAttachment* pOutput = findAttachment( outputName );
                if ( pOutput != nullptr && isDepthAttachment( pOutput ) == false )
                    pass._listResolvedColorOutput.push_back( resolveAttachment( outputName ) );
            }
            if ( isPipelinePassType( pass._resolvedType ) == false )
            {
                SW_LOG_ERROR( "[%#] pass '%#': 알 수 없는 타입 '%#' — RenderPassType 에 없는 표기입니다",
                              sourcePath, pass._name, pass._type );
                ++issueCount;
            }

            for ( const string& inputName : pass._listInput )
            {
                if ( findAttachment( inputName ) == nullptr )
                {
                    SW_LOG_ERROR( "[%#] pass '%#': 입력 '%#' 이 _attachments 에 없습니다", sourcePath, pass._name, inputName );
                    ++issueCount;
                }
            }
            for ( const string& outputName : pass._listOutput )
            {
                // 스왑체인은 파이프라인이 선언하는 첨부가 아니라 디바이스가 주는 백버퍼다.
                if ( outputName == kSwapchainOutputName )
                    continue;
                if ( findAttachment( outputName ) == nullptr )
                {
                    SW_LOG_ERROR( "[%#] pass '%#': 출력 '%#' 이 _attachments 에 없습니다", sourcePath, pass._name, outputName );
                    ++issueCount;
                }
            }

            // 뎁스 첨부: 비어 있는 것은 "일부러 뎁스를 안 쓴다" 는 뜻이라 정상이다. 다만 이름을
            // 적었으면 그 첨부가 실재하고 뎁스 포맷이어야 한다. 컬러 첨부를 뎁스로 바인딩하면
            // 렌더 패스가 통째로 비호환이 된다.
            if ( pass._depthAttachment.empty() == false )
            {
                const RenderPassAttachment* pDepthAttachment = findAttachment( pass._depthAttachment );
                if ( pDepthAttachment == nullptr )
                {
                    SW_LOG_ERROR( "[%#] pass '%#': 뎁스 첨부 '%#' 이 _attachments 에 없습니다",
                                  sourcePath, pass._name, pass._depthAttachment );
                    ++issueCount;
                }
                else if ( engine::getTypeRegistry().enumFromString<RHIFormat>( pDepthAttachment->_format ) != constant::kDepthStencilFormat )
                {
                    SW_LOG_ERROR( "[%#] pass '%#': 뎁스 첨부 '%#' 의 포맷이 '%#' 입니다 — 뎁스 포맷이어야 합니다",
                                  sourcePath, pass._name, pass._depthAttachment, pDepthAttachment->_format );
                    ++issueCount;
                }
            }
            else
            {
                // 뎁스를 쓰겠다고 출력에 적어 놓고 바인딩은 안 하는 것은 앞뒤가 안 맞는다.
                for ( const string& outputName : pass._listOutput )
                {
                    const RenderPassAttachment* pAttachment = findAttachment( outputName );
                    if ( pAttachment == nullptr || engine::getTypeRegistry().enumFromString<RHIFormat>( pAttachment->_format ) != constant::kDepthStencilFormat )
                        continue;
                    SW_LOG_ERROR( "[%#] pass '%#': 뎁스 첨부 '%#' 을 출력으로 선언했는데 _depthAttachment 가 비어 "
                                  "있습니다 — 이대로면 뎁스 없이 그립니다",
                                  sourcePath, pass._name, outputName );
                    ++issueCount;
                }
            }
        }

        // 3) 컬러 첨부가 여러 개인 패스는 MRT 로 묶인다. 포맷은 서로 달라도 되지만 개수에는 한계가 있다.
        for ( const RenderGraphPassDesc& pass : _desc._listPass )
        {
            uint32 colorCount{ 0 };
            for ( const string& outputName : pass._listOutput )
            {
                const RenderPassAttachment* pAttachment = findAttachment( outputName );
                if ( pAttachment == nullptr )
                    continue;
                RHIFormat fmt{};
                if ( engine::getTypeRegistry().enumFromString( pAttachment->_format, fmt ) && fmt != RHIFormat::D24_UNORM_S8_UINT )
                    ++colorCount;
            }
            if ( colorCount > kMaxColorAttachments )
            {
                SW_LOG_ERROR( "[%#] pass '%#': 컬러 출력이 %#개로 한계(%#)를 넘습니다",
                              sourcePath, pass._name, colorCount, static_cast<uint32>( kMaxColorAttachments ) );
                ++issueCount;
            }
        }

        // 3-0) 한 패스의 출력(컬러 · 깊이)은 크기가 같아야 한다 — 렌더 패스의 타깃은 한 크기다. 나눗수가 다르면 D3D 는 렌더 패스를 거부하고
        //      Vulkan 은 프레임버퍼를 못 만든다.
        for ( const RenderGraphPassDesc& pass : _desc._listPass )
        {
            const RenderPassAttachment* pFirst           = nullptr;
            auto                        checkSameDivisor = [&]( string_view attachmentName )
            {
                const RenderPassAttachment* pAttachment = findAttachment( attachmentName );
                if ( pAttachment == nullptr )
                    return;
                if ( pFirst == nullptr )
                {
                    pFirst = pAttachment;
                    return;
                }
                if ( pAttachment->_resolutionDivisor == pFirst->_resolutionDivisor )
                    return;
                SW_LOG_ERROR( "[%#] pass '%#': 출력 '%#'(나눗수 %#) 과 '%#'(나눗수 %#) 의 크기가 다릅니다 — 한 패스의 출력은 같은 크기여야 합니다",
                              sourcePath, pass._name, pFirst->_name, pFirst->_resolutionDivisor, pAttachment->_name, pAttachment->_resolutionDivisor );
                ++issueCount;
            };
            for ( const string& outputName : pass._listOutput )
                checkSameDivisor( outputName );
            if ( pass._depthAttachment.empty() == false )
                checkSameDivisor( pass._depthAttachment );
        }

        // 3-1) 지오메트리 패스(ForwardOpaque · GBuffer · Transparent)는 **선언한 컬러 출력**에 그린다. 컬러 출력이 없으면 그릴 곳이 없다
        //      (없는 첨부의 핸들 0 은 백버퍼라, 그대로 두면 화면에 그린다).
        for ( const RenderGraphPassDesc& pass : _desc._listPass )
        {
            const bool bGeometryColorPass = pass._resolvedType == RenderPassType::ForwardOpaque || pass._resolvedType == RenderPassType::GBuffer ||
                                            pass._resolvedType == RenderPassType::Transparent;
            if ( bGeometryColorPass == false || pass._listResolvedColorOutput.empty() == false )
                continue;
            SW_LOG_ERROR( "[%#] pass '%#'(%#): 컬러 출력이 없습니다 — 지오메트리 패스는 _listOutput 에 선언한 컬러 첨부에 그립니다",
                          sourcePath, pass._name, pass._type );
            ++issueCount;
        }

        // 3-2) GBuffer 는 알베도 · 노멀을 한 MRT 패스로 쓴다(컬러 타깃 수는 패스 타입 표의 값). 수가 다르면 PSO 와 렌더 패스가 어긋나거나,
        //      노멀이 없어 Lighting · SSAO 가 읽을 것이 없다.
        for ( const RenderGraphPassDesc& pass : _desc._listPass )
        {
            const uint32 expectedColorCount = getRenderPassTypeTraits( RenderPassType::GBuffer )._colorTargetCount;
            if ( pass._resolvedType != RenderPassType::GBuffer || pass._listResolvedColorOutput.empty() ||
                 pass._listResolvedColorOutput.size() == expectedColorCount )
                continue;
            SW_LOG_ERROR( "[%#] pass '%#'(%#): 컬러 출력이 %#개입니다 — G버퍼는 알베도 · 노멀 %#개를 한 MRT 패스로 씁니다",
                          sourcePath, pass._name, pass._type, static_cast<uint32>( pass._listResolvedColorOutput.size() ), expectedColorCount );
            ++issueCount;
        }

        // 4) 풀스크린 패스의 입력이 그 타입의 계약과 맞는가. "선언만 있고 아무도 안 읽는 입력" 을 여기서 잡는다.
        //    그런 입력은 오류 없이 지나가고, 그것을 만드는 패스(예: SSAO)만 매 프레임 돌고 버려진다.
        //    실행은 같은 해석(_listResolvedInput)을 그대로 건다.
        for ( const RenderGraphPassDesc& pass : _desc._listPass )
        {
            const RenderPassInputContract* pContract = findRenderPassInputContract( pass._resolvedType );
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
                SW_LOG_ERROR( "[%#] pass '%#'(%#): 입력 '%#'(역할 %#) 은 이 패스 타입이 읽지 않습니다 — 선언만 있고 바인딩되지 않는 입력입니다",
                              sourcePath, pass._name, pass._type, input._attachment.c_str(), getRenderPassInputRoleName( role ) );
                ++issueCount;
            }
            if ( sourceColorCount > 1 )
            {
                SW_LOG_ERROR( "[%#] pass '%#'(%#): 가공할 컬러 입력(SourceColor 역할)이 %#개입니다 — 셰이더는 하나만 읽습니다",
                              sourcePath, pass._name, pass._type, sourceColorCount );
                ++issueCount;
            }
            for ( uint32 requiredIndex = 0; requiredIndex < pContract->_requiredCount; ++requiredIndex )
            {
                const RenderPassInputRole required = pContract->_arrRequired[requiredIndex];
                bool                      bFound{ false };
                for ( const RenderGraphPassDesc::ResolvedAttachment& input : pass._listResolvedInput )
                {
                    if ( static_cast<RenderPassInputRole>( input._role ) == required )
                    {
                        bFound = true;
                        break;
                    }
                }
                if ( bFound )
                    continue;
                SW_LOG_ERROR( "[%#] pass '%#'(%#): 필수 입력(역할 %#)이 선언되지 않았습니다 — 셰이더가 kInvalidIndex 를 읽습니다",
                              sourcePath, pass._name, pass._type, getRenderPassInputRoleName( required ) );
                ++issueCount;
            }
        }

        if ( issueCount > 0 )
            SW_LOG_ERROR( "[%#] 파이프라인 검증에서 %#건의 문제를 찾았습니다 — 렌더 결과가 어긋나거나 GPU 가 죽을 수 있습니다",
                          sourcePath, issueCount );
        return issueCount;
    }

    bool RenderPipelineResource::saveToXmlFile( string_view assetRelativePath ) const
    {
        if ( ReflectedXmlFile::saveDesc( assetRelativePath, _desc ) == false )
            return false;

        SW_LOG_INFO( "Saved '%#' -> %#", _desc._name, assetRelativePath );
        return true;
    }

    TaskHandle RenderPipelineResource::loadFromXmlFileAsync( string_view assetRelativePath )
    {
        TaskHandle handle = engine::getTaskManager().emplaceTask(
            "LoadRenderPipelineAsync",
            SW_DELEGATE_FUNCTION( TaskArgsDelegate, RenderPipelineResource::loadFromXmlFileAsyncJob ),
            MakeTaskArgs( this, string( assetRelativePath ) ) );
        handle.submit();
        return handle;
    }

    void RenderPipelineResource::loadFromXmlFileAsyncJob( const TaskArgs& args )
    {
        RenderPipelineResource* pResource = args.get<RenderPipelineResource*>( 0 );
        if ( pResource == nullptr )
            return;
        if ( pResource->loadFromXmlFile( args.get<string>( 1 ) ) == false )
            SW_LOG_WARNING( "Could not load '%#'", args.get<string>( 1 ) );
    }

} // namespace sw
