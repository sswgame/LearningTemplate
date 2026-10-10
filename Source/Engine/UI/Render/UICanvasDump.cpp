#include "pch.h"

#include "Engine/UI/Render/UICanvasDump.h"

#include "Core/Common/Defines.h"
#include "Core/Container/formatString.h"
#include "Core/String/fixed_string.h"

#include "Engine/Graphics/Canvas/CanvasDrawList.h"

namespace sw
{
    namespace
    {
        struct UICanvasDumpInternal
        {
            static const utf8* getKindName( uint32 kind )
            {
                switch ( static_cast<CanvasQuadKind>( kind ) )
                {
                    case CanvasQuadKind::Rect:
                        return "rect";
                    case CanvasQuadKind::Image:
                        return "image";
                    case CanvasQuadKind::Glyph:
                        return "glyph";
                    case CanvasQuadKind::Shadow:
                        return "shadow";
                }
                return "unknown";
            }

            /** @brief 소수 둘째 자리 — 반올림이 0 이면 부호를 떼어 `-0.00` 이 나오지 않게 합니다(부동소수 누적 부호가 덤프에 새지 않게). */
            static void appendNumber( float32 value, string& inoutText )
            {
                fixed_string<constant::kMaxBuffer32> number{};
                formatstring( number.data(), number.capacity(), " %.2f", value );
                if ( string_view( number.c_str() ) == " -0.00" )
                    inoutText += " 0.00";
                else
                    inoutText += number.c_str();
            }

            static void appendTextureName( const CanvasTextureRef& texture, string& inoutText )
            {
                if ( texture._texture != nullptr )
                {
                    inoutText += "object";
                    return;
                }
                if ( texture._texturePath.empty() == false )
                {
                    inoutText += texture._texturePath.c_str();
                    return;
                }
                inoutText += "atlas" + to_string( static_cast<uint32>( texture._atlasPage ) );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    string UICanvasDump::makeDump( const CanvasDrawList& list )
    {
        using Internal = UICanvasDumpInternal;
        string text    = "target";
        Internal::appendNumber( list._targetSize._x, text );
        Internal::appendNumber( list._targetSize._y, text );
        text += "\n";
        for ( const CanvasBatch& batch : list._listBatch )
        {
            text += "batch " + to_string( batch._quadCount ) + " scissor";
            if ( batch._bScissor == SW_TRUE )
                text += " " + to_string( batch._scissor._x ) + " " + to_string( batch._scissor._y ) + " " + to_string( batch._scissor._width ) + " " +
                        to_string( batch._scissor._height );
            else
                text += " none";
            text += " tex";
            for ( uint32 index = 0; index < batch._textureCount; ++index )
            {
                text += " ";
                Internal::appendTextureName( batch._arrTexture[index], text );
            }
            text += "\n";
            for ( uint32 quadIndex = batch._firstQuad; quadIndex < batch._firstQuad + batch._quadCount && quadIndex < list._listQuad.size(); ++quadIndex )
            {
                const CanvasQuad& quad = list._listQuad[quadIndex];
                text += "  ";
                text += Internal::getKindName( quad._kind );
                Internal::appendNumber( quad._rect._x, text );
                Internal::appendNumber( quad._rect._y, text );
                Internal::appendNumber( quad._rect._z, text );
                Internal::appendNumber( quad._rect._w, text );
                Internal::appendNumber( quad._color._x, text );
                Internal::appendNumber( quad._color._y, text );
                Internal::appendNumber( quad._color._z, text );
                Internal::appendNumber( quad._color._w, text );
                if ( quad._textureSlot != invalid_index::kUint32 && quad._textureSlot < batch._textureCount )
                {
                    text += " tex ";
                    Internal::appendTextureName( batch._arrTexture[quad._textureSlot], text );
                }
                text += "\n";
            }
        }
        return text;
    }
} // namespace sw
