#include "pch.h"

#include "Engine/Graphics/Debug/RenderTargetRegistry.h"

#include "Core/Container/VectorUtil.h"

namespace sw
{
    void RenderTargetRegistry::publish( vector<RenderTargetInfo> listTarget )
    {
        {
            ScopedLock<mutex> lock{ _mutex };
            _listTarget = std::move( listTarget );
        }
        // 잠금을 푼 뒤에 세대를 증가시킨다. 읽는 쪽은 세대를 보고 목록을 다시 가져갈지 정한다.
        _generation.fetch_add( 1, std::memory_order_release );
    }

    void RenderTargetRegistry::clear()
    {
        {
            ScopedLock<mutex> lock{ _mutex };
            _listTarget.clear();
        }
        _generation.fetch_add( 1, std::memory_order_release );
    }

    void RenderTargetRegistry::snapshot( vector<RenderTargetInfo>& outList ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        outList = _listTarget;
    }

    bool RenderTargetRegistry::isPublished( RHITextureHandle texture ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        return VectorUtil::containsBy<&RenderTargetInfo::_texture>( _listTarget, texture );
    }

    uint32 RenderTargetRegistry::requestReadback( RHITextureHandle texture )
    {
        ScopedLock<mutex> lock{ _readbackMutex };
        ++_lastRequestSerial;
        _pendingReadbackTexture = texture;
        _pendingReadbackSerial  = _lastRequestSerial;
        return _lastRequestSerial;
    }

    bool RenderTargetRegistry::takeReadbackRequest( RHITextureHandle& outTexture, uint32& outSerial )
    {
        ScopedLock<mutex> lock{ _readbackMutex };
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
            ScopedLock<mutex> lock{ _readbackMutex };
            _readback = std::move( readback );
        }
        _readbackSerial.store( serial, std::memory_order_release );
    }

    bool RenderTargetRegistry::copyReadback( RenderTargetReadback& outReadback ) const
    {
        ScopedLock<mutex> lock{ _readbackMutex };
        if ( _readback._serial == 0 )
            return false;
        outReadback = _readback;
        return true;
    }
} // namespace sw
