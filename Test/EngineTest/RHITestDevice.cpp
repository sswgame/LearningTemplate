#include "pch.h"

#include "EngineTest/RHITestDevice.h"

#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHIBackendInfo.h"

#include "TestFramework/TestRegistry.h"
#include "TestFramework/TestScopes.h"

namespace test
{
    RHITestDevice::RHITestDevice( sw::RHIBackend backend )
    {
        initialize( backend );
    }

    RHITestDevice::RHITestDevice( std::initializer_list<sw::RHIBackend> listBackend )
    {
        for ( const sw::RHIBackend backend : listBackend )
        {
            if ( initialize( backend ) )
                return;
        }
    }

    RHITestDevice::~RHITestDevice()
    {
        shutdown();
    }

    bool RHITestDevice::initialize( sw::RHIBackend backend )
    {
        shutdown();
        _backend = backend;

        // 빌드에 없는 백엔드(리눅스의 DX · 배포본의 고르지 않은 백엔드)는 구성 차이라 요구하지 않는다. 그 밖에 서지 못한 것은
        // 이 실행이 요구한 백엔드면(`--required_backends=`) 케이스를 실패로 기록한다 — 건너뜀으로 초록이 되지 않게.
        if ( isBackendInThisBuild( backend ) == false )
            return false;
        if ( sw::RHIAvailability::isAvailable( backend ) == false )
        {
            (void)reportUnusableBackend( backend, "no runtime or driver for it", __FILE__, __LINE__ ); // 요구 여부와 상관없이 이 백엔드는 서지 않았다
            return false;
        }

        _window = sw::IWindow::createPlatformWindow();
        if ( _window == nullptr || _window->initializeWindow( "RHITestDevice", 320, 240 ) == false )
        {
            _window.reset();
            (void)reportUnusableBackend( backend, "the test window could not be created", __FILE__, __LINE__ ); // 위와 같다
            return false;
        }

        if ( recreateDevice() == false )
        {
            shutdown();
            (void)reportUnusableBackend( backend, "the device failed to initialize", __FILE__, __LINE__ ); // 위와 같다
            return false;
        }
        return true;
    }

    void RHITestDevice::shutdown()
    {
        shutdownDevice();
        if ( _window != nullptr )
        {
            _window->destroy();
            _window.reset();
        }
    }

    void RHITestDevice::shutdownDevice()
    {
        if ( _device == nullptr )
            return;
        _device->waitIdle();
        _device->shutdown();
        _device.reset();
    }

    RHIBackendSweep::RHIBackendSweep()
        : _device{}
        , _pTrace{}
        , _listBackend( std::begin( kArrAllRHIBackend ), std::end( kArrAllRHIBackend ) )
        , _nextIndex{ 0 }
        , _readyCount{ 0 }
    {
    }

    RHIBackendSweep::RHIBackendSweep( std::initializer_list<sw::RHIBackend> listBackend )
        : _device{}
        , _pTrace{}
        , _listBackend( listBackend.begin(), listBackend.end() )
        , _nextIndex{ 0 }
        , _readyCount{ 0 }
    {
    }

    RHIBackendSweep::RHIBackendSweep( const sw::vector<sw::RHIBackend>& listBackend )
        : _device{}
        , _pTrace{}
        , _listBackend( listBackend.begin(), listBackend.end() )
        , _nextIndex{ 0 }
        , _readyCount{ 0 }
    {
    }

    RHIBackendSweep::Iterator RHIBackendSweep::begin()
    {
        advance();
        return Iterator{ this };
    }

    RHIBackendSweep::~RHIBackendSweep() = default;

    void RHIBackendSweep::advance()
    {
        _pTrace.reset();
        _device.shutdown();
        while ( _nextIndex < _listBackend.size() )
        {
            const sw::RHIBackend backend = _listBackend[_nextIndex];
            ++_nextIndex;
            if ( _device.initialize( backend ) )
            {
                ++_readyCount;
                const sw::RHIBackendInfo* pInfo = sw::RHIBackendInfoTable::find( backend );
                _pTrace                         = sw::make_unique<ScopedTrace>( sw::string( "backend=" ) + ( pInfo != nullptr ? pInfo->_pName : "?" ) );
                return;
            }
        }
    }

    bool RHITestDevice::recreateDevice()
    {
        shutdownDevice();
        if ( _window == nullptr )
            return false;

        sw::unique_ptr<sw::IRHIDevice> device = sw::RHI::createDevice( _backend );
        if ( device == nullptr )
            return false;
        device->setRenderSurface( _window.get() );
        if ( device->initialize() == false )
            return false;

        _device = std::move( device );
        return true;
    }
} // namespace test
