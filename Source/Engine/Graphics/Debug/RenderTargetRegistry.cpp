#include "pch.h"

#include "Engine/Graphics/Debug/RenderTargetRegistry.h"

namespace sw
{
    void RenderTargetRegistry::publish( vector<RenderTargetInfo> listTarget )
    {
        {
            std::scoped_lock<mutex> lock{ _mutex };
            _listTarget = std::move( listTarget );
        }
        // 잠금을 놓은 뒤에 올린다. 읽는 쪽은 세대를 보고 목록을 다시 가져갈지 정한다.
        _generation.fetch_add( 1, std::memory_order_release );
    }

    void RenderTargetRegistry::clear()
    {
        {
            std::scoped_lock<mutex> lock{ _mutex };
            _listTarget.clear();
        }
        _generation.fetch_add( 1, std::memory_order_release );
    }

    void RenderTargetRegistry::snapshot( vector<RenderTargetInfo>& outList ) const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        outList = _listTarget;
    }

    uint32 RenderTargetRegistry::requestReadback( RHITextureHandle texture )
    {
        std::scoped_lock<mutex> lock{ _readbackMutex };
        ++_lastRequestSerial;
        _pendingReadbackTexture = texture;
        _pendingReadbackSerial  = _lastRequestSerial;
        return _lastRequestSerial;
    }

    bool RenderTargetRegistry::takeReadbackRequest( RHITextureHandle& outTexture, uint32& outSerial )
    {
        std::scoped_lock<mutex> lock{ _readbackMutex };
        if ( _pendingReadbackSerial == 0 )
            return false;
        outTexture              = _pendingReadbackTexture;
        outSerial               = _pendingReadbackSerial;
        _pendingReadbackTexture = 0;
        _pendingReadbackSerial  = 0;
        return true;
    }

    void RenderTargetRegistry::publishReadback( RenderTargetReadback readback )
    {
        const uint32 serial = readback._serial;
        {
            std::scoped_lock<mutex> lock{ _readbackMutex };
            _readback = std::move( readback );
        }
        _readbackSerial.store( serial, std::memory_order_release );
    }

    bool RenderTargetRegistry::copyReadback( RenderTargetReadback& outReadback ) const
    {
        std::scoped_lock<mutex> lock{ _readbackMutex };
        if ( _readback._serial == 0 )
            return false;
        outReadback = _readback;
        return true;
    }
} // namespace sw
