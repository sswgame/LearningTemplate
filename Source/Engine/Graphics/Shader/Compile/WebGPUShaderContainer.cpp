#include "pch.h"

#include "Engine/Graphics/Shader/Compile/WebGPUShaderContainer.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    namespace
    {
        struct WebGPUShaderContainerInternal
        {
            static void appendWord( vector<uint8>& ioBytes, uint32 value )
            {
                const size_t offset = ioBytes.size();
                ioBytes.resize( offset + sizeof( value ) );
                Memory::copy( ioBytes.data() + offset, &value, sizeof( value ) );
            }

            [[nodiscard]] static bool readWord( vector_reference<const uint8> bytes, size_t& ioOffset, uint32& outValue )
            {
                if ( ioOffset + sizeof( outValue ) > bytes.size() )
                    return false;
                Memory::copy( &outValue, bytes.data() + ioOffset, sizeof( outValue ) );
                ioOffset += sizeof( outValue );
                return true;
            }

            /** @brief 머리를 읽고 SPIR-V · WGSL 구간을 찾습니다. 크기가 꾸러미 밖이면 false 입니다. */
            [[nodiscard]] static bool locate( vector_reference<const uint8> bytes, vector_reference<const uint8>& outSpirv, vector_reference<const uint8>& outWgsl )
            {
                size_t offset  = 0;
                uint32 magic   = 0;
                uint32 version = 0;
                uint32 size    = 0;
                if ( readWord( bytes, offset, magic ) == false || magic != WebGPUShaderContainer::kMagic || readWord( bytes, offset, version ) == false ||
                     version != WebGPUShaderContainer::kVersion || readWord( bytes, offset, size ) == false || offset + size > bytes.size() )
                    return false;
                outSpirv = bytes.subspan( offset, size );
                offset += size;
                if ( readWord( bytes, offset, size ) == false || offset + size > bytes.size() )
                    return false;
                outWgsl = bytes.subspan( offset, size );
                return true;
            }

            /** @brief @p text 의 @p position 에서 공백을 건너뜁니다. */
            static size_t skipSpace( string_view text, size_t position )
            {
                while ( position < text.size() && ( text[position] == ' ' || text[position] == '\t' || text[position] == '\n' || text[position] == '\r' ) )
                    ++position;
                return position;
            }

            /** @brief `@<name>(N)` 의 N 을 읽습니다. 맞지 않으면 false 이고 @p ioPosition 은 그대로입니다. */
            [[nodiscard]] static bool readAttribute( string_view text, string_view name, size_t& ioPosition, uint32& outValue )
            {
                size_t position = skipSpace( text, ioPosition );
                if ( text.substr( position, name.size() ) != name )
                    return false;
                position = skipSpace( text, position + name.size() );
                if ( position >= text.size() || text[position] != '(' )
                    return false;
                position      = skipSpace( text, position + 1 );
                uint32 value  = 0;
                bool   bDigit = false;
                while ( position < text.size() && text[position] >= '0' && text[position] <= '9' )
                {
                    value  = value * 10u + static_cast<uint32>( text[position] - '0' );
                    bDigit = true;
                    ++position;
                }
                position = skipSpace( text, position );
                if ( bDigit == false || position >= text.size() || text[position] != ')' )
                    return false;
                outValue   = value;
                ioPosition = position + 1;
                return true;
            }
        };
    } // namespace

    void WebGPUShaderContainer::pack( vector_reference<const uint8> spirv, string_view wgsl, vector<uint8>& outBytes )
    {
        outBytes.clear();
        outBytes.reserve( sizeof( uint32 ) * 4 + spirv.size() + wgsl.size() );
        WebGPUShaderContainerInternal::appendWord( outBytes, kMagic );
        WebGPUShaderContainerInternal::appendWord( outBytes, kVersion );
        WebGPUShaderContainerInternal::appendWord( outBytes, static_cast<uint32>( spirv.size() ) );
        outBytes.insert( outBytes.end(), spirv.begin(), spirv.end() );
        WebGPUShaderContainerInternal::appendWord( outBytes, static_cast<uint32>( wgsl.size() ) );
        outBytes.insert( outBytes.end(), wgsl.begin(), wgsl.end() );
    }

    bool WebGPUShaderContainer::findSpirv( vector_reference<const uint8> bytes, vector_reference<const uint8>& outSpirv )
    {
        vector_reference<const uint8> wgsl;
        return WebGPUShaderContainerInternal::locate( bytes, outSpirv, wgsl );
    }

    bool WebGPUShaderContainer::findWgsl( vector_reference<const uint8> bytes, string_view& outWgsl )
    {
        vector_reference<const uint8> spirv;
        vector_reference<const uint8> wgsl;
        if ( WebGPUShaderContainerInternal::locate( bytes, spirv, wgsl ) == false )
            return false;
        outWgsl = string_view( reinterpret_cast<const utf8*>( wgsl.data() ), wgsl.size() );
        return true;
    }

    void WebGPUShaderContainer::collectBindings( string_view wgsl, vector<uint32>& outListBinding )
    {
        outListBinding.clear();
        constexpr string_view kGroup   = "@group";
        size_t                position = wgsl.find( kGroup );
        while ( position != string_view::npos )
        {
            size_t cursor  = position;
            uint32 group   = 0;
            uint32 binding = 0;
            // Tint 는 `@group(0) @binding(N)` 순서로 쓴다(기억) — 다른 순서는 바인딩으로 보지 않는다.
            if ( WebGPUShaderContainerInternal::readAttribute( wgsl, kGroup, cursor, group ) && group == 0 &&
                 WebGPUShaderContainerInternal::readAttribute( wgsl, "@binding", cursor, binding ) )
                outListBinding.push_back( binding );
            position = wgsl.find( kGroup, position + kGroup.size() );
        }
        VectorUtil::sortUnique( outListBinding );
    }
} // namespace sw
