#include "pch.h"

#include "Engine/Graphics/Renderer/ShaderBindingBinder.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/RHI/IRHICommandContext.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Graphics/Renderer/FrameResourceRegistry.h"
#include "Engine/Graphics/Shader/ShaderBindingLayout.h"
#include "Engine/Graphics/Shader/ShaderBindingSlots.h"

namespace sw
{
    SW_LOG_CALLER( "ShaderBindingBinder" );

    namespace
    {
        /** @brief `g_ShadowMap` / `g_ShadowMapIndex` / `ShadowMap` → `"ShadowMap"` (레지스트리 조회 키). */
        string canonicalResourceName( string_view identifier, bool bStripIndexSuffix )
        {
            string_view name = identifier;
            if ( name.size() > 2 && name[0] == 'g' && name[1] == '_' )
                name = name.substr( 2 );
            if ( bStripIndexSuffix && name.size() > 5 )
            {
                const string_view suffix = name.substr( name.size() - 5 );
                if ( suffix == "Index" )
                    name = name.substr( 0, name.size() - 5 );
            }
            return string( name );
        }
    } // namespace

    // ------------------------------------------------------------------------------
    // PassConstantValues
    // ------------------------------------------------------------------------------
    void PassConstantValues::setBytes( hashed_string name, const void* pData, uint32 byteSize )
    {
        if ( pData == nullptr || byteSize == 0 || byteSize > kMaxValueBytes )
            return;

        for ( Entry& entry : _listEntry )
        {
            if ( entry._name == name )
            {
                entry._size = byteSize;
                Memory::copy( entry._data.data(), pData, byteSize );
                return;
            }
        }

        Entry entry{};
        entry._name = name;
        entry._size = byteSize;
        Memory::copy( entry._data.data(), pData, byteSize );
        _listEntry.push_back( entry );
    }

    void PassConstantValues::setMatrix( hashed_string name, const float4x4& value )
    {
        setBytes( name, &value, sizeof( float4x4 ) );
    }

    void PassConstantValues::setFloat4( hashed_string name, const float4& value )
    {
        setBytes( name, &value, sizeof( float4 ) );
    }

    void PassConstantValues::setFloat( hashed_string name, float32 value )
    {
        setBytes( name, &value, sizeof( float32 ) );
    }

    void PassConstantValues::setUint( hashed_string name, uint32 value )
    {
        setBytes( name, &value, sizeof( uint32 ) );
    }

    const uint8* PassConstantValues::find( hashed_string name, uint32& outSize ) const
    {
        for ( const Entry& entry : _listEntry )
        {
            if ( entry._name == name )
            {
                outSize = entry._size;
                return entry._data.data();
            }
        }
        outSize = 0;
        return nullptr;
    }

    // ------------------------------------------------------------------------------
    // ShaderBindingBinder
    // ------------------------------------------------------------------------------
    void ShaderBindingBinder::bindGraphics( IRHIDevice& device, IRHICommandList& cmd,
                                            const ShaderBindingLayout&      layout,
                                            const FrameResourceRegistry&    registry,
                                            const PassConstantValues&       values,
                                            const EngineConstantBufferSlot& engineCb,
                                            RHIDescriptorIndex              materialCb,
                                            bool                            bNativeBindless )
    {
        if ( layout.isEmpty() )
            return;

        IRHIResource* pResource = device.getResource();

        // 1) 엔진 CB 크기 산정 (모든 non-Material CB 의 최대 크기)
        const hashed_string materialCbName{ shaderslot::cbname::kMaterial };
        uint32              engineCbSize = 0;
        for ( const ShaderBindingSlot& slot : layout.getSlots() )
        {
            if ( slot._kind != ShaderBindingKind::ConstantBuffer || slot._name == materialCbName )
                continue;
            uint32 slotEnd = slot._cbTotalSize;
            for ( const ShaderVariableInfo& member : slot._listCbMember )
                slotEnd = MathUtil::max( slotEnd, member._offset + member._size );
            engineCbSize = MathUtil::max( engineCbSize, slotEnd );
        }

        // 2) 엔진 CB 바이트 버퍼 구성 (리플렉션 멤버 이름 ← PassConstantValues)
        if ( engineCbSize > 0 && engineCb._buffer != 0 && pResource != nullptr )
        {
            vector<uint8> cbBytes;
            cbBytes.assign( MathUtil::align( engineCbSize, 16u ), 0 );

            for ( const ShaderBindingSlot& slot : layout.getSlots() )
            {
                if ( slot._kind != ShaderBindingKind::ConstantBuffer || slot._name == materialCbName )
                    continue;

                for ( const ShaderVariableInfo& member : slot._listCbMember )
                {
                    const hashed_string memberKey( static_cast<std::string_view>( member._name ) );

                    uint32       valueSize = 0;
                    const uint8* pValue    = values.find( memberKey, valueSize );

                    // 명시 값이 없고 `g_<Name>Index` 패턴이면 레지스트리에서 텍스처/버퍼 bindless 인덱스를 채운다.
                    // 해결 실패해도 이 멤버는 반드시 INVALID(0xFFFFFFFF) 로 채운다 — 0 으로 두면 셰이더가 힙 0번을
                    // 오독한다 (SwLoadInstanceWorld / SW_SampleIndex 는 SW_INVALID_INDEX 비교로 폴백).
                    uint32 autoIndex = kInvalidDescriptorIndex;
                    if ( ( pValue == nullptr || valueSize == 0 ) && member._size == sizeof( uint32 ) &&
                         member._name.find( "Index" ) != string::npos )
                    {
                        const string             baseName = canonicalResourceName( static_cast<std::string_view>( member._name ), true );
                        const RegisteredTexture* pTex     = registry.findTexture( hashed_string( baseName.c_str() ) );
                        if ( pTex != nullptr )
                        {
                            autoIndex = pTex->_srv;
                        }
                        else
                        {
                            const RegisteredBuffer* pBuffer = registry.findBuffer( hashed_string( baseName.c_str() ) );
                            if ( pBuffer != nullptr )
                                autoIndex = pBuffer->_index;
                        }
                        pValue    = reinterpret_cast<const uint8*>( &autoIndex );
                        valueSize = sizeof( uint32 );
                    }

                    if ( pValue == nullptr || valueSize == 0 )
                        continue;
                    const uint32 writeSize = MathUtil::min( valueSize, member._size );
                    if ( member._offset + writeSize <= cbBytes.size() )
                        Memory::copy( cbBytes.data() + member._offset, pValue, writeSize );
                }
            }

            pResource->updateConstantBuffer( engineCb._buffer, cbBytes.data(), static_cast<uint32>( cbBytes.size() ) );
        }

        // 3) 슬롯별 바인딩
        for ( const ShaderBindingSlot& slot : layout.getSlots() )
        {
            switch ( slot._kind )
            {
                case ShaderBindingKind::ConstantBuffer:
                {
                    if ( slot._name == materialCbName )
                    {
                        if ( materialCb != kInvalidDescriptorIndex )
                            cmd.bindConstantBuffer( materialCb, slot._registerIndex );
                    }
                    else if ( engineCb._index != kInvalidDescriptorIndex )
                    {
                        cmd.bindConstantBuffer( engineCb._index, slot._registerIndex );
                    }
                    break;
                }
                case ShaderBindingKind::Texture:
                {
                    if ( bNativeBindless )
                        break; // 인덱스는 이미 CB 에 기록됨
                    const string             key  = canonicalResourceName( slot._name.c_str(), false );
                    const RegisteredTexture* pTex = registry.findTexture( hashed_string( key.c_str() ) );
                    if ( pTex != nullptr && pTex->_srv != kInvalidDescriptorIndex )
                        cmd.bindShaderResource( pTex->_srv, slot._registerIndex );
                    break;
                }
                case ShaderBindingKind::StructuredBuffer:
                {
                    // 텍스처와 달리 구조버퍼는 "네이티브 bindless(텍스처 샘플링)" 백엔드라도 항상 명시 바인딩이
                    // 필요할 수 있다 (Vulkan: 텍스처는 네이티브지만 그래픽스 storage buffer 는 디스크립터셋
                    // 바인딩 필요). 스킵 여부는 각 백엔드 bindStructuredBuffer 가 자체 판단한다
                    // (DX12 는 _bHeapDirectlyIndexed 면 내부에서 no-op).
                    const string            key     = canonicalResourceName( slot._name.c_str(), false );
                    const RegisteredBuffer* pBuffer = registry.findBuffer( hashed_string( key.c_str() ) );
                    if ( pBuffer != nullptr && pBuffer->_index != kInvalidDescriptorIndex )
                        cmd.bindStructuredBuffer( pBuffer->_index, slot._registerIndex );
                    break;
                }
                case ShaderBindingKind::Sampler:
                case ShaderBindingKind::RwStructuredBuffer:
                case ShaderBindingKind::RwTexture:
                case ShaderBindingKind::Unknown:
                default:
                    break;
            }
        }
    }
} // namespace sw
