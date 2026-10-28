#include "pch.h"

#include "Engine/Renderer/Extension/RenderExtensionRegistry.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Log/Logger.h"

namespace sw
{
    SW_LOG_CALLER( "ExtensionRenderPass" );

    bool ExtensionDrawCollector::add( const ExtensionDrawElement& element )
    {
        if ( element._mesh.empty() )
        {
            SW_LOG_ERROR( "Extension draw element has no mesh id - the element is rejected" );
            return false;
        }
        if ( element._instanceCount == 0 || element._instanceCount > ExtensionDrawElement::kMaxInstanceCount )
        {
            SW_LOG_ERROR( "Extension draw element '%#' has instance count %# (valid 1..%#) - the element is rejected", element._mesh.c_str(), element._instanceCount,
                          ExtensionDrawElement::kMaxInstanceCount );
            return false;
        }
        if ( element._constantCount > ExtensionDrawElement::kMaxConstantCount )
        {
            SW_LOG_ERROR( "Extension draw element '%#' has %# constants (limit %#) - the element is rejected", element._mesh.c_str(), element._constantCount,
                          ExtensionDrawElement::kMaxConstantCount );
            return false;
        }
        _listElement.push_back( element );
        return true;
    }

    bool RenderExtensionRegistry::registerExtension( IRenderExtension* pExtension )
    {
        if ( pExtension == nullptr )
            return false;
        ScopedLock<mutex> lock{ _mutex };
        if ( VectorUtil::contains( _listExtension, pExtension ) )
            return false;
        _listExtension.push_back( pExtension );
        return true;
    }

    void RenderExtensionRegistry::unregisterExtension( IRenderExtension* pExtension )
    {
        // flush: 렌더 스레드가 모듈 구간 안이면 그 구간이 끝나기를 기다린다. 이 잠금을 쥔 채 목록에서 빼므로, 뺀 뒤에는 렌더 스레드가 이 확장을 부르지 않는다.
        ScopedLock<mutex> runLock{ _runMutex };
        ScopedLock<mutex> lock{ _mutex };
        (void)VectorUtil::removeValue( _listExtension, pExtension );
    }

    ExtensionSceneProxyID RenderExtensionRegistry::addProxy( unique_ptr<IExtensionSceneProxy> pProxy )
    {
        if ( pProxy == nullptr )
            return 0;
        ScopedLock<mutex>           lock{ _mutex };
        const ExtensionSceneProxyID id = _nextProxyID++;
        ProxyEntry                  entry;
        entry._id     = id;
        entry._pProxy = std::move( pProxy );
        _listProxy.push_back( std::move( entry ) );
        return id;
    }

    void RenderExtensionRegistry::removeProxy( ExtensionSceneProxyID id )
    {
        unique_ptr<IExtensionSceneProxy> pRemoved;
        vector<PendingUpdate>            listDroppedUpdate;
        {
            ScopedLock<mutex> runLock{ _runMutex }; // flush
            ScopedLock<mutex> lock{ _mutex };
            for ( size_t index = 0; index < _listProxy.size(); ++index )
            {
                if ( _listProxy[index]._id == id )
                {
                    pRemoved = std::move( _listProxy[index]._pProxy );
                    _listProxy.erase( _listProxy.begin() + static_cast<ptrdiff_t>( index ) );
                    break;
                }
            }
            for ( size_t index = 0; index < _listPendingUpdate.size(); )
            {
                if ( _listPendingUpdate[index]._id == id )
                {
                    listDroppedUpdate.push_back( std::move( _listPendingUpdate[index] ) );
                    _listPendingUpdate.erase( _listPendingUpdate.begin() + static_cast<ptrdiff_t>( index ) );
                }
                else
                {
                    ++index;
                }
            }
        }
        // 소멸은 잠금 밖이다 — 프록시와 갱신 함수의 소멸 코드는 이 함수를 부른 모듈 안에 있다(모듈이 내려가기 전).
        pRemoved.reset();
        listDroppedUpdate.clear();
    }

    bool RenderExtensionRegistry::enqueueProxyUpdate( ExtensionSceneProxyID id, ExtensionProxyUpdateFn update )
    {
        if ( update.isBound() == false )
            return false;
        ScopedLock<mutex> lock{ _mutex };
        bool              bFound{ false };
        for ( const ProxyEntry& entry : _listProxy )
        {
            bFound = bFound || entry._id == id;
        }
        if ( bFound == false )
            return false;
        PendingUpdate pending;
        pending._id     = id;
        pending._update = std::move( update );
        _listPendingUpdate.push_back( std::move( pending ) );
        return true;
    }

    void RenderExtensionRegistry::applyPendingProxyUpdates()
    {
        // 렌더 스레드(RenderScope 안)다. 대기열을 통째로 꺼내 잠금 밖에서 적용한다 — 갱신 함수가 모듈 코드라 잠금을 쥔 채 부르지 않는다.
        vector<PendingUpdate> listUpdate;
        {
            ScopedLock<mutex> lock{ _mutex };
            listUpdate.swap( _listPendingUpdate );
        }
        for ( PendingUpdate& pending : listUpdate )
        {
            IExtensionSceneProxy* pProxy{ nullptr };
            {
                ScopedLock<mutex> lock{ _mutex };
                for ( ProxyEntry& entry : _listProxy )
                {
                    if ( entry._id == pending._id )
                        pProxy = entry._pProxy.get();
                }
            }
            if ( pProxy != nullptr )
                pending._update( *pProxy );
        }
    }

    void RenderExtensionRegistry::copyExtensions( vector<IRenderExtension*>& outList ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        outList = _listExtension;
    }

    void RenderExtensionRegistry::copyProxies( vector<IExtensionSceneProxy*>& outList ) const
    {
        outList.clear();
        ScopedLock<mutex> lock{ _mutex };
        for ( const ProxyEntry& entry : _listProxy )
        {
            outList.push_back( entry._pProxy.get() );
        }
    }

    bool RenderExtensionRegistry::noteFailureOnce( string_view key )
    {
        const hashed_string hashedKey{ key };
        ScopedLock<mutex>   lock{ _mutex };
        return _uniqueFailureLogged.insert( hashedKey ).second;
    }

    RenderExtensionRegistration::RenderExtensionRegistration( RenderExtensionRegistry& registry, IRenderExtension& extension )
        : _registry{ registry }
        , _pExtension{ &extension }
        , _bRegistered{ SW_FALSE }
    {
        _bRegistered = _registry.registerExtension( _pExtension ) ? SW_TRUE : SW_FALSE;
        if ( _bRegistered == SW_FALSE )
            SW_LOG_ERROR( "A module render extension is already registered - the second registration is ignored" );
    }

    RenderExtensionRegistration::~RenderExtensionRegistration()
    {
        if ( _bRegistered != SW_FALSE )
            _registry.unregisterExtension( _pExtension );
    }

    ExtensionSceneProxyRegistration::ExtensionSceneProxyRegistration( RenderExtensionRegistry& registry, unique_ptr<IExtensionSceneProxy> pProxy )
        : _registry{ registry }
        , _id{ 0 }
    {
        _id = _registry.addProxy( std::move( pProxy ) );
    }

    ExtensionSceneProxyRegistration::~ExtensionSceneProxyRegistration()
    {
        if ( _id != 0 )
            _registry.removeProxy( _id );
    }

    bool ExtensionSceneProxyRegistration::update( ExtensionProxyUpdateFn update )
    {
        if ( _id == 0 )
            return false;
        return _registry.enqueueProxyUpdate( _id, std::move( update ) );
    }
} // namespace sw
