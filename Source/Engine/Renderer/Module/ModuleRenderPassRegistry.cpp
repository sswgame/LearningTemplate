#include "pch.h"

#include "Engine/Renderer/Module/ModuleRenderPassRegistry.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Log/Logger.h"

namespace sw
{
    SW_LOG_CALLER( "ModuleRenderPass" );

    bool ModuleDrawCollector::add( const ModuleDrawElement& element )
    {
        if ( element._mesh.empty() )
        {
            SW_LOG_ERROR( "Module draw element has no mesh id - the element is rejected" );
            return false;
        }
        if ( element._instanceCount == 0 || element._instanceCount > ModuleDrawElement::kMaxInstanceCount )
        {
            SW_LOG_ERROR( "Module draw element '%#' has instance count %# (valid 1..%#) - the element is rejected", element._mesh.c_str(), element._instanceCount,
                          ModuleDrawElement::kMaxInstanceCount );
            return false;
        }
        if ( element._constantCount > ModuleDrawElement::kMaxConstantCount )
        {
            SW_LOG_ERROR( "Module draw element '%#' has %# constants (limit %#) - the element is rejected", element._mesh.c_str(), element._constantCount,
                          ModuleDrawElement::kMaxConstantCount );
            return false;
        }
        _listElement.push_back( element );
        return true;
    }

    bool ModuleRenderPassRegistry::registerExtension( IModuleRenderExtension* pExtension )
    {
        if ( pExtension == nullptr )
            return false;
        ScopedLock<mutex> lock{ _mutex };
        if ( VectorUtil::contains( _listExtension, pExtension ) )
            return false;
        _listExtension.push_back( pExtension );
        return true;
    }

    void ModuleRenderPassRegistry::unregisterExtension( IModuleRenderExtension* pExtension )
    {
        // flush: 렌더 스레드가 모듈 구간 안이면 그 구간이 끝나기를 기다린다. 이 잠금을 쥔 채 목록에서 빼므로, 뺀 뒤에는 렌더 스레드가 이 확장을 부르지 않는다.
        ScopedLock<mutex> runLock{ _runMutex };
        ScopedLock<mutex> lock{ _mutex };
        (void)VectorUtil::removeValue( _listExtension, pExtension );
    }

    ModuleSceneProxyID ModuleRenderPassRegistry::addProxy( unique_ptr<IModuleSceneProxy> pProxy )
    {
        if ( pProxy == nullptr )
            return 0;
        ScopedLock<mutex>        lock{ _mutex };
        const ModuleSceneProxyID id = _nextProxyID++;
        ProxyEntry               entry;
        entry._id     = id;
        entry._pProxy = std::move( pProxy );
        _listProxy.push_back( std::move( entry ) );
        return id;
    }

    void ModuleRenderPassRegistry::removeProxy( ModuleSceneProxyID id )
    {
        unique_ptr<IModuleSceneProxy> pRemoved;
        vector<PendingUpdate>         listDroppedUpdate;
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

    bool ModuleRenderPassRegistry::enqueueProxyUpdate( ModuleSceneProxyID id, ModuleProxyUpdateFn update )
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

    void ModuleRenderPassRegistry::applyPendingProxyUpdates()
    {
        // 렌더 스레드(RenderScope 안)다. 대기열을 통째로 꺼내 잠금 밖에서 적용한다 — 갱신 함수가 모듈 코드라 잠금을 쥔 채 부르지 않는다.
        vector<PendingUpdate> listUpdate;
        {
            ScopedLock<mutex> lock{ _mutex };
            listUpdate.swap( _listPendingUpdate );
        }
        for ( PendingUpdate& pending : listUpdate )
        {
            IModuleSceneProxy* pProxy{ nullptr };
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

    void ModuleRenderPassRegistry::copyExtensions( vector<IModuleRenderExtension*>& outList ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        outList = _listExtension;
    }

    void ModuleRenderPassRegistry::copyProxies( vector<IModuleSceneProxy*>& outList ) const
    {
        outList.clear();
        ScopedLock<mutex> lock{ _mutex };
        for ( const ProxyEntry& entry : _listProxy )
        {
            outList.push_back( entry._pProxy.get() );
        }
    }

    bool ModuleRenderPassRegistry::noteFailureOnce( string_view key )
    {
        const hashed_string hashedKey{ key };
        ScopedLock<mutex>   lock{ _mutex };
        return _uniqueFailureLogged.insert( hashedKey ).second;
    }

    ModuleRenderExtensionRegistration::ModuleRenderExtensionRegistration( ModuleRenderPassRegistry& registry, IModuleRenderExtension& extension )
        : _registry{ registry }
        , _pExtension{ &extension }
        , _bRegistered{ SW_FALSE }
    {
        _bRegistered = _registry.registerExtension( _pExtension ) ? SW_TRUE : SW_FALSE;
        if ( _bRegistered == SW_FALSE )
            SW_LOG_ERROR( "A module render extension is already registered - the second registration is ignored" );
    }

    ModuleRenderExtensionRegistration::~ModuleRenderExtensionRegistration()
    {
        if ( _bRegistered != SW_FALSE )
            _registry.unregisterExtension( _pExtension );
    }

    ModuleSceneProxyRegistration::ModuleSceneProxyRegistration( ModuleRenderPassRegistry& registry, unique_ptr<IModuleSceneProxy> pProxy )
        : _registry{ registry }
        , _id{ 0 }
    {
        _id = _registry.addProxy( std::move( pProxy ) );
    }

    ModuleSceneProxyRegistration::~ModuleSceneProxyRegistration()
    {
        if ( _id != 0 )
            _registry.removeProxy( _id );
    }

    bool ModuleSceneProxyRegistration::update( ModuleProxyUpdateFn update )
    {
        if ( _id == 0 )
            return false;
        return _registry.enqueueProxyUpdate( _id, std::move( update ) );
    }
} // namespace sw
