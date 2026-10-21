#include "pch.h"

#include "Engine/Renderer/Frame/Resource/ShaderParameterBinder.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/Support/BindlessIndexValidator.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingLayout.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Renderer/Frame/Resource/FrameResourceRegistry.h"
#include "Engine/Renderer/Frame/Resource/PassConstantValues.h"

namespace sw
{
    SW_LOG_CALLER( "ShaderParameterBinder" );

    // ------------------------------------------------------------------------------
    // PassConstantValues
    // ------------------------------------------------------------------------------
    void PassConstantValues::setBytes( hashed_string name, const void* pData, uint32 byteSize )
    {
        const bool bInvalidValueBytes = pData == nullptr || byteSize == 0 || byteSize > kMaxValueBytes;
        if ( bInvalidValueBytes )
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
    // ShaderParameterBinder
    // ------------------------------------------------------------------------------
    namespace
    {
        /**
         * @brief `bindGraphics` 의 단계들입니다. 드로우마다 불리므로 이름 intern · 힙 할당을 하지 않습니다(레이아웃이 미리 계산해 둔 키만 읽는다).
         * @details bindless 인덱스 검사(`BindlessIndexValidator`)는 배포 구성에서 사라진다 — `checkIndex` 하나가 그 갈래를 가진다.
         */
        struct ShaderParameterBinderInternal
        {
            /** @brief 이번 바인딩에서 bindless 인덱스를 검사하는지입니다. 배포 구성은 늘 false 다. */
            static bool isIndexCheckOn( const BindlessIndexCheckSite* pIndexCheck )
            {
#if !defined( SW_SHIPPING )
                return pIndexCheck != nullptr && BindlessIndexValidator::isEnabled();
#else
                (void)pIndexCheck;
                return false;
#endif
            }

            /** @brief 넘기는 bindless 인덱스가 살아 있는 슬롯인지 봅니다. 죽은 번호는 DX12 · Vulkan 에서 디바이스 손실로 끝나므로 검사가 로그로 남긴다. */
            static void checkIndex( const BindlessIndexCheckSite* pIndexCheck, bool bCheck, RHIBindlessIndexSpace space, RHIDescriptorIndex index,
                                    const hashed_string& name, uint32 elementIndex = 0 )
            {
#if !defined( SW_SHIPPING )
                if ( bCheck == false )
                    return;
                (void)BindlessIndexValidator::validate( *pIndexCheck, space, index, name, elementIndex ); // 죽은 번호는 검사가 로그로 남긴다
#else
                (void)pIndexCheck;
                (void)bCheck;
                (void)space;
                (void)index;
                (void)name;
                (void)elementIndex;
#endif
            }

            /**
             * @brief 1) 엔진 CB 를 채워 올립니다.
             * @details 크기 · 멤버 키 · 자동 인덱스 키는 모두 레이아웃 빌드 때 미리 계산해 뒀다(ShaderBindingLayout::computeBindPlan). 명시 값이 없고
             *          `g_<Name>Index` 패턴이면 레지스트리에서 텍스처 · 버퍼 bindless 인덱스를 채운다. 해결에 실패해도 그 멤버는 반드시 INVALID(0xFFFFFFFF)로
             *          채운다 — 0 으로 두면 셰이더가 힙 0번을 잘못 읽는다(swLoadInstanceWorld / swSampleIndex 는 kInvalidIndex 비교로 폴백).
             */
            static void uploadEngineConstantBuffer( IRHICommandList& cmd, const ShaderBindingLayout& layout, const FrameResourceRegistry& registry,
                                                    const PassConstantValues& values, const EngineConstantBufferSlot& engineCb,
                                                    const BindlessIndexCheckSite* pIndexCheck, bool bCheckIndex )
            {
                // 드로우마다 힙에서 새로 잡지 않는다. 병렬 패스 기록이 여러 스레드에서 이 함수를
                // 동시에 부르므로 스레드마다 자기 버퍼를 갖는다.
                thread_local vector<uint8> s_cbBytes;
                const uint32               alignedSize = MathUtil::align( layout.getEngineCbSize(), 16u );
                s_cbBytes.assign( alignedSize, 0 );

                for ( const ShaderEngineCbMember& member : layout.getEngineCbMembers() )
                {
                    uint32       valueSize = 0;
                    const uint8* pValue    = values.find( member._valueKey, valueSize );
                    uint32       autoIndex = kInvalidDescriptorIndex;
                    if ( ( pValue == nullptr || valueSize == 0 ) && member._autoIndexKey.empty() == false )
                    {
                        RHIBindlessIndexSpace    autoSpace = RHIBindlessIndexSpace::Texture;
                        const RegisteredTexture* pTex      = registry.findTexture( member._autoIndexKey );
                        if ( pTex != nullptr )
                        {
                            autoIndex = pTex->_srv;
                        }
                        else
                        {
                            const RegisteredBuffer* pBuffer = registry.findBuffer( member._autoIndexKey );
                            if ( pBuffer != nullptr )
                                autoIndex = pBuffer->_index;
                            autoSpace = RHIBindlessIndexSpace::Buffer;
                        }
                        checkIndex( pIndexCheck, bCheckIndex, autoSpace, autoIndex, member._valueKey );
                        pValue    = reinterpret_cast<const uint8*>( &autoIndex );
                        valueSize = sizeof( uint32 );
                    }

                    if ( pValue == nullptr || valueSize == 0 )
                        continue;
                    const uint32 writeSize = MathUtil::min( valueSize, member._size );
                    if ( member._offset + writeSize <= s_cbBytes.size() )
                        Memory::copy( s_cbBytes.data() + member._offset, pValue, writeSize );
                }

                cmd.updateConstantBuffer( engineCb._buffer, s_cbBytes.data(), static_cast<uint32>( s_cbBytes.size() ) );
            }

            /**
             * @brief 2) 예약 CB 를 **기준 슬롯 번호로** 겁니다(b0 = PassCB, b1 = MaterialCB — bindingslots.hlsli).
             * @details 리플렉션이 주는 `_registerIndex` 는 백엔드마다 뜻이 다를 수 있다(세트 · 시프트 규약). 그대로 넘기면 한 CB 가 다른 CB 자리를
             *          덮어쓰거나 어느 슬롯에도 안 걸리는데, 같은 UNIFORM_BUFFER 라 검증 에러도 안 난다. 텍스처 · 구조버퍼는 3) 의 표가 맡는다.
             */
            static void bindReservedConstantBuffers( IRHICommandList& cmd, const ShaderBindingLayout& layout, const EngineConstantBufferSlot& engineCb,
                                                     RHIDescriptorIndex materialCb, const BindlessIndexCheckSite* pIndexCheck, bool bCheckIndex )
            {
                // 이 함수는 드로우마다 불린다. 리터럴이라도 hashed_string 을 여기서 만들면 드로우마다
                // 전역 레지스트리 intern(FNV + 샤드 뮤텍스)이 붙는다. 함수 지역 static 으로 한 번만 만든다.
                static const hashed_string s_materialCbName{ shaderslot::cbname::kMaterial };
                for ( const ShaderBindingSlot& slot : layout.getSlots() )
                {
                    if ( slot._kind != ShaderBindingKind::ConstantBuffer )
                        continue;
                    const bool               bMaterialCb = slot._name == s_materialCbName;
                    const RHIDescriptorIndex cbIndex     = bMaterialCb ? materialCb : engineCb._index;
                    checkIndex( pIndexCheck, bCheckIndex, RHIBindlessIndexSpace::Buffer, cbIndex, slot._name );
                    if ( cbIndex != kInvalidDescriptorIndex )
                        cmd.bindConstantBuffer( cbIndex, bMaterialCb ? shaderslot::kMaterialConstantBuffer : shaderslot::kPassConstantBuffer );
                }
            }

            /**
             * @brief 머티리얼 텍스처 인덱스를 검사하고, 에뮬 백엔드(DX11 · GL)면 t16..t31 에 서수대로 겁니다.
             * @details 네이티브 bindless 백엔드는 인덱스가 이미 MaterialCB 에 있어 걸 필요가 없지만, 같은 번호가 머티리얼 데이터에 들어가 있어 검사는 네 백엔드 모두 한다.
             */
            static void bindMaterialTextures( IRHICommandList& cmd, bool bNativeBindless, const RHIDescriptorIndex* pMaterialTexSrv,
                                              const BindlessIndexCheckSite* pIndexCheck, bool bCheckIndex )
            {
                if ( pMaterialTexSrv == nullptr )
                    return;
                static const hashed_string s_materialTextureName{ "MaterialTexture" };
                for ( uint32 texIndex = 0; texIndex < shaderslot::kMaterialTextureCount; ++texIndex )
                {
                    checkIndex( pIndexCheck, bCheckIndex, RHIBindlessIndexSpace::Texture, pMaterialTexSrv[texIndex], s_materialTextureName, texIndex );
                }
                if ( bNativeBindless )
                    return;
                for ( uint32 texIndex = 0; texIndex < shaderslot::kMaterialTextureCount; ++texIndex )
                {
                    if ( pMaterialTexSrv[texIndex] != kInvalidDescriptorIndex )
                        cmd.bindShaderResource( pMaterialTexSrv[texIndex], shaderslot::kMaterialTexture0 + texIndex );
                }
            }

            /**
             * @brief 3) 텍스처 · 구조버퍼를 레이아웃이 미리 계산한 조회 키로 겁니다.
             * @details 구조버퍼(인스턴스 t4, 머티리얼 데이터 t5 …)는 리플렉션이 준 슬롯에 네 백엔드가 각자의 방식으로 건다. DX12 디스크립터 테이블,
             *          Vulkan 슬롯 세트, DX11 SRV 슬롯, GL SSBO. 드로우별로 바뀌는 데이터는 이 버퍼 안의 원소라 바인딩 자체는 패스 · 배치마다 한 번이다.
             */
            static void bindRegisteredResources( IRHICommandList& cmd, const ShaderBindingLayout& layout, const FrameResourceRegistry& registry, bool bNativeBindless,
                                                 const BindlessIndexCheckSite* pIndexCheck, bool bCheckIndex )
            {
                for ( const ShaderResourceBindOp& bind : layout.getResourceBindOps() )
                {
                    if ( bind._kind == ShaderBindingKind::Texture )
                    {
                        if ( bNativeBindless )
                            continue; // 인덱스는 이미 CB 에 기록됨
                        const RegisteredTexture* pTex = registry.findTexture( bind._lookupKey );
                        if ( pTex != nullptr && pTex->_srv != kInvalidDescriptorIndex )
                        {
                            checkIndex( pIndexCheck, bCheckIndex, RHIBindlessIndexSpace::Texture, pTex->_srv, bind._lookupKey );
                            cmd.bindShaderResource( pTex->_srv, bind._registerIndex );
                        }
                        continue;
                    }
                    const RegisteredBuffer* pBuffer = registry.findBuffer( bind._lookupKey );
                    if ( pBuffer != nullptr && pBuffer->_index != kInvalidDescriptorIndex )
                    {
                        checkIndex( pIndexCheck, bCheckIndex, RHIBindlessIndexSpace::Buffer, pBuffer->_index, bind._lookupKey );
                        cmd.bindStructuredBuffer( pBuffer->_index, bind._registerIndex );
                    }
                }
            }
        };
    } // namespace

    void ShaderParameterBinder::bindGraphics( IRHICommandList&                cmd,
                                              const ShaderBindingLayout&      layout,
                                              const FrameResourceRegistry&    registry,
                                              const PassConstantValues&       values,
                                              const EngineConstantBufferSlot& engineCb,
                                              RHIDescriptorIndex              materialCb,
                                              bool                            bNativeBindless,
                                              const RHIDescriptorIndex*       pMaterialTexSrv,
                                              bool                            bEngineCbUpToDate,
                                              const BindlessIndexCheckSite*   pIndexCheck )
    {
        using Internal = ShaderParameterBinderInternal;
        if ( layout.isEmpty() )
            return;
        const bool bCheckIndex = Internal::isIndexCheckOn( pIndexCheck );

        // 1) 값 · 레지스트리가 그대로면 버퍼 내용도 그대로다. 다시 만들지도, 올리지도 않는다.
        //    배치마다 바뀌는 값은 루트 상수로 나가므로(binding.hlsli) 한 패스의 두 번째 드로우부터는 늘 여기로 온다.
        const bool bUploadEngineCb = layout.getEngineCbSize() > 0 && engineCb._buffer != 0 && bEngineCbUpToDate == false;
        if ( bUploadEngineCb )
            Internal::uploadEngineConstantBuffer( cmd, layout, registry, values, engineCb, pIndexCheck, bCheckIndex );
        Internal::bindReservedConstantBuffers( cmd, layout, engineCb, materialCb, pIndexCheck, bCheckIndex );
        Internal::bindMaterialTextures( cmd, bNativeBindless, pMaterialTexSrv, pIndexCheck, bCheckIndex );

        // 3) 등록 내용이 그대로면 이미 걸린 것과 같은 자원을 다시 거는 셈이라 건너뛴다(같은 패스 안의 두 번째
        //    드로우부터가 대부분 그렇다). 백엔드도 값이 같으면 더럽힘 표시를 안 하지만, 여기서 끊으면
        //    레지스트리 조회 자체가 사라진다. PSO 가 바뀌면 백엔드가 슬롯 상태를 비우므로 그때는 다시 건다.
        if ( bEngineCbUpToDate )
            return;
        Internal::bindRegisteredResources( cmd, layout, registry, bNativeBindless, pIndexCheck, bCheckIndex );
    }
} // namespace sw
