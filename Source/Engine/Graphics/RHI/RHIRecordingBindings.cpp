#include "pch.h"

#include "Engine/Graphics/RHI/RHIRecordingBindings.h"

#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    RHIRootConstantWrite RHIRootConstants::write( uint32 count, const void* pData, uint32 firstDword )
    {
        if ( count == 0 || pData == nullptr )
            return RHIRootConstantWrite::Empty;
        if ( firstDword >= shaderslot::kRootConstantDwords || count > shaderslot::kRootConstantDwords - firstDword )
            return RHIRootConstantWrite::Overflow;
        Memory::copy( _arrDword + firstDword, pData, static_cast<size_t>( count ) * sizeof( uint32 ) );
        const bool bWasDirty = isDirty();
        _dirtyBegin          = bWasDirty ? MathUtil::min( _dirtyBegin, firstDword ) : firstDword;
        _dirtyEnd            = bWasDirty ? MathUtil::max( _dirtyEnd, firstDword + count ) : firstDword + count;
        return RHIRootConstantWrite::Written;
    }

    RHIShaderSlots::RHIShaderSlots()
    {
        for ( RHIDescriptorIndex( &arrSlot )[kShaderResourceCount] : _arrIndex )
        {
            for ( RHIDescriptorIndex& index : arrSlot )
            {
                index = kInvalidDescriptorIndex;
            }
        }
    }

    void RHIShaderSlots::bind( RHIShaderSlotKind kind, uint32 slot, RHIDescriptorIndex index )
    {
        const uint32        kindIndex = static_cast<uint32>( kind );
        RHIDescriptorIndex& current   = _arrIndex[kindIndex][slot];
        if ( current == index )
            return;
        const uint64 bit = uint64{ 1 } << slot;
        current          = index;
        _arrDirtyMask[kindIndex] |= bit;
        if ( index != kInvalidDescriptorIndex )
            _arrBoundMask[kindIndex] |= bit;
        else
            _arrBoundMask[kindIndex] &= ~bit;
    }

    void RHIShaderSlots::clear()
    {
        for ( uint32 kind = 0; kind < kKindCount; ++kind )
        {
            uint64 bound = _arrBoundMask[kind];
            _arrDirtyMask[kind] |= bound;
            _arrBoundMask[kind] = 0;
            for ( uint32 slot = 0; bound != 0; ++slot, bound >>= 1 )
            {
                if ( ( bound & 1u ) != 0 )
                    _arrIndex[kind][slot] = kInvalidDescriptorIndex;
            }
        }
    }

    void RHIShaderSlots::markAllDirty()
    {
        for ( uint32 kind = 0; kind < kKindCount; ++kind )
        {
            _arrDirtyMask[kind] |= _arrBoundMask[kind];
        }
    }

    void RHIShaderSlots::markDirty( RHIShaderSlotKind kind, uint32 slot )
    {
        if ( isBound( kind, slot ) )
            _arrDirtyMask[static_cast<uint32>( kind )] |= uint64{ 1 } << slot;
    }

    void RHIRecordingBindings::setGraphicsPipeline( RHIPipelineStateHandle pso )
    {
        if ( pso == _graphicsPSO )
            return;
        _graphicsPSO = pso;
        getSlots( RHIBindPoint::Graphics ).clear();
        _geometry.clearVertexBuffers();
    }

    void RHIRecordingBindings::setComputePipeline( RHIPipelineStateHandle pso )
    {
        if ( pso == _computePSO )
            return;
        _computePSO = pso;
        getSlots( RHIBindPoint::Compute ).clear();
    }

    void RHIRecordingBindings::resetGraphics()
    {
        _graphicsPSO = 0;
        _geometry    = RHIGeometryBindings{};
        getSlots( RHIBindPoint::Graphics ).clear();
    }
} // namespace sw
