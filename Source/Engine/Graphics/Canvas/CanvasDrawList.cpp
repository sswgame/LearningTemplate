#include "pch.h"

#include "Engine/Graphics/Canvas/CanvasDrawList.h"

#include "Core/Memory/Memory.h"

namespace sw
{
    namespace
    {
        struct CanvasDrawListInternal
        {
            static bool isSameScissor( const CanvasBatch& lhs, const CanvasBatch& rhs )
            {
                return lhs._bScissor == rhs._bScissor && ( lhs._bScissor == SW_FALSE || Memory::compare( &lhs._scissor, &rhs._scissor, sizeof( RHIScissorRect ) ) == 0 );
            }

            /**
             * @brief @p source 의 텍스처를 @p target 에 넣을 자리를 정합니다. 다 들어가면 true 이고 @p outArrSlot 이 원래 번호 → 새 번호입니다.
             * @details 들어가지 않으면 @p target 을 바꾸지 않습니다.
             */
            static bool mergeTextures( CanvasBatch& target, const CanvasBatch& source, uint32 ( &outArrSlot )[shaderslot::kMaterialTextureCount] )
            {
                CanvasBatch merged = target;
                for ( uint32 index = 0; index < source._textureCount; ++index )
                {
                    uint32 slot = merged._textureCount;
                    for ( uint32 existing = 0; existing < merged._textureCount; ++existing )
                    {
                        if ( merged._arrTexture[existing].isEqual( source._arrTexture[index] ) )
                        {
                            slot = existing;
                            break;
                        }
                    }
                    if ( slot == merged._textureCount )
                    {
                        if ( merged._textureCount >= shaderslot::kMaterialTextureCount )
                            return false;
                        merged._arrTexture[slot] = source._arrTexture[index];
                        ++merged._textureCount;
                    }
                    outArrSlot[index] = slot;
                }
                for ( uint32 index = 0; index < merged._textureCount; ++index )
                    target._arrTexture[index] = merged._arrTexture[index];
                target._textureCount = merged._textureCount;
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool CanvasTextureRef::isEqual( const CanvasTextureRef& other ) const
    {
        return _texture == other._texture && _texturePath == other._texturePath && _atlasPage == other._atlasPage;
    }

    void CanvasDrawList::clear()
    {
        _listQuad.clear();
        _listBatch.clear();
        _targetSize = float2{};
    }

    void CanvasDrawList::appendDrawList( const CanvasDrawList& source )
    {
        for ( const CanvasBatch& sourceBatch : source._listBatch )
        {
            if ( sourceBatch._quadCount == 0 )
                continue;
            uint32 arrSlot[shaderslot::kMaterialTextureCount]{};
            bool   bMerged = false;
            if ( _listBatch.empty() == false && CanvasDrawListInternal::isSameScissor( _listBatch.back(), sourceBatch ) )
                bMerged = CanvasDrawListInternal::mergeTextures( _listBatch.back(), sourceBatch, arrSlot );
            if ( bMerged == false )
            {
                CanvasBatch& batch = _listBatch.emplace_back( sourceBatch );
                batch._firstQuad   = static_cast<uint32>( _listQuad.size() );
                batch._quadCount   = 0;
                for ( uint32 index = 0; index < shaderslot::kMaterialTextureCount; ++index )
                    arrSlot[index] = index;
            }
            for ( uint32 quadIndex = sourceBatch._firstQuad; quadIndex < sourceBatch._firstQuad + sourceBatch._quadCount; ++quadIndex )
            {
                CanvasQuad quad = source._listQuad[quadIndex];
                if ( quad._textureSlot != invalid_index::kUint32 )
                    quad._textureSlot = arrSlot[quad._textureSlot];
                _listQuad.push_back( quad );
            }
            _listBatch.back()._quadCount += sourceBatch._quadCount;
        }
    }

    bool CanvasDrawList::isSameContent( const CanvasDrawList& other ) const
    {
        if ( _listQuad.size() != other._listQuad.size() || _listBatch.size() != other._listBatch.size() )
            return false;
        if ( _listQuad.empty() == false && Memory::compare( _listQuad.data(), other._listQuad.data(), _listQuad.size() * sizeof( CanvasQuad ) ) != 0 )
            return false;
        for ( size_t index = 0; index < _listBatch.size(); ++index )
        {
            const CanvasBatch& lhs = _listBatch[index];
            const CanvasBatch& rhs = other._listBatch[index];
            if ( lhs._firstQuad != rhs._firstQuad || lhs._quadCount != rhs._quadCount || lhs._textureCount != rhs._textureCount ||
                 CanvasDrawListInternal::isSameScissor( lhs, rhs ) == false )
                return false;
            for ( uint32 slot = 0; slot < lhs._textureCount; ++slot )
            {
                if ( lhs._arrTexture[slot].isEqual( rhs._arrTexture[slot] ) == false )
                    return false;
            }
        }
        return true;
    }

    void CanvasFrameData::clear()
    {
        _mainOutput.clear();
        _listTarget.clear();
        _listAtlasUpload.clear();
        _contentRevision = 0;
        _colorVisionMode = 0;
    }
} // namespace sw
