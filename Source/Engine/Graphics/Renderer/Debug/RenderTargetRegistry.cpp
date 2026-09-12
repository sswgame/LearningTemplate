#include "pch.h"

#include "Engine/Graphics/Renderer/Debug/RenderTargetRegistry.h"

namespace sw
{
    void RenderTargetRegistry::publish( vector<RenderTargetInfo> listTarget )
    {
        {
            std::scoped_lock<mutex> lock{ _mutex };
            _listTarget = std::move( listTarget );
        }
        // 락을 놓은 뒤에 올린다 — 읽는 쪽은 세대를 보고 목록을 다시 가져갈지 정한다.
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
} // namespace sw
