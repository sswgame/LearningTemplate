#include "pch.h"

#include "EngineTest/RHITestDevice.h"

#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHIBackendInfo.h"

#include "TestFramework/TestRegistry.h"
#include "TestFramework/TestScopes.h"

#include <algorithm>

namespace test
{
    namespace
    {
        /** @brief 지금 케이스의 매개변수(`-dx12` …)가 백엔드 스위치면 그 백엔드를 @p outBackend 에 담고 true 를 돌려줍니다. 매개변수 케이스가 아니면 false 입니다. */
        bool findCaseBackend( sw::RHIBackend& outBackend )
        {
            const sw::string parameter = getCurrentCaseParameter();
            for ( const sw::RHIBackend backend : kArrAllRHIBackend )
            {
                const sw::string switchText = sw::string( "-" ) + TestRunOptions::getBackendName( backend );
                if ( parameter == switchText )
                {
                    outBackend = backend;
                    return true;
                }
            }
            return false;
        }

        /** @brief 지금 케이스가 백엔드 하나로 가른 케이스면 @p inoutList 를 그 백엔드 하나로 좁힙니다(목록에 없으면 비웁니다). 아니면 그대로 둡니다. */
        void restrictToCaseBackend( sw::vector<sw::RHIBackend>& inoutList )
        {
            sw::RHIBackend caseBackend{};
            if ( findCaseBackend( caseBackend ) == false )
                return;
            const bool bListed = std::find( inoutList.begin(), inoutList.end(), caseBackend ) != inoutList.end();
            inoutList.clear();
            if ( bListed )
                inoutList.push_back( caseBackend );
        }
    } // namespace

    bool matchesCaseBackend( sw::RHIBackend backend )
    {
        sw::RHIBackend caseBackend{};
        return findCaseBackend( caseBackend ) == false || caseBackend == backend;
    }

    sw::vector<sw::string> listRHIBackendParameter()
    {
        sw::vector<sw::string> listParameter;
        for ( const sw::RHIBackend backend : kArrAllRHIBackend )
        {
            listParameter.push_back( sw::string( "-" ) + TestRunOptions::getBackendName( backend ) );
        }
        return listParameter;
    }

    RHITestDevice::RHITestDevice( sw::RHIBackend backend )
    {
        initialize( backend );
    }

    RHITestDevice::RHITestDevice( std::initializer_list<sw::RHIBackend> listBackend )
    {
        sw::vector<sw::RHIBackend> listCandidate( listBackend.begin(), listBackend.end() );
        restrictToCaseBackend( listCandidate ); // 백엔드마다 가른 케이스면 그 백엔드만(목록에 없으면 서지 않는다)
        for ( const sw::RHIBackend backend : listCandidate )
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
        if ( isBackendUnderTest( backend ) == false )
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
        restrictToCaseBackend( _listBackend );
    }

    RHIBackendSweep::RHIBackendSweep( std::initializer_list<sw::RHIBackend> listBackend )
        : _device{}
        , _pTrace{}
        , _listBackend( listBackend.begin(), listBackend.end() )
        , _nextIndex{ 0 }
        , _readyCount{ 0 }
    {
        restrictToCaseBackend( _listBackend );
    }

    RHIBackendSweep::RHIBackendSweep( const sw::vector<sw::RHIBackend>& listBackend )
        : _device{}
        , _pTrace{}
        , _listBackend( listBackend.begin(), listBackend.end() )
        , _nextIndex{ 0 }
        , _readyCount{ 0 }
    {
        restrictToCaseBackend( _listBackend );
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
