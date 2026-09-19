#include "pch.h"

#include "Engine/Graphics/Renderer/Pipeline/RenderPipelineResource.h"

#include "Core/String/StringUtil.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPassInputContract.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderResourceXml.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Resource/AssetFormat.h"
#include "Engine/Resource/ResourceManager.h"
#include "Engine/Serialization/Format/XmlSerializer.h"

namespace sw
{
    SW_LOG_CALLER( "RenderPipelineResource" );

    bool RenderPipelineResource::loadFromXmlFile( string_view assetRelativePath )
    {
        _desc = {};
        if ( RenderResourceXml::loadDesc( assetRelativePath, _desc ) == false )
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
            for ( const RenderPassAttachment& att : _desc._listAttachment )
            {
                if ( att._name == name )
                    return &att;
            }
            return nullptr;
        };

        // 1) 첨부: 포맷 표기가 RHIFormat 으로 해석되는가, 이름이 겹치지 않는가.
        for ( uint32 attIndex = 0; attIndex < _desc._listAttachment.size(); ++attIndex )
        {
            const RenderPassAttachment& att = _desc._listAttachment[attIndex];
            RHIFormat                   parsed{};
            if ( engine::getTypeRegistry().enumFromString( att._format, parsed ) == false )
            {
                SW_LOG_ERROR( "[%#] attachment '%#': 알 수 없는 포맷 '%#'", sourcePath, att._name, att._format );
                ++issueCount;
            }
            for ( uint32 otherIndex = 0; otherIndex < attIndex; ++otherIndex )
            {
                if ( _desc._listAttachment[otherIndex]._name == att._name )
                {
                    SW_LOG_ERROR( "[%#] attachment '%#' 이름이 중복됐습니다", sourcePath, att._name );
                    ++issueCount;
                    break;
                }
            }
        }

        auto isDepthAttachment = [&]( const RenderPassAttachment* pAtt ) -> bool
        {
            return pAtt != nullptr && engine::getTypeRegistry().enumFromString<RHIFormat>( pAtt->_format ) == constant::kDepthStencilFormat;
        };

        // 2) 패스: 타입 표기가 해석되는가, 입출력이 선언된 첨부를 가리키는가.
        for ( RenderGraphPassDesc& pass : _desc._listPass )
        {
            pass._resolvedType = engine::getTypeRegistry().enumFromString<RenderPassType>( pass._type );
            pass._resolvedDepthAttachment =
                pass._depthAttachment.empty() ? hashed_string{} : hashed_string( pass._depthAttachment.c_str() );
            pass._listResolvedInput.clear();
            pass._listResolvedOutput.clear();
            for ( const string& inputName : pass._listInput )
            {
                RenderGraphPassDesc::ResolvedInput resolved{};
                resolved._attachment = hashed_string( inputName.c_str() );
                resolved._role       = static_cast<uint8>( resolveRenderPassInputRole( inputName, isDepthAttachment( findAttachment( inputName ) ) ) );
                pass._listResolvedInput.push_back( resolved );
            }
            for ( const string& outputName : pass._listOutput )
                pass._listResolvedOutput.emplace_back( outputName.c_str() );
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
            // 적었으면 그 첨부가 실재하고 뎁스 포맷이어야 한다 — 컬러 첨부를 뎁스로 바인딩하면
            // 렌더패스가 통째로 비호환이 된다.
            if ( pass._depthAttachment.empty() == false )
            {
                const RenderPassAttachment* pDepthAtt = findAttachment( pass._depthAttachment );
                if ( pDepthAtt == nullptr )
                {
                    SW_LOG_ERROR( "[%#] pass '%#': 뎁스 첨부 '%#' 이 _attachments 에 없습니다",
                                  sourcePath, pass._name, pass._depthAttachment );
                    ++issueCount;
                }
                else if ( engine::getTypeRegistry().enumFromString<RHIFormat>( pDepthAtt->_format ) != constant::kDepthStencilFormat )
                {
                    SW_LOG_ERROR( "[%#] pass '%#': 뎁스 첨부 '%#' 의 포맷이 '%#' 입니다 — 뎁스 포맷이어야 합니다",
                                  sourcePath, pass._name, pass._depthAttachment, pDepthAtt->_format );
                    ++issueCount;
                }
            }
            else
            {
                // 뎁스를 쓰겠다고 출력에 적어 놓고 바인딩은 안 하는 것은 앞뒤가 안 맞는다.
                for ( const string& outputName : pass._listOutput )
                {
                    const RenderPassAttachment* pAtt = findAttachment( outputName );
                    if ( pAtt == nullptr || engine::getTypeRegistry().enumFromString<RHIFormat>( pAtt->_format ) != constant::kDepthStencilFormat )
                        continue;
                    SW_LOG_ERROR( "[%#] pass '%#': 뎁스 첨부 '%#' 을 출력으로 선언했는데 _depthAttachment 가 비어 "
                                  "있습니다 — 이대로면 뎁스 없이 그립니다",
                                  sourcePath, pass._name, outputName );
                    ++issueCount;
                }
            }
        }

        // 3) 컬러 첨부가 여러 개인 패스는 MRT 로 묶인다 — 포맷이 서로 달라도 되지만 개수 한계는 있다.
        for ( const RenderGraphPassDesc& pass : _desc._listPass )
        {
            uint32 colorCount{ 0 };
            for ( const string& outputName : pass._listOutput )
            {
                const RenderPassAttachment* pAtt = findAttachment( outputName );
                if ( pAtt == nullptr )
                    continue;
                RHIFormat fmt{};
                if ( engine::getTypeRegistry().enumFromString( pAtt->_format, fmt ) && fmt != RHIFormat::D24_UNORM_S8_UINT )
                    ++colorCount;
            }
            if ( colorCount > kMaxColorAttachments )
            {
                SW_LOG_ERROR( "[%#] pass '%#': 컬러 출력이 %#개로 한계(%#)를 넘습니다",
                              sourcePath, pass._name, colorCount, static_cast<uint32>( kMaxColorAttachments ) );
                ++issueCount;
            }
        }

        // 4) 풀스크린 패스의 입력이 그 타입의 계약과 맞는가 — "선언만 있고 아무도 안 읽는 입력" 을 여기서 잡는다.
        //    디퍼드 XML 이 Bloom 의 입력으로 AOColor 를 적어 두었지만 Bloom 코드가 그것을 걸지 않아 SSAO 가 매 프레임
        //    돌고 버려졌던 것(백로그 1-6)이 이 검사가 없어서였다. 실행은 같은 해석(_listResolvedInput)을 그대로 건다.
        for ( const RenderGraphPassDesc& pass : _desc._listPass )
        {
            const RenderPassInputContract* pContract = findRenderPassInputContract( pass._resolvedType );
            if ( pContract == nullptr )
                continue;

            uint32 sourceColorCount{ 0 };
            for ( const RenderGraphPassDesc::ResolvedInput& input : pass._listResolvedInput )
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
                for ( const RenderGraphPassDesc::ResolvedInput& input : pass._listResolvedInput )
                {
                    if ( static_cast<RenderPassInputRole>( input._role ) == required )
                    {
                        bFound = true;
                        break;
                    }
                }
                if ( bFound )
                    continue;
                SW_LOG_ERROR( "[%#] pass '%#'(%#): 필수 입력(역할 %#)이 선언되지 않았습니다 — 셰이더가 SW_INVALID_INDEX 를 읽습니다",
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
        if ( RenderResourceXml::saveDesc( assetRelativePath, _desc ) == false )
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
        pResource->loadFromXmlFile( args.get<string>( 1 ) );
    }

} // namespace sw
