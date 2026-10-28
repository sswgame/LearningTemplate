#include "pch.h"

#include "Engine/DedicatedServer/ServerServiceManager.h"

namespace sw
{
    SW_LOG_CALLER( "ServerServiceManager" );

    ServerServiceManager::ServerServiceManager()
        : _listRunning{}
        , _bRunning{ SW_FALSE }
    {
    }

    ServerServiceManager::~ServerServiceManager()
    {
        stopAll();
    }

    bool ServerServiceManager::startAll( const ServerServiceContext& context )
    {
        if ( _bRunning == SW_TRUE )
            return true;
        _bRunning = SW_TRUE; // 실패해도 `stopAll` 이 시작한 것까지 멈춘다
        vector<ServerServiceFactory*> listFactory;
        ServerServiceRegistry::collectFactories( listFactory );
        for ( const ServerServiceFactory* pFactory : listFactory )
        {
            RunningService running;
            running._name    = pFactory->_pName;
            running._service = pFactory->_pCreate();
            if ( running._service == nullptr )
            {
                SW_LOG_ERROR( "Server service '%#' could not be created", running._name.c_str() );
                stopAll();
                return false;
            }
            string     error;
            const bool bStarted = running._service->initialize( context, error );
            _listRunning.push_back( std::move( running ) );
            if ( bStarted == false )
            {
                SW_LOG_ERROR( "Server service '%#' failed to start: %#", _listRunning.back()._name.c_str(), error.empty() ? "no reason given" : error.c_str() );
                stopAll();
                return false;
            }
        }
        if ( _listRunning.empty() == false )
            SW_LOG_INFO( "Server services started: %#", static_cast<int32>( _listRunning.size() ) );
        return true;
    }

    void ServerServiceManager::stopAll()
    {
        // 시작의 반대 순서로 멈춘다. 뒤에 시작한 서비스가 앞의 것을 빌려 쓸 수 있기 때문이다.
        while ( _listRunning.empty() == false )
        {
            RunningService& running = _listRunning.back();
            if ( running._service != nullptr )
                running._service->shutdown();
            running._service.reset();
            _listRunning.pop_back();
        }
        _bRunning = SW_FALSE;
    }

    void ServerServiceManager::update( UnixMilliseconds nowMs )
    {
        for ( RunningService& running : _listRunning )
        {
            running._service->update( nowMs );
        }
    }

    int32 ServerServiceManager::getConnectionCount() const
    {
        int32 count = 0;
        for ( const RunningService& running : _listRunning )
        {
            count += running._service->getConnectionCount();
        }
        return count;
    }

    int32 ServerServiceManager::getOpenWorkCount() const
    {
        int32 count = 0;
        for ( const RunningService& running : _listRunning )
        {
            count += running._service->getOpenWorkCount();
        }
        return count;
    }

    void ServerServiceManager::beginShutdown( const ServerShutdownNotice& notice )
    {
        for ( RunningService& running : _listRunning )
        {
            running._service->beginShutdown( notice );
        }
    }
} // namespace sw
