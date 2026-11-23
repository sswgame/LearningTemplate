#include "pch.h"

#include "Engine/Renderer/Debug/RenderBufferInspector.h"

#include "Engine/Graphics/RHI/Diagnostics/RHIComputeBindingLog.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingLayout.h"
#include "Engine/Renderer/Frame/Resource/RenderPSOCache.h"
#include "Engine/Renderer/Graph/FrameBufferTable.h"

namespace sw
{
    namespace
    {
        struct RenderBufferInspectorInternal
        {
            /// 목록을 요청한 뒤 바인딩을 기록하는 프레임 수입니다 — 이 안에 디스패치한 셰이더의 배치가 실린다.
            static constexpr uint32 kLoggingFrames = 4;

            /** @brief 표의 버퍼 하나를 목록 항목으로 만듭니다. 그 버퍼를 건 컴퓨트 셰이더를 봤고 원소 크기가 같으면 멤버를 싣는다. */
            static RenderBufferEntry makeEntry( const FrameBufferTable::Entry& entry, const IRHIDevice& device, RenderPSOCache& psoCache )
            {
                RenderBufferEntry result{ entry._name.c_str(), entry._slot, entry._elementSize, entry._elementCount, {} };
                for ( const bool bUAV : { false, true } )
                {
                    const RHIDescriptorIndex                      index   = bUAV ? RHIDescriptorIndex{ entry._uav } : RHIDescriptorIndex{ entry._srv };
                    const optional<RHIComputeBindingLog::Binding> binding = index != kInvalidDescriptorIndex ? device.getComputeBindingLog().find( index, bUAV ) : nullopt;
                    const ShaderBindingSlot*                      pSlot   = binding.has_value() ? psoCache.findComputeBufferSlot( binding->_pso, binding->_slot, bUAV, device.getShaderTargetFormat() ) : nullptr;
                    // 크기가 다르면 셰이더가 다른 형식으로 읽는다(RAW 로 건 자리) — 틀린 이름을 붙이지 않는다.
                    if ( pSlot == nullptr || pSlot->_elementStride != entry._elementSize )
                        continue;
                    for ( const ShaderVariableInfo& member : pSlot->_listElementMember )
                    {
                        result._listMember.push_back( RenderBufferEntry::Member{ member._name, member._type, member._offset, member._size } );
                    }
                    break;
                }
                return result;
            }
        };
    } // namespace

    void RenderBufferInspector::service( IRHICommandList& cmd, IRHIDevice* pDevice, const FrameBufferTable& table, const RenderGraphExecutionContext& context, RenderPSOCache& psoCache,
                                         RenderDebugRegistry& registry )
    {
        using Internal = RenderBufferInspectorInternal;
        if ( pDevice == nullptr )
            return;
        const bool bRequested = registry.takeBufferListRequest();
        if ( bRequested )
        {
            _loggingFrames = Internal::kLoggingFrames;
            pDevice->getComputeBindingLog().setEnabled( true );
        }
        if ( _loggingFrames > 0 )
        {
            // 목록은 요청한 프레임에 곧바로, 원소 배치는 기록을 켠 뒤의 디스패치를 보고 다시 공개한다.
            vector<RenderBufferEntry> listEntry;
            for ( const FrameBufferTable::Entry& entry : table.getEntries() )
            {
                if ( entry._viewable == RenderGraphViewableKind::Buffer )
                    listEntry.push_back( Internal::makeEntry( entry, *pDevice, psoCache ) );
            }
            if ( bRequested || _loggingFrames == 1 )
                registry.publishBufferList( std::move( listEntry ) );
            if ( --_loggingFrames == 0 )
                pDevice->getComputeBindingLog().setEnabled( false );
        }

        // 읽는 중이면 도착을 본다 — 한 번에 하나만 읽는다(새 요청은 우편함에 남아 다음 차례를 기다린다).
        if ( _pendingSerial != 0 )
        {
            vector<uint8> listByte;
            uint64        tag = 0;
            if ( _readback.takeLatest( listByte, tag ) == false || tag != _tag )
                return;
            registry.publishBufferContent( RenderBufferContent{ _pendingEntry, std::move( listByte ), _pendingSerial } );
            _pendingSerial = 0;
            _readback.shutdown();
        }

        string name;
        uint32 slot   = 0;
        uint32 serial = 0;
        if ( registry.takeBufferReadRequest( name, slot, serial ) == false )
            return;
        // 실패(없는 이름 · 읽기 버퍼를 못 만듦 · 고리가 참)는 빈 결과로 알린다 — 창이 "읽지 못함" 을 보이고 다시 요청할 수 있다.
        RenderBufferContent failure{
            RenderBufferEntry{ name, slot, 0, 0, {} },
            {},
            serial
        };
        const hashed_string key = hashed_string::findInterned( name );
        for ( const FrameBufferTable::Entry& entry : table.getEntries() )
        {
            const bool bTarget = entry._name == key && entry._slot == slot && entry._viewable == RenderGraphViewableKind::Buffer;
            if ( bTarget == false )
                continue;
            failure._entry                            = RenderBufferEntry{ name, slot, entry._elementSize, entry._elementCount, {} };
            const RenderGraphAccess        lastAccess = context.findAccess( entry._name ).value_or( entry._initialAccess );
            const optional<RHIBufferState> stateAfter = RenderGraphAccessUtil::findBufferState( lastAccess );
            const bool                     bReadable  = stateAfter.has_value() &&
                                   _readback.initialize( pDevice, entry._elementSize, entry._elementCount );
            if ( bReadable && _readback.request( cmd, entry.getBuffer(), *stateAfter, ++_tag ) )
            {
                _pendingEntry  = failure._entry;
                _pendingSerial = serial;
                return;
            }
            _readback.shutdown();
            break;
        }
        registry.publishBufferContent( std::move( failure ) );
    }

    void RenderBufferInspector::release()
    {
        _readback.shutdown();
        _pendingSerial = 0;
    }
} // namespace sw
