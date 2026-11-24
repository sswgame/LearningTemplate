#include "pch.h"

#include "Engine/Renderer/Frame/View/FrameViewHistory.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHIDescriptors.h"

namespace sw
{
    bool FrameViewHistory::ensure( IRHIDevice& device, const RHITextureDesc& desc, const utf8* pDebugName )
    {
        const RHITextureHandle first  = _arrTexture[0]._texture;
        const RHITextureHandle second = _arrTexture[1]._texture;
        const bool             bReady = _arrTexture[0].ensure( device, desc, pDebugName ) && ( _layout == FrameViewHistoryLayout::Copy || _arrTexture[1].ensure( device, desc, pDebugName ) );
        if ( bReady == false )
        {
            release( &device );
            return false;
        }
        if ( _arrTexture[0]._texture != first || _arrTexture[1]._texture != second )
        {
            _bHasContent = _bWritten; // 이번 프레임에 쓰기로 정한 뒤 만들었으면 이 프레임 끝에는 내용이 있다
            _bReset      = SW_TRUE;
        }
        return true;
    }

    void FrameViewHistory::release( IRHIDevice* pDevice )
    {
        for ( RHITextureSlot& texture : _arrTexture )
        {
            texture.release( pDevice );
        }
        _bWritten    = SW_FALSE;
        _bReset      = SW_TRUE;
        _bHasContent = SW_FALSE;
    }
} // namespace sw
