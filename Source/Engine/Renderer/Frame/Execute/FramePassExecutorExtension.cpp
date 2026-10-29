#include "pch.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Container/string.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Debug/GPUBreadcrumbs.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Renderer/Extension/RenderExtensionRegistry.h"
#include "Engine/Renderer/Frame/Execute/FramePassExecutor.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"

namespace sw
{
    SW_LOG_CALLER( "FramePassExecutor" );

    namespace
    {
        /**
         * @brief 패스 상수를 쓰고 소멸에서 되돌립니다. 이름은 엔진 패스 상수 칸(PassCB)의 이름이라 같은 컨텍스트의 뒤 드로우가 같은 칸을 읽습니다 — 쓰기 전의 값을 떠 두었다가
         *        되돌립니다(없던 칸은 지웁니다). 되돌리지 않으면 요소 하나가 빌린 칸의 값이 뒤 요소의 그림을 바꾼다.
         */
        template <typename TContext>
        class ExtensionConstantScope
        {
        public:
            explicit ExtensionConstantScope( TContext& context )
                : _context{ context }
            {
            }
            ~ExtensionConstantScope()
            {
                for ( const Saved& saved : _listSaved )
                {
                    if ( saved._bExisted )
                        _context._passValues.setFloat4( saved._name, saved._value );
                    else
                        _context._passValues.remove( saved._name );
                }
            }
            ExtensionConstantScope( const ExtensionConstantScope& )            = delete;
            ExtensionConstantScope& operator=( const ExtensionConstantScope& ) = delete;

            void apply( const ExtensionConstant* pConstant, uint32 count )
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

    } // namespace

    bool ExtensionPassContext::drawFullscreen( vector_reference<const ExtensionConstant> constants )
    {
        return _pExecutor->drawModuleFullscreen( *static_cast<FramePassContext*>( _pPassContext ), *static_cast<const ExtensionFramePass*>( _pPass ), constants );
    }

    bool ExtensionPassContext::drawProxyElements( vector_reference<const ExtensionConstant> constants )
    {
        return _pExecutor->drawExtensionElements( *static_cast<FramePassContext*>( _pPassContext ), *static_cast<const ExtensionFramePass*>( _pPass ), constants );
    }

    void FramePassExecutor::executeExtensionPass( FramePassContext& context, const ExtensionFramePass& pass )
    {
        if ( context._pCmd == nullptr )
            return;
        GPUBreadcrumbs::mark( *context._pCmd, pass._graphName.view() );
        context._pCmd->beginEventMarker( pass._graphName.c_str() );
        context._resourceRegistry.reset();
        context._passType = RenderPassType::Invalid;
        registerLightBuffer( context );
        ExtensionViewInfo view{};
        view._bMainView = _pActiveView == &_state._mainView;
        view._width     = activePool().getWidth();
        view._height    = activePool().getHeight();
        ExtensionPassContext moduleContext( *this, &context, &pass, view );
        pass._execute( moduleContext );
        context._pCmd->endEventMarker();
    }

    bool FramePassExecutor::drawModuleFullscreen( FramePassContext& context, const ExtensionFramePass& pass, vector_reference<const ExtensionConstant> listConstant )
    {
        if ( pass._pso == 0 || context._pCmd == nullptr )
        {
            FrameExtensionHost::logFailureOnce( string( "nodraw:" ) + string( pass._graphName.view() ), "pass has no program - nothing is drawn", pass._graphName.view() );
            return false;
        }
        if ( listConstant.size() > kExtensionMaxConstantCount )
        {
            FrameExtensionHost::logFailureOnce( string( "constants:" ) + string( pass._graphName.view() ), "pass has too many constants - nothing is drawn", pass._graphName.view() );
            return false;
        }
        for ( const ExtensionPassRead& read : pass._params._listRead )
        {
            // 이 뷰에서 빠진 첨부(꺼진 효과의 출력)는 걸지 않는다 — 셰이더는 그 역할의 기본값을 쓴다.
            if ( activePool().resolveName( read._attachment ).empty() == false )
                registerPassTexture( context, inputRoleName( read._role ), read._attachment );
        }
        ExtensionConstantScope<FramePassContext> constantScope( context );
        constantScope.apply( listConstant.data(), static_cast<uint32>( listConstant.size() ) );
        const float4              targetClear = getTargetClearColor( pass._colorTarget.view() );
        const RHIRenderPassLoadOp depthLoad   = pass._depthTarget.empty() ? RHIRenderPassLoadOp::Load : colorLoadFor( pass._depthTarget );
        if ( beginColorPass( context, pass._colorTarget.view(), pass._depthTarget.view(), targetClear, colorLoadFor( pass._colorTarget ), depthLoad ) == false )
            return false;
        drawFullscreen( context, pass._pso );
        context._pCmd->endRenderPass();
        return true;
    }

    bool FramePassExecutor::drawExtensionElements( FramePassContext& context, const ExtensionFramePass& pass, vector_reference<const ExtensionConstant> listConstant )
    {
        if ( pass._pso == 0 || context._pCmd == nullptr )
        {
            FrameExtensionHost::logFailureOnce( string( "nodraw:" ) + string( pass._graphName.view() ), "pass has no program - nothing is drawn", pass._graphName.view() );
            return false;
        }
        if ( listConstant.size() > kExtensionMaxConstantCount )
        {
            FrameExtensionHost::logFailureOnce( string( "constants:" ) + string( pass._graphName.view() ), "pass has too many constants - nothing is drawn", pass._graphName.view() );
            return false;
        }
        if ( _extensionHost.getDrawElements().empty() )
            return true; // 그릴 것이 없으면 렌더 패스를 열지 않는다(열면 타깃의 첫 접근이 클리어로 소비된다)
        for ( const ExtensionPassRead& read : pass._params._listRead )
        {
            if ( activePool().resolveName( read._attachment ).empty() == false )
                registerPassTexture( context, inputRoleName( read._role ), read._attachment );
        }
        ExtensionConstantScope<FramePassContext> passScope( context );
        passScope.apply( listConstant.data(), static_cast<uint32>( listConstant.size() ) );
        const float4              targetClear = getTargetClearColor( pass._colorTarget.view() );
        const RHIRenderPassLoadOp depthLoad   = pass._depthTarget.empty() ? RHIRenderPassLoadOp::Load : colorLoadFor( pass._depthTarget );
        if ( beginColorPass( context, pass._colorTarget.view(), pass._depthTarget.view(), targetClear, colorLoadFor( pass._colorTarget ), depthLoad ) == false )
            return false;
        bool bAllDrawn{ true };
        for ( const ExtensionDrawElement& element : _extensionHost.getDrawElements() )
        {
            // 메시 표는 셋업(`FrameExtensionHost::collectFrame`)이 채웠다 — 기록 중에는 읽기만 한다. 없으면 셋업이 이유를 남겼다.
            const Mesh* pMesh = _extensionHost.findMesh( element._mesh );
            if ( pMesh == nullptr || pMesh->isRHIValid() == false )
            {
                bAllDrawn = false;
                continue;
            }
            const Mesh&  mesh        = *pMesh;
            const uint32 vertexCount = mesh.getVertexCount();
            if ( vertexCount == 0 )
                continue;
            // 요소마다 상수가 다르다 — 새 패스 상수 슬롯을 잡는다(앞 요소의 슬롯을 덮으면 GPU 가 나중에 읽는 백엔드에서 모두 마지막 값을 본다). 링이 바닥나면 이 요소부터 그리지 않는다.
            if ( acquirePassCb( context ) == false )
            {
                bAllDrawn = false;
                break;
            }
            ExtensionConstantScope<FramePassContext> elementScope( context );
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
