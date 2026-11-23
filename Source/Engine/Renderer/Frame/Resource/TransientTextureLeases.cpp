#include "pch.h"

#include "Engine/Renderer/Frame/Resource/TransientTextureLeases.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    uint32 TransientTextureLeases::lease( IRHIDevice& device, const RHITextureDesc& desc, const void* pHolder, const utf8* pDebugName )
    {
        uint32 freeIndex = constant::invalid_index::kUint32;
        for ( uint32 index = 0; index < _listEntry.size(); ++index )
        {
            Entry& entry = _listEntry[index];
            if ( entry._listHolder.empty() )
            {
                freeIndex = index;
                continue;
            }
            if ( isSameDesc( entry._desc, desc ) && VectorUtil::contains( entry._listHolder, pHolder ) == false )
            {
                entry._listHolder.push_back( pHolder );
                return index;
            }
        }
        Entry created;
        created._desc = desc;
        if ( created._texture.ensure( device, desc, pDebugName ) == false )
            return constant::invalid_index::kUint32;
        created._listHolder.push_back( pHolder );
        if ( freeIndex == constant::invalid_index::kUint32 )
        {
            freeIndex = static_cast<uint32>( _listEntry.size() );
            _listEntry.push_back( std::move( created ) );
        }
        else
        {
            _listEntry[freeIndex] = std::move( created );
        }
        return freeIndex;
    }

    void TransientTextureLeases::release( IRHIDevice* pDevice, uint32 index, const void* pHolder )
    {
        if ( index >= _listEntry.size() )
            return;
        Entry& entry = _listEntry[index];
        for ( size_t holderIndex = 0; holderIndex < entry._listHolder.size(); ++holderIndex )
        {
            if ( entry._listHolder[holderIndex] != pHolder )
                continue;
            entry._listHolder.erase( entry._listHolder.begin() + holderIndex );
            if ( entry._listHolder.empty() )
                entry._texture.release( pDevice );
            return;
        }
        SW_ASSERT( false && "transient texture returned by a holder that does not lease it" );
    }

    uint32 TransientTextureLeases::getLiveCount() const
    {
        uint32 count{ 0 };
        for ( const Entry& entry : _listEntry )
        {
            count += entry._listHolder.empty() ? 0u : 1u;
        }
        return count;
    }

    bool TransientTextureLeases::isSameDesc( const RHITextureDesc& left, const RHITextureDesc& right )
    {
        const bool bSameShape = left._width == right._width && left._height == right._height && left._arraySize == right._arraySize && left._mipLevels == right._mipLevels &&
                                left._format == right._format && left._dimension == right._dimension;
        const bool bSameUsage = left._bIsRenderTarget == right._bIsRenderTarget && left._bIsDepthStencil == right._bIsDepthStencil && left._bIsShaderResource == right._bIsShaderResource &&
                                left._bIsUnorderedAccess == right._bIsUnorderedAccess && left._bIsTransient == right._bIsTransient;
        const bool bSameClear = left._clearDepth == right._clearDepth && left._clearStencil == right._clearStencil && Memory::compare( &left._clearColor, &right._clearColor, sizeof( float4 ) ) == 0;
        return bSameShape && bSameUsage && bSameClear;
    }
} // namespace sw
