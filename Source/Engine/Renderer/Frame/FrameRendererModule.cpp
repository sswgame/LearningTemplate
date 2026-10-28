#include "pch.h"

#include "Core/Common/HashUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Debug/GPUBreadcrumbs.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Module/ModuleRenderPassRegistry.h"

namespace sw
{
    SW_LOG_CALLER( "FrameRenderer" );

    namespace
    {
        /**
         * @brief 패스 상수를 쓰고 소멸에서 되돌립니다. 이름은 엔진 패스 상수 칸(PassCB)의 이름이라 같은 컨텍스트의 뒤 드로우가 같은 칸을 읽습니다 — 쓰기 전의 값을 떠 두었다가
         *        되돌립니다(없던 칸은 지웁니다). 되돌리지 않으면 요소 하나가 빌린 칸의 값이 뒤 요소의 그림을 바꾼다.
         */
        template <typename TContext>
        class ModuleConstantScope
        {
        public:
            explicit ModuleConstantScope( TContext& context )
                : _context{ context }
            {
            }
            ~ModuleConstantScope()
            {
                for ( const Saved& saved : _listSaved )
                {
                    if ( saved._bExisted )
                        _context._passValues.setFloat4( saved._name, saved._value );
                    else
                        _context._passValues.remove( saved._name );
                }
            }
            ModuleConstantScope( const ModuleConstantScope& )            = delete;
            ModuleConstantScope& operator=( const ModuleConstantScope& ) = delete;

            void apply( const ModuleConstant* pConstant, uint32 count )
            {
                for ( uint32 index = 0; index < count; ++index )
                {
                    Saved saved;
                    saved._name = pConstant[index]._name;
                    uint32       valueSize{ 0 };
                    const uint8* pBytes = _context._passValues.find( saved._name, valueSize );
                    if ( pBytes != nullptr && valueSize == sizeof( float4 ) )
                    {
                        saved._bExisted = true;
                        Memory::copy( &saved._value, pBytes, sizeof( float4 ) );
                    }
                    _listSaved.push_back( saved );
                    _context._passValues.setFloat4( saved._name, pConstant[index]._value );
                }
            }

        private:
            struct Saved
            {
                hashed_string _name{};
                float4        _value{};
                bool          _bExisted{ false };
            };
            TContext&     _context;
            vector<Saved> _listSaved;
        };

        /** @brief 모듈 패스 · 요소의 오류를 키마다 한 번만 남깁니다(그리는 길은 프레임마다 돌아 같은 줄이 쌓이지 않게). */
        void logModuleFailureOnce( string_view key, const char* pReason, string_view detail )
        {
            ModuleRenderPassRegistry* const pRegistry = engine::findSubsystem<ModuleRenderPassRegistry>();
            if ( pRegistry == nullptr || pRegistry->noteFailureOnce( key ) )
                SW_LOG_ERROR( "Module render: %# '%#'", pReason, detail );
        }

        uint64 hashText( const string& text )
        {
            return text.empty() ? 0ull : static_cast<uint64>( hashed_string( text.c_str() ).getHash() );
        }
    } // namespace

    bool ModulePassContext::drawFullscreen( vector_reference<const ModuleConstant> constants )
    {
        return _pRenderer->drawModuleFullscreen( *static_cast<FrameRenderer::FramePassContext*>( _pPassContext ), *static_cast<const FrameRenderer::ModuleFramePass*>( _pPass ), constants );
    }

    bool ModulePassContext::drawProxyElements( vector_reference<const ModuleConstant> constants )
    {
        return _pRenderer->drawModuleElements( *static_cast<FrameRenderer::FramePassContext*>( _pPassContext ), *static_cast<const FrameRenderer::ModuleFramePass*>( _pPass ), constants );
    }

    void FrameRenderer::collectModuleFrame( ModuleRenderPassRegistry& registry )
    {
        _listModuleFramePass.clear();
        _listModuleTexture.clear();
        _listModuleDrawElement.clear();
        _moduleFrameSignature = 0;
        if ( _pDevice == nullptr )
            return;

        // 1) 프록시 갱신 적용 → 확장 훅 → 프록시 요소. 모두 모듈 코드이고 렌더 스레드의 모듈 구간(RenderScope) 안이다.
        registry.applyPendingProxyUpdates();
        const ModuleFrameInfo           frameInfo{ _outputWidth, _outputHeight };
        ModulePassBuilder               builder;
        vector<IModuleRenderExtension*> listExtension;
        registry.copyExtensions( listExtension );
        for ( IModuleRenderExtension* pExtension : listExtension )
        {
            pExtension->addPasses( builder, frameInfo );
        }
        ModuleDrawCollector        collector;
        vector<IModuleSceneProxy*> listProxy;
        registry.copyProxies( listProxy );
        for ( const IModuleSceneProxy* pProxy : listProxy )
        {
            pProxy->collectDrawElements( collector, frameInfo );
        }
        _listModuleDrawElement = collector.getElements();
        for ( const ModuleDrawElement& element : _listModuleDrawElement )
        {
            (void)resolveModuleMesh( registry, element._mesh ); // 메시는 기록 전에 읽고 올린다(기록 중에는 표를 읽기만 한다)
        }
        _listModuleTexture = builder.getTextures();

        // 2) 패스 선언을 검사한다. 어긋난 패스는 이유를 한 번 남기고 넣지 않는다(다른 첨부 · 셰이더로 대신 그리지 않는다).
        const vector<RenderPassAttachment>& listAttachment = _pipelineResource.getDesc()._listAttachment;
        const vector<string>&               listShader     = _pipelineResource.getDesc()._listModuleShader;
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
            for ( const ModuleTextureDesc& texture : _listModuleTexture )
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
        for ( const ModulePassBuilder::Entry& entry : builder.getEntries() )
        {
            const ModulePassParams& params = entry._params;
            auto                    fail   = [&]( const char* pReason )
            {
                logModuleFailureOnce( string( "pass:" ) + params._name + ":" + pReason, pReason, params._name );
            };
            if ( params._name.empty() )
            {
                fail( "pass has no name" );
                continue;
            }
            ModuleFramePass pass;
            pass._graphName = hashed_string( ( string( "Module." ) + params._name ).c_str() );
            if ( VectorUtil::containsIf( _listModuleFramePass, [&]( const ModuleFramePass& existing )
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
            for ( const ModulePassRead& read : params._listRead )
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
                    fail( "pass program shader is not in the pipeline _listModuleShader (it would not be cooked)" );
                    bValid = false;
                }
                else
                {
                    pass._pso = findOrCreateModuleProgramPSO( params._program, colorFormat, params._depthAttachment.empty() == false );
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
            for ( const ModulePassRead& read : params._listRead )
            {
                signature = HashUtil::combine( signature, hashText( read._attachment ) );
                signature = HashUtil::combine( signature, static_cast<uint64>( read._role ) );
            }
            for ( const string& write : params._listWrite )
            {
                signature = HashUtil::combine( signature, hashText( write ) );
            }
            signature = HashUtil::combine( signature, hashText( params._depthAttachment ) );
            _listModuleFramePass.push_back( std::move( pass ) );
        }
        if ( _listModuleFramePass.empty() == false )
            _moduleFrameSignature = signature != 0 ? signature : 1ull; // 0 은 "모듈 패스 없음" 이다

        // 3) 선언이 그래프에 묶인 것과 다르면(처음 · 확장 등록 · 해제 · 선언 변경) 다음 `bindPassCallbacks` 가 모듈 노드를 새로 넣는다.
        if ( _moduleFrameSignature != _moduleBoundSignature )
            _bCallbacksBound = SW_FALSE;

        allocateModuleTextures( _mainView );
        for ( unique_ptr<ViewTarget>& pView : _listExtraView )
        {
            if ( pView->_bRenderThisFrame == SW_TRUE )
                allocateModuleTextures( *pView );
        }
    }

    void FrameRenderer::releaseModuleFrame()
    {
        // 모듈이 넣은 콜백(람다)의 소멸 코드는 모듈에 있다 — 모듈 구간(RenderScope) 안에서 놓는다. 그래프 노드(이름뿐)는 남아도 모듈 코드를 가리키지 않는다.
        _listModuleFramePass.clear();
        _listModuleTexture.clear();
        _listModuleDrawElement.clear();
        _moduleFrameSignature = 0;
    }

    void FrameRenderer::allocateModuleTextures( ViewTarget& view )
    {
        if ( _pDevice == nullptr || view._transientPool.isEmpty() )
            return;
        for ( const ModuleTextureDesc& texture : _listModuleTexture )
        {
            if ( view._transientPool.contains( texture._name ) )
                continue;
            (void)view._transientPool.allocate( _pDevice, texture._name, texture._format, FrameRendererUtil::isDepthFormat( texture._format ), texture._clearColor,
                                                texture._resolutionDivisor );
        }
    }

    Mesh* FrameRenderer::resolveModuleMesh( ModuleRenderPassRegistry& registry, const hashed_string& meshID )
    {
        shared_ptr<Mesh> pMesh;
        const auto       it = _mapModuleMesh.find( meshID );
        if ( it != _mapModuleMesh.end() )
        {
            pMesh = it->second;
        }
        else
        {
            // 철자는 `MeshComponent::_meshID` 와 같다 — `.mesh` 경로면 메시 캐시, 아니면 내장 도형 이름이다. 둘 다 약한 참조 캐시라 렌더러가 쥔다.
            pMesh = MeshAssetFormat::isMeshAssetPath( meshID.view() ) ? MeshCache::acquire( meshID.view() ) : MeshUtil::acquirePrimitive( meshID.view() );
            if ( pMesh == nullptr )
            {
                (void)registry;
                logModuleFailureOnce( string( "mesh:" ) + string( meshID.view() ), "draw element mesh could not be loaded - the element is not drawn", meshID.view() );
                return nullptr;
            }
            _mapModuleMesh.insert_or_assign( meshID, pMesh );
        }
        if ( pMesh->initRHI( _pDevice ) == false )
        {
            logModuleFailureOnce( string( "meshgpu:" ) + string( meshID.view() ), "draw element mesh could not be uploaded - the element is not drawn", meshID.view() );
            return nullptr;
        }
        return pMesh.get();
    }

    void FrameRenderer::executeModuleExtensionPass( FramePassContext& context, const ModuleFramePass& pass )
    {
        if ( context._pCmd == nullptr )
            return;
        GPUBreadcrumbs::mark( *context._pCmd, pass._graphName.view() );
        context._pCmd->beginEventMarker( pass._graphName.c_str() );
        context._resourceRegistry.reset();
        context._passType = RenderPassType::Invalid;
        registerLightBuffer( context );
        ModuleViewInfo view{};
        view._bMainView = _pActiveView == &_mainView;
        view._width     = activePool().getWidth();
        view._height    = activePool().getHeight();
        ModulePassContext moduleContext( *this, &context, &pass, view );
        pass._execute( moduleContext );
        context._pCmd->endEventMarker();
    }

    bool FrameRenderer::drawModuleFullscreen( FramePassContext& context, const ModuleFramePass& pass, vector_reference<const ModuleConstant> listConstant )
    {
        if ( pass._pso == 0 || context._pCmd == nullptr )
        {
            logModuleFailureOnce( string( "nodraw:" ) + string( pass._graphName.view() ), "pass has no program - nothing is drawn", pass._graphName.view() );
            return false;
        }
        if ( listConstant.size() > kModuleMaxConstantCount )
        {
            logModuleFailureOnce( string( "constants:" ) + string( pass._graphName.view() ), "pass has too many constants - nothing is drawn", pass._graphName.view() );
            return false;
        }
        for ( const ModulePassRead& read : pass._params._listRead )
        {
            // 이 뷰에서 빠진 첨부(꺼진 효과의 출력)는 걸지 않는다 — 셰이더는 그 역할의 기본값을 쓴다.
            if ( activePool().resolveName( read._attachment ).empty() == false )
                registerPassTexture( context, inputRoleName( read._role ), read._attachment );
        }
        ModuleConstantScope<FramePassContext> constantScope( context );
        constantScope.apply( listConstant.data(), static_cast<uint32>( listConstant.size() ) );
        const float4              targetClear = getTargetClearColor( pass._colorTarget.view() );
        const RHIRenderPassLoadOp depthLoad   = pass._depthTarget.empty() ? RHIRenderPassLoadOp::Load : colorLoadFor( pass._depthTarget );
        if ( beginColorPass( context, pass._colorTarget.view(), pass._depthTarget.view(), targetClear, colorLoadFor( pass._colorTarget ), depthLoad ) == false )
            return false;
        drawFullscreen( context, pass._pso );
        context._pCmd->endRenderPass();
        return true;
    }

    bool FrameRenderer::drawModuleElements( FramePassContext& context, const ModuleFramePass& pass, vector_reference<const ModuleConstant> listConstant )
    {
        if ( pass._pso == 0 || context._pCmd == nullptr )
        {
            logModuleFailureOnce( string( "nodraw:" ) + string( pass._graphName.view() ), "pass has no program - nothing is drawn", pass._graphName.view() );
            return false;
        }
        if ( listConstant.size() > kModuleMaxConstantCount )
        {
            logModuleFailureOnce( string( "constants:" ) + string( pass._graphName.view() ), "pass has too many constants - nothing is drawn", pass._graphName.view() );
            return false;
        }
        if ( _listModuleDrawElement.empty() )
            return true; // 그릴 것이 없으면 렌더 패스를 열지 않는다(열면 타깃의 첫 접근이 클리어로 소비된다)
        for ( const ModulePassRead& read : pass._params._listRead )
        {
            if ( activePool().resolveName( read._attachment ).empty() == false )
                registerPassTexture( context, inputRoleName( read._role ), read._attachment );
        }
        ModuleConstantScope<FramePassContext> passScope( context );
        passScope.apply( listConstant.data(), static_cast<uint32>( listConstant.size() ) );
        const float4              targetClear = getTargetClearColor( pass._colorTarget.view() );
        const RHIRenderPassLoadOp depthLoad   = pass._depthTarget.empty() ? RHIRenderPassLoadOp::Load : colorLoadFor( pass._depthTarget );
        if ( beginColorPass( context, pass._colorTarget.view(), pass._depthTarget.view(), targetClear, colorLoadFor( pass._colorTarget ), depthLoad ) == false )
            return false;
        bool bAllDrawn{ true };
        for ( const ModuleDrawElement& element : _listModuleDrawElement )
        {
            // 메시 표는 셋업(`collectModuleFrame`)이 채웠다 — 기록 중에는 읽기만 한다. 없으면 셋업이 이유를 남겼다.
            const auto meshIt = _mapModuleMesh.find( element._mesh );
            if ( meshIt == _mapModuleMesh.end() || meshIt->second == nullptr || meshIt->second->isRHIValid() == false )
            {
                bAllDrawn = false;
                continue;
            }
            const Mesh&  mesh        = *meshIt->second;
            const uint32 vertexCount = mesh.getVertexCount();
            if ( vertexCount == 0 )
                continue;
            // 요소마다 상수가 다르다 — 새 패스 상수 슬롯을 잡는다(앞 요소의 슬롯을 덮으면 GPU 가 나중에 읽는 백엔드에서 모두 마지막 값을 본다). 링이 바닥나면 이 요소부터 그리지 않는다.
            if ( acquirePassCb( context ) == false )
            {
                bAllDrawn = false;
                break;
            }
            ModuleConstantScope<FramePassContext> elementScope( context );
            elementScope.apply( element._arrConstant, element._constantCount );
            context._world = element._world;
            context._pCmd->setVertexBuffer( 0, mesh.getVertexBuffer(), sizeof( RHIVertex ), 0 );
            context._pCmd->setVertexBuffer( constant::kInstanceSlotStreamSlot, 0, 0, 0 );
            context._pCmd->setPipelineState( pass._pso );
            commitBindlessTextureBindings( context );
            bindForDraw( context, pass._pso, kInvalidDescriptorIndex );
            context._pCmd->drawInstanced( vertexCount, element._instanceCount );
        }
        setIdentityWorld( context );
        context._pCmd->endRenderPass();
        return bAllDrawn;
    }
} // namespace sw
