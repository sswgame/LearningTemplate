#include "pch.h"

#include "EngineTest/RHITestDevice.h"

#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHICapabilities.h"

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

        // 빌드에 없는 백엔드(리눅스의 DX · 배포본의 고르지 않은 백엔드) · 런타임이 없는 백엔드는 창을 띄우기 전에 거른다.
        if ( isBackendInThisBuild( backend ) == false || sw::RHIAvailability::isAvailable( backend ) == false )
            return false;

        _window = sw::IWindow::createPlatformWindow();
        if ( _window == nullptr || _window->initializeWindow( "RHITestDevice", 320, 240 ) == false )
        {
            _window.reset();
            return false;
        }

        if ( recreateDevice() == false )
        {
            shutdown();
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
