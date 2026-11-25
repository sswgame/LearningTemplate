#include "pch.h"

#include "Engine/Graphics/Shader/Binding/ComputeShaderParameters.h"

#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingLayout.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

namespace sw
{
    namespace
    {
        /** @brief 매개변수 종류가 받는 리플렉션 종류인지입니다(UAV 는 버퍼 · 텍스처 둘 다). */
        bool isAcceptedBindingKind( ComputeShaderParameterKind parameterKind, ShaderBindingKind bindingKind )
        {
            switch ( parameterKind )
            {
                case ComputeShaderParameterKind::ConstantBuffer:
                    return bindingKind == ShaderBindingKind::ConstantBuffer;
                case ComputeShaderParameterKind::Buffer:
                    return bindingKind == ShaderBindingKind::StructuredBuffer;
                case ComputeShaderParameterKind::UAV:
                    return bindingKind == ShaderBindingKind::RwStructuredBuffer || bindingKind == ShaderBindingKind::RwTexture;
            }
            return false;
        }

        /**
         * @brief 표가 이름으로 걸어야 하는 셰이더 자원인지입니다.
         * @details 표 밖인 것: 텍스처 · 샘플러(컴퓨트는 bindless 번호나 RW 칸으로 읽는다), space 0 밖과 크기 없는 배열(bindless 표 — 번호로 고른다),
         *          루트 상수 블록(`SwRootConstants` — DX11 · GL 은 상수 버퍼로 흉내 낸다, `setComputeRootConstants` 가 채운다).
         */
        bool isNamedParameterSlot( const ShaderBindingSlot& slot )
        {
            const bool bParameterKind = slot._kind == ShaderBindingKind::ConstantBuffer || slot._kind == ShaderBindingKind::StructuredBuffer ||
                                        slot._kind == ShaderBindingKind::RwStructuredBuffer || slot._kind == ShaderBindingKind::RwTexture;
            return bParameterKind && slot._space == 0 && slot._arrayCount != 0 && slot._name.view() != string_view( shaderslot::cbname::kRootConstants );
        }
    } // namespace

    ComputeShaderBindings::ComputeShaderBindings()
        : _pInfo{ nullptr }
        , _count{ 0 }
        , _arrSlot{}
    {
        for ( uint8& slot : _arrSlot )
            slot = kAbsentSlot;
    }

    bool ComputeShaderBindings::resolve( const ShaderBindingLayout& layout, const ComputeShaderParameterTable& table, string_view label, ComputeShaderBindings& outBindings )
    {
        outBindings = ComputeShaderBindings{};
        if ( table._count > kMaxParameters )
        {
            SW_LOG_ERROR_ID( "ComputeShaderBindings.TableTooLarge", "Compute shader '%#' has %# parameters (max %#) - the PSO is not used", label, table._count, kMaxParameters );
            return false;
        }
        bool bResolved{ true };
        for ( const ShaderBindingSlot& slot : layout.getSlots() )
        {
            if ( isNamedParameterSlot( slot ) == false )
                continue;
            uint32 found = table._count;
            for ( uint32 index = 0; index < table._count; ++index )
            {
                if ( slot._name.view() == string_view( table._pInfo[index]._pName ) )
                {
                    found = index;
                    break;
                }
            }
            if ( found == table._count )
            {
                SW_LOG_ERROR_ID( "ComputeShaderBindings.UndeclaredParameter", "Compute shader '%#' declares '%#' (register %#) but its C++ parameter table does not - the PSO is not used",
                                 label, slot._name.c_str(), slot._registerIndex );
                bResolved = false;
                continue;
            }
            if ( isAcceptedBindingKind( table._pInfo[found]._kind, slot._kind ) == false || slot._registerIndex >= kAbsentSlot )
            {
                SW_LOG_ERROR_ID( "ComputeShaderBindings.ParameterKindMismatch", "Compute shader '%#' declares '%#' as kind %# register %# - the C++ parameter table disagrees, the PSO is not used",
                                 label, slot._name.c_str(), static_cast<uint32>( slot._kind ), slot._registerIndex );
                bResolved = false;
                continue;
            }
            outBindings._arrSlot[found] = static_cast<uint8>( slot._registerIndex );
        }
        if ( bResolved == false )
        {
            outBindings = ComputeShaderBindings{};
            return false;
        }
        outBindings._pInfo = table._pInfo;
        outBindings._count = table._count;
        return true;
    }

    bool ComputeShaderBindings::apply( IRHIPassCommandList& commands, RHIPipelineStateHandle pso, const ComputeShaderParameterInfo* pInfo, const uint32* pValues ) const
    {
        if ( _pInfo != pInfo || pso == 0 )
        {
            SW_LOG_ERROR_ID( "ComputeShaderBindings.PipelineTableMismatch", "A compute pipeline was applied with another shader's parameter table (or without a PSO) - the dispatch is skipped" );
            return false;
        }
        commands.setComputePipelineState( pso );
        for ( uint32 ordinal = 0; ordinal < _count; ++ordinal )
        {
            const uint8  slot  = _arrSlot[ordinal];
            const uint32 value = pValues[ordinal];
            if ( slot == kAbsentSlot || value == static_cast<uint32>( kInvalidDescriptorIndex ) )
                continue;
            switch ( _pInfo[ordinal]._kind )
            {
                case ComputeShaderParameterKind::ConstantBuffer:
                    commands.bindComputeConstantBuffer( RHIBufferSRVIndex{ value }, slot );
                    break;
                case ComputeShaderParameterKind::Buffer:
                    commands.bindComputeShaderResource( RHIBufferSRVIndex{ value }, slot );
                    break;
                case ComputeShaderParameterKind::UAV:
                    commands.bindComputeUAV( RHIUnorderedAccessIndex{ value }, slot );
                    break;
            }
        }
        return true;
    }
} // namespace sw
