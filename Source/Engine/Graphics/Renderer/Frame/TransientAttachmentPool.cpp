#include "pch.h"

#include "Engine/Graphics/Renderer/Frame/TransientAttachmentPool.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"

namespace sw
{
    SW_LOG_CALLER( "TransientAttachmentPool" );

    TransientAttachmentPool::TransientAttachmentPool()
        : _mapAttachment{}
        , _listClearedThisFrame{}
        , _clearedMutex{}
        , _width{ 0 }
        , _height{ 0 }
    {
    }

    void TransientAttachmentPool::setSize( uint32 width, uint32 height )
    {
        _width  = width;
        _height = height;
    }

    bool TransientAttachmentPool::allocate( IRHIDevice* pDevice, string_view name, RHIFormat format, bool bDepth, const float4& clearColor,
                                            uint32 resolutionDivisor )
    {
        return allocateSized( pDevice, name, format, bDepth, clearColor, computeScaledExtent( _width, resolutionDivisor ),
                              computeScaledExtent( _height, resolutionDivisor ) );
    }

    bool TransientAttachmentPool::allocateSized( IRHIDevice* pDevice, string_view name, RHIFormat format, bool bDepth, const float4& clearColor, uint32 width,
                                                 uint32 height )
    {
        if ( pDevice == nullptr || pDevice->getResourceFactory() == nullptr )
            return false;
        if ( _mapAttachment.find( name ) != _mapAttachment.end() )
            return true;

        RHITextureDesc desc{};
        desc._width                   = width;
        desc._height                  = height;
        desc._format                  = format;
        desc._bIsRenderTarget         = bDepth ? SW_FALSE : SW_TRUE;
        desc._bIsDepthStencil         = bDepth ? SW_TRUE : SW_FALSE;
        desc._bIsShaderResource       = SW_TRUE;
        desc._bIsTransient            = SW_TRUE;
        desc._clearDepth              = clearColor._x;
        desc._clearColor              = clearColor;
        const RHITextureHandle handle = pDevice->getResourceFactory()->createTexture2D( desc );
        if ( handle == 0 )
        {
            SW_LOG_WARNING( "Failed to allocate transient '%#'", name );
            return false;
        }
        const RHIDescriptorIndex srv = pDevice->getResourceFactory()->registerBindlessTexture( handle );
        _mapAttachment.emplace( name, Attachment{ handle, srv, desc._width, desc._height } );
        return true;
    }

    TransientAttachmentPool::Attachment TransientAttachmentPool::find( string_view name ) const
    {
        const auto it = _mapAttachment.find( name );
        return it != _mapAttachment.end() ? it->second : Attachment{};
    }

    RHITextureHandle TransientAttachmentPool::findTexture( string_view name ) const
    {
        const auto it = _mapAttachment.find( name );
        return it != _mapAttachment.end() ? it->second._texture : 0;
    }

    void TransientAttachmentPool::release( IRHIDevice* pDevice )
    {
        if ( pDevice != nullptr && pDevice->getResourceFactory() != nullptr )
        {
            for ( auto& [name, attachment] : _mapAttachment )
            {
                // 텍스처 SRV 인덱스다. 주의: 버퍼용 해제로 넘기면 버퍼 프리리스트가 오염되고, 그 자리를 다른 구조버퍼가
                // 차지해 살아 있는 패스 CB 슬롯이 STORAGE 세트로 바뀐다(Vulkan 검증 에러).
                if ( attachment._srv != kInvalidDescriptorIndex )
                    pDevice->getResourceFactory()->unregisterBindlessTexture( attachment._srv );
                if ( attachment._texture != 0 )
                    pDevice->getResourceFactory()->destroyTexture( attachment._texture );
            }
        }
        forget();
    }

    void TransientAttachmentPool::forget()
    {
        _mapAttachment.clear();
        _width  = 0;
        _height = 0;
    }

    bool TransientAttachmentPool::markCleared( const hashed_string& key )
    {
        std::scoped_lock<mutex> lock{ _clearedMutex };
        if ( std::find( _listClearedThisFrame.begin(), _listClearedThisFrame.end(), key ) != _listClearedThisFrame.end() )
            return false;
        _listClearedThisFrame.push_back( key );
        return true;
    }

    void TransientAttachmentPool::resetCleared()
    {
        std::scoped_lock<mutex> lock{ _clearedMutex };
        _listClearedThisFrame.clear();
    }
} // namespace sw
