#include "pch.h"

#include "Engine/Graphics/Renderer/Frame/ShaderBindingBinder.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Graphics/Renderer/Frame/FrameResourceRegistry.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingLayout.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

namespace sw
{
    SW_LOG_CALLER( "ShaderBindingBinder" );

    // ------------------------------------------------------------------------------
    // PassConstantValues
    // ------------------------------------------------------------------------------
    void PassConstantValues::setBytes( hashed_string name, const void* pData, uint32 byteSize )
    {
        if ( pData == nullptr || byteSize == 0 || byteSize > kMaxValueBytes )
            return;

        // **값이 실제로 달라질 때만** 버전을 올린다. 같은 값을 다시 넣는 호출이 흔한데(드로우마다 g_World 를
        // 같은 항등 행렬로 다시 넣는다), 그때마다 버전을 올리면 상수버퍼를 드로우마다 새로 만들게 된다.
        for ( Entry& entry : _listEntry )
        {
            if ( entry._name == name )
            {
                if ( entry._size == byteSize && Memory::compare( entry._data.data(), pData, byteSize ) == 0 )
                    return;
                entry._size = byteSize;
                Memory::copy( entry._data.data(), pData, byteSize );
                ++_version;
                return;
            }
        }

        ++_version;
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
                                            bool                            bNativeBindless,
                                            const RHIDescriptorIndex*       pMaterialTexSrv,
                                            bool                            bEngineCbUpToDate )
    {
        if ( layout.isEmpty() )
            return;

        IRHIResource* pResource = device.getResource();
        // 이 함수는 드로우마다 불린다. 리터럴이라도 hashed_string 을 여기서 만들면 드로우마다
        // 전역 레지스트리 intern(FNV + 샤드 뮤텍스)이 붙는다 — 함수 지역 static 으로 한 번만 만든다.
        static const hashed_string s_materialCbName{ shaderslot::cbname::kMaterial };

        // 1) 엔진 CB 채우기.
        //    크기·멤버 키·자동 인덱스 키는 전부 레이아웃 빌드 때 구워 뒀다(ShaderBindingLayout::
        //    buildBindPlan). 예전엔 드로우마다 슬롯/멤버를 훑어 크기를 다시 구하고, 멤버 이름으로
        //    hashed_string 을 만들고(전역 intern 테이블 조회), canonical 이름을 string 으로 새로
        //    할당했다 — 전부 PSO 마다 한 번이면 되는 일이라 드로우 경로에서 걷어냈다.
        // 값·레지스트리가 그대로면 버퍼 내용도 그대로다 — 다시 만들지도, 올리지도 않는다.
        // 배치마다 바뀌는 값은 루트 상수로 나가므로(binding.hlsli 1-0) 한 패스의 두 번째 드로우부터는 늘 여기로 온다.
        // 예전엔 드로우마다 멤버 표를 훑어 바이트를 채우고 512 바이트를 올렸다.
        const uint32 engineCbSize = layout.getEngineCbSize();
        if ( engineCbSize > 0 && engineCb._buffer != 0 && pResource != nullptr && bEngineCbUpToDate == false )
        {
            // 드로우마다 힙에서 새로 잡지 않는다. 병렬 패스 기록이 여러 스레드에서 이 함수를
            // 동시에 부르므로 스레드마다 자기 버퍼를 갖는다.
            thread_local vector<uint8> s_cbBytes;
            const uint32               alignedSize = MathUtil::align( engineCbSize, 16u );
            s_cbBytes.assign( alignedSize, 0 );

            for ( const ShaderEngineCbMember& member : layout.getEngineCbMembers() )
            {
                uint32       valueSize = 0;
                const uint8* pValue    = values.find( member._valueKey, valueSize );

                // 명시 값이 없고 `g_<Name>Index` 패턴이면 레지스트리에서 텍스처/버퍼 bindless 인덱스를 채운다.
                // 해결 실패해도 이 멤버는 반드시 INVALID(0xFFFFFFFF) 로 채운다 — 0 으로 두면 셰이더가 힙 0번을
                // 오독한다 (SwLoadInstanceWorld / SW_SampleIndex 는 SW_INVALID_INDEX 비교로 폴백).
                uint32 autoIndex = kInvalidDescriptorIndex;
                if ( ( pValue == nullptr || valueSize == 0 ) && member._autoIndexKey.empty() == false )
                {
                    const RegisteredTexture* pTex = registry.findTexture( member._autoIndexKey );
                    if ( pTex != nullptr )
                    {
                        autoIndex = pTex->_srv;
                    }
                    else
                    {
                        const RegisteredBuffer* pBuffer = registry.findBuffer( member._autoIndexKey );
                        if ( pBuffer != nullptr )
                            autoIndex = pBuffer->_index;
                    }
                    pValue    = reinterpret_cast<const uint8*>( &autoIndex );
                    valueSize = sizeof( uint32 );
                }

                if ( pValue == nullptr || valueSize == 0 )
                    continue;
                const uint32 writeSize = MathUtil::min( valueSize, member._size );
                if ( member._offset + writeSize <= s_cbBytes.size() )
                    Memory::copy( s_cbBytes.data() + member._offset, pValue, writeSize );
            }

            pResource->updateConstantBuffer( engineCb._buffer, s_cbBytes.data(), static_cast<uint32>( s_cbBytes.size() ) );
        }

        // 3) 슬롯별 바인딩
        for ( const ShaderBindingSlot& slot : layout.getSlots() )
        {
            switch ( slot._kind )
            {
                case ShaderBindingKind::ConstantBuffer:
                {
                    // **예약 CB 는 리플렉션 번호가 아니라 정본 슬롯 번호로 건다.** 리플렉션이 주는
                    // `_registerIndex` 는 백엔드마다 뜻이 다르다 — Vulkan 은 b0/b1 을 각각 다른
                    // 디스크립터 세트의 binding 0 으로 만들므로 PassCB 와 MaterialCB 가 **둘 다 0** 이고,
                    // GL 은 -fvk-b-shift 때문에 16/17 이 된다. 그대로 넘기면 Vulkan 은 머티리얼 CB 가
                    // 패스 CB 자리(set 0)를 덮어써 뷰/투영 행렬이 통째로 깨지고(화면이 비었다),
                    // GL 은 어느 슬롯에도 안 걸린다. 둘 다 검증 에러가 안 난다 — 같은 UNIFORM_BUFFER 라서.
                    // bindingslots.hlsli 가 정하는 b0=PassCB / b1=MaterialCB 를 그대로 쓴다.
                    if ( slot._name == s_materialCbName )
                    {
                        if ( materialCb != kInvalidDescriptorIndex )
                            cmd.bindConstantBuffer( materialCb, shaderslot::kMaterialConstantBuffer );
                    }
                    else if ( engineCb._index != kInvalidDescriptorIndex )
                    {
                        cmd.bindConstantBuffer( engineCb._index, shaderslot::kPassConstantBuffer );
                    }
                    break;
                }
                case ShaderBindingKind::Texture:
                case ShaderBindingKind::StructuredBuffer:
                    break; // 아래 getResourceBinds() 표에서 한 번에 처리한다
                case ShaderBindingKind::Sampler:
                case ShaderBindingKind::RwStructuredBuffer:
                case ShaderBindingKind::RwTexture:
                case ShaderBindingKind::Unknown:
                default:
                    break;
            }
        }

        // 머티리얼 텍스처 — 에뮬 백엔드(DX11/GL)만. 셰이더가 서수로 t5..t8 을 고르므로 그 순서대로 건다.
        // 네이티브 bindless 백엔드는 인덱스가 이미 MaterialCB 에 있어 아무것도 걸 필요가 없다.
        if ( bNativeBindless == false && pMaterialTexSrv != nullptr )
        {
            for ( uint32 texIndex = 0; texIndex < shaderslot::kMaterialTextureCount; ++texIndex )
            {
                if ( pMaterialTexSrv[texIndex] != kInvalidDescriptorIndex )
                    cmd.bindShaderResource( pMaterialTexSrv[texIndex], shaderslot::kMaterialTexture0 + texIndex );
            }
        }

        // 3) 텍스처·구조버퍼 — 조회 키는 레이아웃이 이미 canonical 로 구워 뒀다.
        //    등록 내용이 그대로면 이미 걸린 것과 같은 자원을 다시 거는 셈이라 건너뛴다(같은 패스 안의 두 번째
        //    드로우부터가 대부분 그렇다). 백엔드도 값이 같으면 더럽힘 표시를 안 하지만, 여기서 끊으면
        //    레지스트리 조회 자체가 사라진다. PSO 가 바뀌면 백엔드가 슬롯 상태를 비우므로 그때는 다시 건다.
        if ( bEngineCbUpToDate )
            return;

        for ( const ShaderResourceBind& bind : layout.getResourceBinds() )
        {
            if ( bind._kind == ShaderBindingKind::Texture )
            {
                if ( bNativeBindless )
                    continue; // 인덱스는 이미 CB 에 기록됨
                const RegisteredTexture* pTex = registry.findTexture( bind._lookupKey );
                if ( pTex != nullptr && pTex->_srv != kInvalidDescriptorIndex )
                    cmd.bindShaderResource( pTex->_srv, bind._registerIndex );
                continue;
            }

            // 구조버퍼(인스턴스 t4, 머티리얼 데이터 t9 …): 리플렉션이 준 슬롯에 네 백엔드가 각자의 방식으로 건다 —
            // DX12 루트 SRV, Vulkan 슬롯 세트, DX11 SRV 슬롯, GL SSBO. 드로우별로 바뀌는 데이터는 이 버퍼 안의 원소라
            // 바인딩 자체는 패스/배치마다 한 번이다.
            const RegisteredBuffer* pBuffer = registry.findBuffer( bind._lookupKey );
            if ( pBuffer != nullptr && pBuffer->_index != kInvalidDescriptorIndex )
                cmd.bindStructuredBuffer( pBuffer->_index, bind._registerIndex );
        }
    }
} // namespace sw
