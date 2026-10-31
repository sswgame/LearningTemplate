/**
 * @file FrameExtensionHost.cpp
 * @brief 모듈 렌더 확장의 프레임 받아들이기 — 확장 훅 · 프록시 요소 모으기, 패스 선언 검사, 프로그램 PSO · 임시 텍스처 · 요소 메시 준비입니다.
 */
#include "pch.h"

#include "Engine/Renderer/Frame/Extension/FrameExtensionHost.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Renderer/Extension/RenderExtensionRegistry.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Frame/Resource/FramePSOManager.h"
#include "Engine/Renderer/Frame/View/FrameViewTarget.h"
#include "Engine/Renderer/Pipeline/RenderPipelineAsset.h"

namespace sw
{
    SW_LOG_CALLER( "FrameExtensionHost" );

    namespace
    {
        uint64 hashText( const string& text )
        {
            return text.empty() ? 0ull : static_cast<uint64>( hashed_string( text.c_str() ).getHash() );
        }
    } // namespace

    FrameExtensionHost::FrameExtensionHost( IRHIDevice* const& pDevice, const RenderPipelineAsset& pipeline, FramePSOManager& psoManager )
        : _pDevice{ pDevice }
        , _pipeline{ pipeline }
        , _psoManager{ psoManager }
    {
    }

    void FrameExtensionHost::logFailureOnce( string_view key, const char* pReason, string_view detail )
    {
        RenderExtensionRegistry* const pRegistry = engine::findSubsystem<RenderExtensionRegistry>();
        if ( pRegistry == nullptr || pRegistry->noteFailureOnce( key ) )
            SW_LOG_ERROR( "Extension render: %# '%#'", pReason, detail );
    }

    const Mesh* FrameExtensionHost::findMesh( const hashed_string& meshID ) const
    {
        const auto it = _mapMesh.find( meshID );
        return it != _mapMesh.end() ? it->second.get() : nullptr;
    }

    bool FrameExtensionHost::collectFrame( RenderExtensionRegistry& registry, uint32 outputWidth, uint32 outputHeight )
    {
        releaseFrame();
        if ( _pDevice == nullptr )
            return false;

        // 1) 프록시 갱신 적용 → 확장 훅 → 프록시 요소. 모두 모듈 코드이고 렌더 스레드의 모듈 구간(RenderScope) 안이다.
        registry.applyPendingProxyUpdates();
        // 파이프라인이 확장 셰이더를 하나도 선언하지 않았으면 확장 패스를 받지 않는 파이프라인이다 — 이번 프레임 확장은 쉬고 오류가 아니다
        // (언리얼 SceneViewExtension 의 IsActiveThisFrame). 선언한 파이프라인에서 목록에 빠진 셰이더는 아래 2) 가 오류로 알린다(쿠킹되지 않는다).
        if ( _pipeline.getDesc()._listExtensionShader.empty() )
            return false;
        const ExtensionFrameInfo  frameInfo{ outputWidth, outputHeight };
        ExtensionPassBuilder      builder;
        vector<IRenderExtension*> listExtension;
        registry.copyExtensions( listExtension );
        for ( IRenderExtension* pExtension : listExtension )
        {
            pExtension->addPasses( builder, frameInfo );
        }
        ExtensionDrawCollector        collector;
        vector<IExtensionSceneProxy*> listProxy;
        registry.copyProxies( listProxy );
        for ( const IExtensionSceneProxy* pProxy : listProxy )
        {
            pProxy->collectDrawElements( collector, frameInfo );
        }
        _listDrawElement = collector.getElements();
        for ( const ExtensionDrawElement& element : _listDrawElement )
        {
            (void)resolveMesh( element._mesh ); // 메시는 기록 전에 읽고 올린다(기록 중에는 표를 읽기만 한다)
        }
        _listTexture = builder.getTextures();

        // 2) 패스 선언을 검사한다. 어긋난 패스는 이유를 한 번 남기고 넣지 않는다(다른 첨부 · 셰이더로 대신 그리지 않는다).
        const vector<RenderPassAttachment>& listAttachment = _pipeline.getDesc()._listAttachment;
        const vector<string>&               listShader     = _pipeline.getDesc()._listExtensionShader;
        // 첨부 이름의 포맷을 찾는다 — 파이프라인 XML 의 첨부, 아니면 이번 프레임의 임시 텍스처 요청.
        auto findFormat = [&]( const string& name, RHIFormat& outFormat ) -> bool
        {
            for ( const RenderPassAttachment& attachment : listAttachment )
            {
                if ( attachment._name == name )
                {
                    outFormat = FrameRendererUtil::parseAttachmentFormat( attachment._format );
                    return true;
                }
            }
            for ( const ExtensionTextureDesc& texture : _listTexture )
            {
                if ( texture._name == name )
                {
                    outFormat = texture._format;
                    return true;
                }
            }
            return false;
        };

        uint64 signature{ 0 };
        for ( const ExtensionPassBuilder::Entry& entry : builder.getEntries() )
        {
            const ExtensionPassParams& params = entry._params;
            auto                       fail   = [&]( const char* pReason )
            {
                logFailureOnce( string( "pass:" ) + params._name + ":" + pReason, pReason, params._name );
            };
            if ( params._name.empty() )
            {
                fail( "pass has no name" );
                continue;
            }
            ExtensionFramePass pass;
            pass._graphName = hashed_string( ( string( "Extension." ) + params._name ).c_str() );
            if ( VectorUtil::containsIf( _listPass, [&]( const ExtensionFramePass& existing )
            { return existing._graphName == pass._graphName; } ) )
            {
                fail( "pass name is declared twice" );
                continue;
            }
            if ( entry._execute.isBound() == false )
            {
                fail( "pass has no execute callback" );
                continue;
            }
            if ( params._listWrite.empty() )
            {
                fail( "pass writes nothing (it needs a color target)" );
                continue;
            }
            bool bValid{ true };
            for ( const ExtensionPassRead& read : params._listRead )
            {
                RHIFormat readFormat{ RHIFormat::Unknown };
                if ( findFormat( read._attachment, readFormat ) == false || VectorUtil::contains( params._listWrite, read._attachment ) )
                {
                    fail( "pass reads an unknown attachment, or reads and writes the same one" );
                    bValid = false;
                    break;
                }
            }
            RHIFormat colorFormat{ RHIFormat::Unknown };
            bool      bHasColor{ false };
            for ( const string& write : params._listWrite )
            {
                RHIFormat writeFormat{ RHIFormat::Unknown };
                if ( bValid == false )
                    break;
                if ( findFormat( write, writeFormat ) == false )
                {
                    fail( "pass writes an unknown attachment (the swapchain cannot be written)" );
                    bValid = false;
                    break;
                }
                if ( bHasColor == false && FrameRendererUtil::isDepthFormat( writeFormat ) == false )
                {
                    bHasColor         = true;
                    colorFormat       = writeFormat;
                    pass._colorTarget = hashed_string( write.c_str() );
                }
            }
            if ( bValid && bHasColor == false )
            {
                fail( "pass writes no color attachment" );
                bValid = false;
            }
            if ( bValid && params._depthAttachment.empty() == false )
            {
                RHIFormat depthFormat{ RHIFormat::Unknown };
                if ( findFormat( params._depthAttachment, depthFormat ) == false || FrameRendererUtil::isDepthFormat( depthFormat ) == false )
                {
                    fail( "pass depth attachment is unknown or not a depth format" );
                    bValid = false;
                }
                else
                {
                    pass._depthTarget = hashed_string( params._depthAttachment.c_str() );
                }
            }
            if ( bValid && params._program._shaderPath.empty() == false )
            {
                if ( VectorUtil::contains( listShader, params._program._shaderPath ) == false )
                {
                    fail( "pass program shader is not in the pipeline _listExtensionShader (it would not be cooked)" );
                    bValid = false;
                }
                else
                {
                    pass._pso = _psoManager.findOrCreateExtensionProgramPSO( params._program, colorFormat, params._depthAttachment.empty() == false );
                    if ( pass._pso == 0 )
                    {
                        fail( "pass program PSO could not be created" );
                        bValid = false;
                    }
                }
            }
            if ( bValid == false )
                continue;

            pass._params  = params;
            pass._execute = entry._execute;
            signature     = HashUtil::combine( signature, pass._graphName.getHash() );
            signature     = HashUtil::combine( signature, static_cast<uint64>( params._stage ) );
            for ( const ExtensionPassRead& read : params._listRead )
            {
                signature = HashUtil::combine( signature, hashText( read._attachment ) );
                signature = HashUtil::combine( signature, static_cast<uint64>( read._role ) );
            }
            for ( const string& write : params._listWrite )
            {
                signature = HashUtil::combine( signature, hashText( write ) );
            }
            signature = HashUtil::combine( signature, hashText( params._depthAttachment ) );
            _listPass.push_back( std::move( pass ) );
        }
        if ( _listPass.empty() == false )
            _frameSignature = signature != 0 ? signature : 1ull; // 0 은 "모듈 패스 없음" 이다

        // 3) 선언이 그래프에 묶인 것과 다르면(처음 · 확장 등록 · 해제 · 선언 변경) 다음 `bindPassCallbacks` 가 모듈 노드를 새로 넣는다.
        return _frameSignature != _boundSignature;
    }

    void FrameExtensionHost::releaseFrame()
    {
        // 모듈이 넣은 콜백(람다)의 소멸 코드는 모듈에 있다 — 모듈 구간(RenderScope) 안에서 놓는다. 그래프 노드(이름뿐)는 남아도 모듈 코드를 가리키지 않는다.
        _listPass.clear();
        _listTexture.clear();
        _listDrawElement.clear();
        _frameSignature = 0;
    }

    void FrameExtensionHost::allocateTextures( FrameViewTarget& view ) const
    {
        if ( _pDevice == nullptr || view._transientPool.isEmpty() )
            return;
        for ( const ExtensionTextureDesc& texture : _listTexture )
        {
            if ( view._transientPool.contains( texture._name ) )
                continue;
            (void)view._transientPool.allocate( _pDevice, texture._name, texture._format, FrameRendererUtil::isDepthFormat( texture._format ), texture._clearColor,
                                                texture._resolutionDivisor );
        }
    }

    Mesh* FrameExtensionHost::resolveMesh( const hashed_string& meshID )
    {
        shared_ptr<Mesh> pMesh;
        const auto       it = _mapMesh.find( meshID );
        if ( it != _mapMesh.end() )
        {
            pMesh = it->second;
        }
        else if ( MeshAssetFormat::isMeshAssetPath( meshID.view() ) )
        {
            // 철자는 `MeshComponent::_meshID` 와 같다 — `.mesh` 경로는 워커가 읽고, 다 읽을 때까지 그 요소는 그리지 않는다(렌더 스레드가 디스크를 기다리지 않게 —
            // 언리얼 · 유니티도 메시 스트리밍이 끝나기 전 프레임은 그 드로를 빼거나 낮은 LOD 로 둔다). 메시 캐시는 약한 참조라 렌더러가 쥔다.
            auto loadIt = _mapMeshLoad.find( meshID );
            if ( loadIt == _mapMeshLoad.end() )
                loadIt = _mapMeshLoad.insert_or_assign( meshID, MeshCache::request( meshID.view() ) ).first;
            const MeshHandle& load = loadIt->second;
            if ( load.isLoading() )
                return nullptr;
            pMesh = load.get();
            _mapMeshLoad.erase( loadIt );
            if ( pMesh == nullptr )
            {
                logFailureOnce( string( "mesh:" ) + string( meshID.view() ), "draw element mesh could not be loaded - the element is not drawn", meshID.view() );
                return nullptr;
            }
            _mapMesh.insert_or_assign( meshID, pMesh );
        }
        else
        {
            // 내장 도형은 메모리에서 바로 만든다(디스크가 없다).
            pMesh = MeshUtil::acquirePrimitive( meshID.view() );
            if ( pMesh == nullptr )
            {
                logFailureOnce( string( "mesh:" ) + string( meshID.view() ), "draw element mesh could not be loaded - the element is not drawn", meshID.view() );
                return nullptr;
            }
            _mapMesh.insert_or_assign( meshID, pMesh );
        }
        if ( pMesh->initRHI( _pDevice ) == false )
        {
            logFailureOnce( string( "meshgpu:" ) + string( meshID.view() ), "draw element mesh could not be uploaded - the element is not drawn", meshID.view() );
            return nullptr;
        }
        return pMesh.get();
    }
} // namespace sw
