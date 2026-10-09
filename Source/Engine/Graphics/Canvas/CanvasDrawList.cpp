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
             * @details 들어가지 않으면 @p target 을 바꾸지 않습니다. 일괄을 복사하지 않는다 — 텍스처 참조(shared_ptr)의 원자 증감이 위젯 캐시마다 든다.
             */
            static bool mergeTextures( CanvasBatch& target, const CanvasBatch& source, uint32 ( &outArrSlot )[shaderslot::kMaterialTextureCount] )
            {
                const CanvasTextureRef* arrPending[shaderslot::kMaterialTextureCount]{};
                uint32                  mergedCount = target._textureCount;
                for ( uint32 index = 0; index < source._textureCount; ++index )
                {
                    const CanvasTextureRef& texture = source._arrTexture[index];
                    uint32                  slot    = mergedCount;
                    for ( uint32 existing = 0; existing < mergedCount; ++existing )
                    {
                        const CanvasTextureRef& candidate = existing < target._textureCount ? target._arrTexture[existing] : *arrPending[existing];
                        if ( candidate.isEqual( texture ) )
                        {
                            slot = existing;
                            break;
                        }
                    }
                    if ( slot == mergedCount )
                    {
                        if ( mergedCount >= shaderslot::kMaterialTextureCount )
                            return false;
                        arrPending[slot] = &texture;
                        ++mergedCount;
                    }
                    outArrSlot[index] = slot;
                }
                for ( uint32 index = target._textureCount; index < mergedCount; ++index )
                {
                    target._arrTexture[index] = *arrPending[index];
                }
                target._textureCount = static_cast<uint8>( mergedCount );
                return true;
            }

            /** @brief 텍스처 번호를 바꾸지 않는 자리 매김이면 true 입니다(사각형을 통째로 복사해도 된다). */
            static bool isIdentitySlotMap( const uint32 ( &arrSlot )[shaderslot::kMaterialTextureCount], uint32 textureCount )
            {
                for ( uint32 index = 0; index < textureCount; ++index )
                {
                    if ( arrSlot[index] != index )
                        return false;
                }
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
                {
                    arrSlot[index] = index;
                }
            }
            // 사각형은 통째로 복사하고, 텍스처 번호가 바뀐 일괄만 번호를 다시 매긴다(사각형마다 push_back 하지 않는다).
            const size_t firstNew = _listQuad.size();
            _listQuad.insert( _listQuad.end(), source._listQuad.begin() + sourceBatch._firstQuad,
                              source._listQuad.begin() + sourceBatch._firstQuad + sourceBatch._quadCount );
            if ( CanvasDrawListInternal::isIdentitySlotMap( arrSlot, sourceBatch._textureCount ) == false )
            {
                for ( size_t quadIndex = firstNew; quadIndex < _listQuad.size(); ++quadIndex )
                {
                    CanvasQuad& quad = _listQuad[quadIndex];
                    if ( quad._textureSlot != invalid_index::kUint32 )
                        quad._textureSlot = arrSlot[quad._textureSlot];
                }
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
