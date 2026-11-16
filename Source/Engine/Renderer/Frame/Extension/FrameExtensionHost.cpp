/**
 * @file FrameExtensionHost.cpp
 * @brief 모듈 렌더 확장의 프레임 받아들이기 — 확장 훅 · 프록시 요소 모으기, 패스 선언 검사, 프로그램 PSO · 임시 텍스처 · 요소 메시 준비입니다.
 */
#include "pch.h"

#include "Engine/Renderer/Frame/Extension/FrameExtensionHost.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Graphics/Mesh/MeshRenderData.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Renderer/Extension/RenderExtensionRegistry.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Frame/Resource/FramePSOManager.h"
#include "Engine/Renderer/Frame/View/FrameViewTarget.h"
#include "Engine/Renderer/Pipeline/RenderPipelineAsset.h"

namespace sw
{
    namespace
    {
        uint64 hashText( const string& text )
        {
            return static_cast<uint64>( hashed_string::computeHash( text ) ); // 같은 값이다 — 프레임마다 서명을 만들 뿐이라 글을 표에 넣지 않는다(빈 글은 0)
        }
    } // namespace

    namespace
    {
        struct FrameExtensionHostInternal
        {
            /** @brief 두 선언이 검사 결과가 같은 선언인지(검사가 읽는 칸이 모두 같은지) 봅니다. */
            static bool isSameDeclaration( const ExtensionPassParams& lhs, const ExtensionPassParams& rhs )
            {
                const bool bSameProgram = lhs._program._shaderPath == rhs._program._shaderPath && lhs._program._bCullBack == rhs._program._bCullBack &&
                                          lhs._program._bDepthWrite == rhs._program._bDepthWrite && lhs._program._bBlend == rhs._program._bBlend;
                const bool bSameReads = lhs._listRead.size() == rhs._listRead.size() &&
                                        std::equal( lhs._listRead.begin(), lhs._listRead.end(), rhs._listRead.begin(), []( const ExtensionPassRead& a, const ExtensionPassRead& b )
                { return a._attachment == b._attachment && a._role == b._role; } );
                return lhs._name == rhs._name && lhs._stage == rhs._stage && lhs._listWrite == rhs._listWrite && lhs._depthAttachment == rhs._depthAttachment && bSameReads &&
                       bSameProgram;
            }

            /** @brief 두 임시 텍스처 요청 목록의 이름 · 포맷(검사와 이름 열쇠가 읽는 칸)이 같은지 봅니다. */
            static bool isSameTextureNames( const vector<ExtensionTextureDesc>& lhs, const vector<ExtensionTextureDesc>& rhs )
            {
                return lhs.size() == rhs.size() && std::equal( lhs.begin(), lhs.end(), rhs.begin(), []( const ExtensionTextureDesc& a, const ExtensionTextureDesc& b )
                { return a._name == b._name && a._format == b._format; } );
            }
        };
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
        if ( pRegistry == nullptr || pRegistry->recordFailureOnce( key ) )
            SW_LOG_ERROR_ID( "RenderExtension.Failed", "Extension render: %# '%#'", pReason, detail );
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
        //    실행 콜백은 프레임마다 새로 와서 결과를 통째로 다시 쓸 수 없다 — 선언 검사(첨부 포맷 찾기 · 이름 intern · 선언 해시)만 선언이 그대로면 지난 결과를 쓴다.
        refreshPassChecks();

        // 2-1) 선언을 검사해 받아들일 패스를 모은다. 프로그램 PSO 는 아래 2-2 가 셰이더를 한 번에 데운 뒤 2-3 이 차례로 만든다.
        struct AcceptedPass
        {
            ExtensionFramePass _pass{};
            RHIFormat          _colorFormat{ RHIFormat::Unknown };
            uint64             _declarationHash{ 0 };
        };
        vector<AcceptedPass>                       listAccepted;
        const vector<ExtensionPassBuilder::Entry>& listEntry = builder.getEntries();
        for ( size_t entryIndex = 0; entryIndex < listEntry.size(); ++entryIndex )
        {
            const ExtensionPassBuilder::Entry& entry  = listEntry[entryIndex];
            const ExtensionPassParams&         params = entry._params;
            auto                               fail   = [&]( const char* pReason )
            {
                logFailureOnce( string( "pass:" ) + params._name + ":" + pReason, pReason, params._name );
            };
            if ( params._name.empty() )
            {
                fail( "pass has no name" );
                continue;
            }
            const PassDeclarationCheck& check = findPassCheck( entryIndex, params );
            if ( VectorUtil::containsIf( listAccepted, [&]( const AcceptedPass& existing )
            { return existing._pass._graphName == check._graphName; } ) )
            {
                fail( "pass name is declared twice" );
                continue;
            }
            if ( entry._execute.isBound() == false )
            {
                fail( "pass has no execute callback" );
                continue;
            }
            if ( check._pFailure != nullptr )
            {
                fail( check._pFailure );
                continue;
            }

            AcceptedPass& accepted      = listAccepted.emplace_back();
            accepted._pass._graphName   = check._graphName;
            accepted._pass._colorTarget = check._colorTarget;
            accepted._pass._depthTarget = check._depthTarget;
            accepted._pass._params      = params;
            accepted._pass._execute     = entry._execute;
            accepted._colorFormat       = check._colorFormat;
            accepted._declarationHash   = check._declarationHash;
        }

        // 2-2) 처음 보는 프로그램의 셰이더를 워커가 한 번에 데운다 — 렌더 스레드가 하나씩 컴파일하지 않는다(`FramePSOManager::createPassPSOS` 와 같은 모양).
        vector<FramePSOManager::ExtensionProgramRequest> listProgramRequest;
        for ( const AcceptedPass& accepted : listAccepted )
        {
            const ExtensionPassParams& params = accepted._pass._params;
            if ( params._program._shaderPath.empty() == false )
                listProgramRequest.push_back( FramePSOManager::ExtensionProgramRequest{ &params._program, accepted._colorFormat, params._depthAttachment.empty() == false } );
        }
        _psoManager.warmExtensionPrograms( listProgramRequest );

        // 2-3) 프로그램 PSO 를 차례로 만들고(드라이버 생성은 직렬이다) 받아들인다.
        uint64 signature{ 0 };
        for ( AcceptedPass& accepted : listAccepted )
        {
            ExtensionFramePass&        pass   = accepted._pass;
            const ExtensionPassParams& params = pass._params;
            if ( params._program._shaderPath.empty() == false )
            {
                pass._pso = _psoManager.findOrCreateExtensionProgramPSO( params._program, accepted._colorFormat, params._depthAttachment.empty() == false );
                if ( pass._pso == 0 )
                {
                    const char* const pReason = "pass program PSO could not be created";
                    logFailureOnce( string( "pass:" ) + params._name + ":" + pReason, pReason, params._name );
                    continue;
                }
            }
            signature = HashUtil::combine( signature, accepted._declarationHash ); // 선언 해시는 검사 결과와 함께 다시 쓴다(글을 프레임마다 해시하지 않는다)
            _listPass.push_back( std::move( pass ) );
        }
        if ( _listPass.empty() == false )
            _frameSignature = signature != 0 ? signature : 1ull; // 0 은 "모듈 패스 없음" 이다

        // 3) 선언이 그래프에 묶인 것과 다르면(처음 · 확장 등록 · 해제 · 선언 변경) 다음 `bindPassCallbacks` 가 모듈 노드를 새로 넣는다.
        return _frameSignature != _boundSignature;
    }

    void FrameExtensionHost::refreshPassChecks()
    {
        // 검사는 파이프라인의 첨부 포맷 · 확장 셰이더 목록과 이번 프레임의 임시 텍스처 이름 · 포맷을 읽는다 — 하나라도 바뀌면 지난 결과를 모두 버린다(핫 리로드 포함).
        const RenderPipelineDesc& desc            = _pipeline.getDesc();
        const bool                bSameAttachment = _listCheckedAttachment.size() == desc._listAttachment.size() &&
                                     std::equal( _listCheckedAttachment.begin(), _listCheckedAttachment.end(), desc._listAttachment.begin(),
                                                 []( const RenderPassAttachment& a, const RenderPassAttachment& b )
        { return a._name == b._name && a._format == b._format; } );
        const bool bSameShader  = _listCheckedShader == desc._listExtensionShader;
        const bool bSameTexture = FrameExtensionHostInternal::isSameTextureNames( _listCheckedTexture, _listTexture );
        if ( bSameAttachment && bSameShader && bSameTexture )
            return;
        _listPassCheck.clear();
        _listCheckedAttachment = desc._listAttachment;
        _listCheckedShader     = desc._listExtensionShader;
        _listCheckedTexture    = _listTexture;
        _listTextureName.clear();
        for ( const ExtensionTextureDesc& texture : _listTexture )
        {
            _listTextureName.push_back( hashed_string( texture._name.c_str() ) ); // 바뀐 프레임에만 intern 한다(`allocateTextures` 는 뷰마다 읽는다)
        }
    }

    const FrameExtensionHost::PassDeclarationCheck& FrameExtensionHost::findPassCheck( size_t entryIndex, const ExtensionPassParams& params )
    {
        // 선언은 보통 프레임마다 같은 차례로 온다 — 같은 자리의 지난 결과와 선언이 같으면 그것을 쓴다.
        if ( entryIndex < _listPassCheck.size() && FrameExtensionHostInternal::isSameDeclaration( _listPassCheck[entryIndex]._params, params ) )
            return _listPassCheck[entryIndex];
        if ( entryIndex >= _listPassCheck.size() )
            _listPassCheck.resize( entryIndex + 1 );
        PassDeclarationCheck& check = _listPassCheck[entryIndex];
        check                       = checkPassDeclaration( params );
        return check;
    }

    bool FrameExtensionHost::findAttachmentFormat( const string& name, RHIFormat& outFormat ) const
    {
        // 첨부 이름의 포맷을 찾는다 — 파이프라인 XML 의 첨부, 아니면 이번 프레임의 임시 텍스처 요청.
        for ( const RenderPassAttachment& attachment : _pipeline.getDesc()._listAttachment )
        {
            if ( attachment._name == name )
            {
                outFormat = attachment._format;
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
    }

    FrameExtensionHost::PassDeclarationCheck FrameExtensionHost::checkPassDeclaration( const ExtensionPassParams& params ) const
    {
        // 차례는 예전 검사와 같다 — 첫 어긋남이 이유다. 이름 · 중복 · 실행 콜백은 부르는 쪽이 본다(다른 패스 · 이번 프레임 콜백에 기댄다).
        PassDeclarationCheck check{};
        check._params          = params;
        check._graphName       = hashed_string( ( string( "Extension." ) + params._name ).c_str() );
        check._declarationHash = HashUtil::combine( HashUtil::combine( uint64{ 0 }, check._graphName.getHash() ), static_cast<uint64>( params._stage ) );
        for ( const ExtensionPassRead& read : params._listRead )
        {
            check._declarationHash = HashUtil::combine( check._declarationHash, hashText( read._attachment ) );
            check._declarationHash = HashUtil::combine( check._declarationHash, static_cast<uint64>( read._role ) );
        }
        for ( const string& write : params._listWrite )
        {
            check._declarationHash = HashUtil::combine( check._declarationHash, hashText( write ) );
        }
        check._declarationHash = HashUtil::combine( check._declarationHash, hashText( params._depthAttachment ) );

        if ( params._listWrite.empty() )
        {
            check._pFailure = "pass writes nothing (it needs a color target)";
            return check;
        }
        for ( const ExtensionPassRead& read : params._listRead )
        {
            RHIFormat readFormat{ RHIFormat::Unknown };
            if ( findAttachmentFormat( read._attachment, readFormat ) == false || VectorUtil::contains( params._listWrite, read._attachment ) )
            {
                check._pFailure = "pass reads an unknown attachment, or reads and writes the same one";
                return check;
            }
        }
        bool bHasColor{ false };
        for ( const string& write : params._listWrite )
        {
            RHIFormat writeFormat{ RHIFormat::Unknown };
            if ( findAttachmentFormat( write, writeFormat ) == false )
            {
                check._pFailure = "pass writes an unknown attachment (the swapchain cannot be written)";
                return check;
            }
            if ( bHasColor == false && FrameRendererUtil::isDepthFormat( writeFormat ) == false )
            {
                bHasColor          = true;
                check._colorFormat = writeFormat;
                check._colorTarget = hashed_string( write.c_str() );
            }
        }
        if ( bHasColor == false )
        {
            check._pFailure = "pass writes no color attachment";
            return check;
        }
        if ( params._depthAttachment.empty() == false )
        {
            RHIFormat depthFormat{ RHIFormat::Unknown };
            if ( findAttachmentFormat( params._depthAttachment, depthFormat ) == false || FrameRendererUtil::isDepthFormat( depthFormat ) == false )
            {
                check._pFailure = "pass depth attachment is unknown or not a depth format";
                return check;
            }
            check._depthTarget = hashed_string( params._depthAttachment.c_str() );
        }
        if ( params._program._shaderPath.empty() == false && VectorUtil::contains( _pipeline.getDesc()._listExtensionShader, params._program._shaderPath ) == false )
            check._pFailure = "pass program shader is not in the pipeline _listExtensionShader (it would not be cooked)";
        return check;
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
        // 모듈 구간 밖(`releaseFrame` 뒤)이면 요청이 비어 있고 열쇠는 다음 `refreshPassChecks` 가 맞춘다.
        SW_ASSERT( _listTexture.empty() || _listTextureName.size() == _listTexture.size() );
        for ( size_t textureIndex = 0; textureIndex < _listTexture.size(); ++textureIndex )
        {
            const ExtensionTextureDesc& texture = _listTexture[textureIndex];
            const hashed_string&        name    = _listTextureName[textureIndex]; // `refreshPassChecks` 가 요청이 바뀐 프레임에만 intern 했다
            if ( view._transientPool.contains( name ) )
                continue;
            // 확장 텍스처는 클리어 값 하나를 적는다 — 깊이 형식이면 그 첫 성분이 깊이다.
            RHIAttachmentOps ops{};
            ops._clearColor = texture._clearColor;
            ops._clearDepth = texture._clearColor._x;
            (void)view._transientPool.allocate( _pDevice, name, texture._format, FrameRendererUtil::isDepthFormat( texture._format ), ops, texture._resolutionDivisor );
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
        if ( pMesh->getRenderData().initRHI( _pDevice ) == false )
        {
            logFailureOnce( string( "meshgpu:" ) + string( meshID.view() ), "draw element mesh could not be uploaded - the element is not drawn", meshID.view() );
            return nullptr;
        }
        return pMesh.get();
    }
} // namespace sw
