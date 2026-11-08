#include "pch.h"

#include "Engine/Renderer/Frame/Resource/TransientAttachmentPool.h"

#include "Core/Container/VectorUtil.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Profiling/FrameProfiler.h"

namespace sw
{
    TransientAttachmentPool::TransientAttachmentPool()
        : _mapAttachment{}
        , _mapForward{}
        , _clearedMutex{}
        , _listClearedThisFrame{}
        , _width{ 0 }
        , _height{ 0 }
    {
    }

    void TransientAttachmentPool::setSize( uint32 width, uint32 height )
    {
        _width  = width;
        _height = height;
    }

    bool TransientAttachmentPool::allocate( IRHIDevice* pDevice, const hashed_string& name, RHIFormat format, bool bDepth, const RHIAttachmentOps& ops,
                                            uint32 resolutionDivisor, bool bUnorderedAccess )
    {
        return allocateSized( pDevice, name, format, bDepth, ops, computeScaledExtent( _width, resolutionDivisor ),
                              computeScaledExtent( _height, resolutionDivisor ), bUnorderedAccess );
    }

    bool TransientAttachmentPool::allocateSized( IRHIDevice* pDevice, const hashed_string& name, RHIFormat format, bool bDepth, const RHIAttachmentOps& ops, uint32 width,
                                                 uint32 height, bool bUnorderedAccess )
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
        desc._bIsUnorderedAccess      = bUnorderedAccess ? SW_TRUE : SW_FALSE;
        desc._bIsTransient            = SW_TRUE;
        desc._clearDepth              = ops._clearDepth;
        desc._clearStencil            = ops._clearStencil;
        desc._clearColor              = ops._clearColor;
        const RHITextureHandle handle = pDevice->getResourceFactory()->createTexture2D( desc );
        if ( handle == 0 )
        {
            SW_LOG_WARNING_ID( "TransientAttachmentPool.FailedAllocateTransient", "Failed to allocate transient '%#'", name.c_str() );
            return false;
        }
        pDevice->getResourceFactory()->setTextureDebugName( handle, name.c_str() ); // 캡처 · DRED 가 파이프라인 XML 의 첨부 이름으로 보인다
        const RHIDescriptorIndex srv = pDevice->getResourceFactory()->registerBindlessTexture( handle );
        const RHIDescriptorIndex uav = bUnorderedAccess ? pDevice->getResourceFactory()->registerBindlessTextureUAV( handle ) : kInvalidDescriptorIndex;
        _mapAttachment.emplace( name, Attachment{ handle, srv, uav, desc._width, desc._height, format, ops } );
        SW_PROFILE_COUNT( "RT.Frame.transientAllocations", 1 ); // 새 첨부를 만들었다 — 풀이 재사용하지 못한 횟수
        return true;
    }

    bool TransientAttachmentPool::releaseIfIdle( IRHIDevice* pDevice, const hashed_string& name, bool bUsedThisFrame )
    {
        const auto it = _mapAttachment.find( name );
        if ( it == _mapAttachment.end() || advanceIdleFrames( it->second._idleFrames, bUsedThisFrame ) == false )
            return false;
        // 앞 프레임들이 아직 GPU 에서 이 타깃을 읽을 수 있다 — 백엔드의 해제 큐가 그 프레임들이 끝난 뒤에 지운다(release 와 같은 순서: SRV 먼저).
        if ( pDevice != nullptr && pDevice->getResourceFactory() != nullptr )
        {
            if ( it->second._uav != kInvalidDescriptorIndex )
                pDevice->getResourceFactory()->unregisterBindlessUAV( it->second._uav );
            if ( it->second._srv != kInvalidDescriptorIndex )
                pDevice->getResourceFactory()->unregisterBindlessTexture( it->second._srv );
            if ( it->second._texture != 0 )
                pDevice->getResourceFactory()->destroyTexture( it->second._texture );
        }
        _mapAttachment.erase( it );
        return true;
    }

    TransientAttachmentPool::Attachment TransientAttachmentPool::find( const hashed_string& name ) const
    {
        const auto it = _mapAttachment.find( resolveName( name ) );
        return it != _mapAttachment.end() ? it->second : Attachment{};
    }

    RHITextureHandle TransientAttachmentPool::findTexture( const hashed_string& name ) const
    {
        const auto it = _mapAttachment.find( resolveName( name ) );
        return it != _mapAttachment.end() ? it->second._texture : 0;
    }

    RHITextureHandle TransientAttachmentPool::findOwnTexture( const hashed_string& name ) const
    {
        const auto it = _mapAttachment.find( name );
        return it != _mapAttachment.end() ? it->second._texture : 0;
    }

    hashed_string TransientAttachmentPool::resolveName( const hashed_string& name ) const
    {
        // 고리는 로드 검증이 막는다(출력은 그 패스만 쓰고 원본은 그 패스의 입력이다). 그래도 끝없이 돌지 않게 원본 수만큼만 따라간다.
        hashed_string resolved = name;
        for ( size_t step = 0; step < _mapForward.size(); ++step )
        {
            const auto it = _mapForward.find( resolved );
            if ( it == _mapForward.end() )
                break;
            resolved = it->second;
        }
        return resolved;
    }

    bool TransientAttachmentPool::forward( const hashed_string& name, const hashed_string& source )
    {
        const auto it = _mapForward.find( name );
        if ( it != _mapForward.end() )
        {
            if ( it->second == source )
                return false;
            it->second = source;
            return true;
        }
        _mapForward.emplace( name, source );
        return true;
    }

    bool TransientAttachmentPool::stopForward( const hashed_string& name )
    {
        const auto it = _mapForward.find( name );
        if ( it == _mapForward.end() )
            return false;
        _mapForward.erase( it );
        return true;
    }

    void TransientAttachmentPool::release( IRHIDevice* pDevice )
    {
        if ( pDevice != nullptr && pDevice->getResourceFactory() != nullptr )
        {
            for ( auto& [name, attachment] : _mapAttachment )
            {
                // 텍스처 SRV 인덱스다. 주의: 버퍼용 해제로 넘기면 버퍼 프리리스트가 오염되고, 그 자리를 다른 구조버퍼가
                // 차지해 살아 있는 패스 CB 슬롯이 STORAGE 세트로 바뀐다(Vulkan 검증 에러).
                if ( attachment._uav != kInvalidDescriptorIndex )
                    pDevice->getResourceFactory()->unregisterBindlessUAV( attachment._uav );
                if ( attachment._srv != kInvalidDescriptorIndex )
                    pDevice->getResourceFactory()->unregisterBindlessTexture( attachment._srv );
                if ( attachment._texture != 0 )
                    pDevice->getResourceFactory()->destroyTexture( attachment._texture );
            }
        }
        clear();
    }

    void TransientAttachmentPool::clear()
    {
        _mapAttachment.clear();
        _mapForward.clear();
        _width  = 0;
        _height = 0;
    }

    bool TransientAttachmentPool::markCleared( const hashed_string& key )
    {
        ScopedLock<mutex> lock{ _clearedMutex };
        if ( VectorUtil::contains( _listClearedThisFrame, key ) )
            return false;
        _listClearedThisFrame.push_back( key );
        return true;
    }

    void TransientAttachmentPool::resetCleared()
    {
        ScopedLock<mutex> lock{ _clearedMutex };
        _listClearedThisFrame.clear();
    }
} // namespace sw
