#include "pch.h"

#include "Engine/Graphics/Renderer/RenderPipelineResource.h"

#include "Core/String/StringUtil.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Renderer/RenderPassXmlUtil.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Resource/AssetFormat.h"
#include "Engine/Resource/ResourceManager.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw
{
    namespace
    {
        struct RenderPipelineResourceInternal
        {
            static void parsePipelineGraphPasses( XmlNode passesNode, vector<RenderGraphPassDesc>& outListPass )
            {
                outListPass.clear();
                if ( passesNode.isValid() == false )
                    return;

                for ( XmlNode passNode = passesNode.child( "item" ); passNode.isValid(); passNode = passNode.next( "item" ) )
                {
                    RenderGraphPassDesc pass{};
                    const utf8*         pName = passNode.childText( "_name" );
                    if ( pName == nullptr )
                        pName = passNode.attr( "name" );
                    if ( pName != nullptr )
                        pass._name = pName;

                    const utf8* pType = passNode.childText( "_type" );
                    if ( pType == nullptr )
                        pType = passNode.attr( "type" );
                    if ( pType != nullptr )
                        pass._type = pType;

                    RenderPassXmlUtil::parseStringList( passNode, "_inputs", pass._listInput );
                    RenderPassXmlUtil::parseStringList( passNode, "_outputs", pass._listOutput );
                    RenderPassXmlUtil::parseStringList( passNode, "_permutations", pass._listPermutation );

                    const utf8* pShaderPath = passNode.childText( "_shaderPath" );
                    if ( pShaderPath != nullptr )
                        pass._shaderPath = pShaderPath;

                    const utf8* pVertexEntryPoint = passNode.childText( "_vertexEntryPoint" );
                    if ( pVertexEntryPoint != nullptr )
                        pass._vertexEntryPoint = pVertexEntryPoint;

                    const utf8* pPixelEntryPoint = passNode.childText( "_pixelEntryPoint" );
                    if ( pPixelEntryPoint != nullptr )
                        pass._pixelEntryPoint = pPixelEntryPoint;

                    const utf8* pComputeEntryPoint = passNode.childText( "_computeEntryPoint" );
                    if ( pComputeEntryPoint != nullptr )
                        pass._computeEntryPoint = pComputeEntryPoint;

                    const utf8* pGeometryEntryPoint = passNode.childText( "_geometryEntryPoint" );
                    if ( pGeometryEntryPoint != nullptr )
                        pass._geometryEntryPoint = pGeometryEntryPoint;

                    const utf8* pHullEntryPoint = passNode.childText( "_hullEntryPoint" );
                    if ( pHullEntryPoint != nullptr )
                        pass._hullEntryPoint = pHullEntryPoint;

                    const utf8* pDomainEntryPoint = passNode.childText( "_domainEntryPoint" );
                    if ( pDomainEntryPoint != nullptr )
                        pass._domainEntryPoint = pDomainEntryPoint;

                    const utf8* pMeshEntryPoint = passNode.childText( "_meshEntryPoint" );
                    if ( pMeshEntryPoint != nullptr )
                        pass._meshEntryPoint = pMeshEntryPoint;

                    const utf8* pAmplificationEntryPoint = passNode.childText( "_amplificationEntryPoint" );
                    if ( pAmplificationEntryPoint != nullptr )
                        pass._amplificationEntryPoint = pAmplificationEntryPoint;

                    const utf8* pCullMode = passNode.childText( "_cullMode" );
                    if ( pCullMode != nullptr )
                        pass._cullMode = pCullMode;

                    pass._bEnableDepthTest  = passNode.childBool( "_bEnableDepthTest", pass._bEnableDepthTest );
                    pass._bEnableDepthWrite = passNode.childBool( "_bEnableDepthWrite", pass._bEnableDepthWrite );
                    pass._bEnableBlend      = passNode.childBool( "_bEnableBlend", pass._bEnableBlend );

                    outListPass.push_back( std::move( pass ) );
                }

                for ( XmlNode passNode = passesNode.child( "Pass" ); passNode.isValid(); passNode = passNode.next( "Pass" ) )
                {
                    RenderGraphPassDesc pass{};
                    const utf8*         pName = passNode.attr( "name" );
                    if ( pName != nullptr )
                        pass._name = pName;
                    const utf8* pType = passNode.attr( "type" );
                    if ( pType != nullptr )
                        pass._type = pType;
                    RenderPassXmlUtil::parseStringList( passNode, "inputs", pass._listInput );
                    RenderPassXmlUtil::parseStringList( passNode, "outputs", pass._listOutput );
                    if ( pass._listInput.empty() )
                        RenderPassXmlUtil::parseStringList( passNode, "_inputs", pass._listInput );
                    if ( pass._listOutput.empty() )
                        RenderPassXmlUtil::parseStringList( passNode, "_outputs", pass._listOutput );
                    RenderPassXmlUtil::parseStringList( passNode, "_permutations", pass._listPermutation );
                    outListPass.push_back( std::move( pass ) );
                }
            }

            static string guessShadingModel( string_view name, const vector<RenderGraphPassDesc>& listPass )
            {
                const string nameNt( name );
                if ( StringUtil::stristr( nameNt.c_str(), "deferred" ) != nullptr )
                    return "Deferred";
                for ( const RenderGraphPassDesc& passDesc : listPass )
                {
                    if ( passDesc._type == "GBuffer" || passDesc._type == "Shading" || passDesc._type == "Lighting" )
                        return "Deferred";
                }
                return "Forward";
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "RenderPipelineResource" );

    bool RenderPipelineResource::loadFromXmlFile( string_view assetRelativePath )
    {
        string      absPath;
        XmlDocument doc;
        if ( doc.loadPath( assetRelativePath, &absPath ) == false )
        {
            SW_LOG_ERROR( "XML file not found: %#", assetRelativePath );
            return false;
        }

        XmlNode root = doc.root( "RenderPipelineDesc" );
        if ( root.isValid() == false )
        {
            SW_LOG_ERROR( "XML missing root <RenderPipelineDesc>: %#", absPath );
            return false;
        }

        if ( engine::getResourceManager().getAssetFormatRegistry().upgradeXml( AssetKind::RenderPipeline, doc, root, AssetFormatVersions::kRenderPipeline ) == false )
        {
            SW_LOG_ERROR( "formatVersion upgrade failed: %#", absPath );
            return false;
        }

        _desc = {};

        const utf8* pName = root.childText( "_name" );
        if ( pName != nullptr )
            _desc._name = pName;

        const utf8* pShadingModel = root.childText( "_shadingModel" );
        if ( pShadingModel != nullptr )
            _desc._shadingModel = pShadingModel;

        RenderPassXmlUtil::parseAttachmentList( root.child( "_attachments" ), _desc._listAttachment );

        XmlNode passesNode = root.child( "_passes" );
        if ( passesNode.isValid() )
            RenderPipelineResourceInternal::parsePipelineGraphPasses( passesNode, _desc._listPass );

        RenderPassXmlUtil::parseStringList( root, "_renderPassRefs", _desc._listRenderPassRef );

        if ( _desc._shadingModel.empty() || _desc._shadingModel == "Forward" )
            _desc._shadingModel = RenderPipelineResourceInternal::guessShadingModel( _desc._name, _desc._listPass );

        // 여기서 걸러내지 못한 불일치는 전부 런타임에 드러난다 — 그것도 조용히. 파이프라인이
        // 선언한 포맷과 PSO 가 어긋나 GPU 가 통째로 죽은 적이 있다(`ae7fb078`).
        validate( absPath );

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

        // 2) 패스: 타입 표기가 해석되는가, 입출력이 선언된 첨부를 가리키는가.
        for ( RenderGraphPassDesc& pass : _desc._listPass )
        {
            pass._resolvedType = engine::getTypeRegistry().enumFromString<RenderPassType>( pass._type );
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

        if ( issueCount > 0 )
            SW_LOG_ERROR( "[%#] 파이프라인 검증에서 %#건의 문제를 찾았습니다 — 렌더 결과가 어긋나거나 GPU 가 죽을 수 있습니다",
                          sourcePath, issueCount );
        return issueCount;
    }

    bool RenderPipelineResource::saveToXmlFile( string_view assetRelativePath ) const
    {
        string absPath = ResourceUtil::getResourcePath( assetRelativePath );
        if ( absPath.empty() )
            absPath = assetRelativePath;

        XmlDocument doc;
        XmlNode     root = doc.appendRoot( "RenderPipelineDesc" );
        engine::getResourceManager().getAssetFormatRegistry().writeXmlVersion( root, AssetFormatVersions::kRenderPipeline );

        root.appendChild( "_name", _desc._name );
        root.appendChild( "_shadingModel", _desc._shadingModel );

        RenderPassXmlUtil::appendAttachmentList( root, _desc._listAttachment );

        XmlNode passesNode = root.appendChild( "_passes" );
        for ( const RenderGraphPassDesc& pass : _desc._listPass )
        {
            XmlNode passNode = passesNode.appendChild( "item" );
            passNode.appendChild( "_name", pass._name );
            passNode.appendChild( "_type", pass._type );

            RenderPassXmlUtil::appendStringList( passNode, "_inputs", pass._listInput );
            RenderPassXmlUtil::appendStringList( passNode, "_outputs", pass._listOutput );
            if ( pass._shaderPath.empty() == false )
                passNode.appendChild( "_shaderPath", pass._shaderPath );
            if ( pass._vertexEntryPoint.empty() == false )
                passNode.appendChild( "_vertexEntryPoint", pass._vertexEntryPoint );
            if ( pass._pixelEntryPoint.empty() == false )
                passNode.appendChild( "_pixelEntryPoint", pass._pixelEntryPoint );
            if ( pass._computeEntryPoint.empty() == false )
                passNode.appendChild( "_computeEntryPoint", pass._computeEntryPoint );
            if ( pass._geometryEntryPoint.empty() == false )
                passNode.appendChild( "_geometryEntryPoint", pass._geometryEntryPoint );
            if ( pass._hullEntryPoint.empty() == false )
                passNode.appendChild( "_hullEntryPoint", pass._hullEntryPoint );
            if ( pass._domainEntryPoint.empty() == false )
                passNode.appendChild( "_domainEntryPoint", pass._domainEntryPoint );
            if ( pass._meshEntryPoint.empty() == false )
                passNode.appendChild( "_meshEntryPoint", pass._meshEntryPoint );
            if ( pass._amplificationEntryPoint.empty() == false )
                passNode.appendChild( "_amplificationEntryPoint", pass._amplificationEntryPoint );
            if ( pass._cullMode.empty() == false )
                passNode.appendChild( "_cullMode", pass._cullMode );

            passNode.appendChild( "_bEnableDepthTest", pass._bEnableDepthTest );
            passNode.appendChild( "_bEnableDepthWrite", pass._bEnableDepthWrite );
            passNode.appendChild( "_bEnableBlend", pass._bEnableBlend );

            if ( pass._listPermutation.empty() == false )
                RenderPassXmlUtil::appendStringList( passNode, "_permutations", pass._listPermutation );
        }

        if ( _desc._listRenderPassRef.empty() == false )
            RenderPassXmlUtil::appendStringList( root, "_renderPassRefs", _desc._listRenderPassRef );

        if ( doc.saveFile( absPath ) == false )
        {
            SW_LOG_ERROR( "Failed to write: %#", absPath );
            return false;
        }

        SW_LOG_INFO( "Saved '%#' -> %#", _desc._name, absPath );
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
