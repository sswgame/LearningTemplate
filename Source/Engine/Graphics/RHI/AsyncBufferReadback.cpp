#include "pch.h"

#include "Engine/Graphics/RHI/AsyncBufferReadback.h"

#include "Core/Concurrency/mutex.h"

#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"

namespace sw
{
    /**
     * @brief 완료 통지와 소비자가 함께 쥐는 상태입니다. 통지가 `shutdown` 뒤에도 도착하므로 고리와 수명을 따로 두고 `shared_ptr` 로 쥔다.
     */
    struct AsyncBufferReadback::State
    {
        /** @brief 읽기 버퍼 칸의 상태입니다. */
        enum class SlotState : uint8
        {
            Free,     ///< 다음 요청이 쓸 수 있다
            InFlight, ///< 요청했고 통지가 아직 안 왔다
            Blocked,  ///< 통지가 `Abandoned` 였거나 읽지 못했다 — GPU 복사 · 매핑이 아직 걸려 있을 수 있어, 뒤 요청의 `Completed` 가 올 때까지 쓰지 않는다
        };

        mutex                 _mutex;
        IRHIDevice* _pDevice  SW_GUARDED_BY( _mutex ); ///< `shutdown` 이 null 로 만든다 — 그 뒤의 콜백은 읽지 않는다
        const uint32          _sizeBytes;
        RHIBufferHandle       _arrBuffer[kSlotCount] SW_GUARDED_BY( _mutex ){};
        uint64                _arrTag[kSlotCount] SW_GUARDED_BY( _mutex ){};
        SlotState             _arrSlotState[kSlotCount] SW_GUARDED_BY( _mutex ){}; ///< 칸마다 `SlotState`(0 = Free)
        vector<uint8> _latest SW_GUARDED_BY( _mutex ){};                           ///< 가장 최근에 도착한 내용
        uint64 _latestTag     SW_GUARDED_BY( _mutex ){ 0 };
        uint8 _bHasLatest     SW_GUARDED_BY( _mutex ){ SW_FALSE };
        uint8 _bFresh         SW_GUARDED_BY( _mutex ){ SW_FALSE }; ///< `_latest` 를 아직 아무도 가져가지 않았다
        /** @brief 만든 버퍼로 상태를 세웁니다 — 크기는 이 뒤로 바뀌지 않고(const), 장치 · 버퍼는 `shutdown` 이 잠금 아래에서 비운다. */
        State( IRHIDevice* pDevice, uint32 sizeBytes, const RHIBufferHandle ( &arrBuffer )[kSlotCount] )
            : _mutex{}
            , _pDevice{ pDevice }
            , _sizeBytes{ sizeBytes }
        {
            for ( uint32 slot = 0; slot < kSlotCount; ++slot )
            {
                _arrBuffer[slot] = arrBuffer[slot];
            }
        }

        /**
         * @brief 요청 하나의 GPU 완료 통지를 받습니다. `Completed` 면 읽어 오고 칸을 비웁니다.
         * @details 칸은 읽은 **뒤에만** 비운다 — 예전에는 먼저 비워, 못 기다린 비우기(옛 한 채널 콜백)나 읽기 실패에서 매핑이 걸린 버퍼가 다음 복사의 목적지가 됐다.
         *          `Completed` 는 그 프레임 앞의 GPU 일이 모두 끝났다는 뜻이라(큐 순서) 앞서 격리한 칸도 함께 푼다.
         */
        void onGPUCompletion( uint32 slot, RHIFrameCompletion result )
        {
            ScopedLock<mutex> lock{ _mutex };
            if ( result != RHIFrameCompletion::Completed )
            {
                _arrSlotState[slot] = SlotState::Blocked;
                return;
            }
            for ( SlotState& rState : _arrSlotState )
            {
                if ( rState == SlotState::Blocked )
                    rState = SlotState::Free;
            }
            if ( _pDevice == nullptr )
            {
                _arrSlotState[slot] = SlotState::Free;
                return;
            }
            vector<uint8> bytes( _sizeBytes );
            if ( _pDevice->getResourceFactory()->readReadbackBuffer( _arrBuffer[slot], _sizeBytes, bytes.data() ) == false )
            {
                _arrSlotState[slot] = SlotState::Blocked;
                SW_LOG_WARNING_ID( "AsyncBufferReadback.FailedReadBackSlot", "Failed to read back slot %# (tag %#); the previous value stays", slot, _arrTag[slot] );
                return;
            }
            _arrSlotState[slot] = SlotState::Free;
            // 통지 순서는 요청 순서와 같지만, 더 새 내용이 이미 도착해 있으면 덮지 않는다.
            if ( _bHasLatest != SW_FALSE && _arrTag[slot] < _latestTag )
                return;
            _latest     = std::move( bytes );
            _latestTag  = _arrTag[slot];
            _bHasLatest = SW_TRUE;
            _bFresh     = SW_TRUE;
        }
    };

    AsyncBufferReadback::AsyncBufferReadback()  = default;
    AsyncBufferReadback::~AsyncBufferReadback() = default;

    bool AsyncBufferReadback::initialize( IRHIDevice* pDevice, uint32 elementSize, uint32 elementCount )
    {
        shutdown();
        const bool bInvalid = pDevice == nullptr || elementSize == 0 || elementCount == 0;
        if ( bInvalid )
            return false;
        RHIBufferHandle arrBuffer[kSlotCount]{};
        for ( uint32 slot = 0; slot < kSlotCount; ++slot )
        {
            arrBuffer[slot] = pDevice->getResourceFactory()->createReadbackBuffer( elementSize, elementCount );
            if ( arrBuffer[slot] != 0 )
                continue;
            SW_LOG_ERROR_ID( "AsyncBufferReadback.FailedCreateReadbackBuffer", "Failed to create readback buffer %# (%# x %# bytes)", slot, elementSize, elementCount );
            for ( uint32 made = 0; made < slot; ++made )
            {
                pDevice->releaseHandle( arrBuffer[made] );
            }
            return false;
        }
        _pState       = make_shared<State>( pDevice, elementSize * elementCount, arrBuffer );
        _elementSize  = elementSize;
        _elementCount = elementCount;
        return true;
    }

    void AsyncBufferReadback::shutdown()
    {
        if ( _pState == nullptr )
            return;
        // 잠금 안에서는 장치 · 버퍼를 꺼내 비우고, 장치 호출(버퍼 지우기)은 잠금 밖에서 한다(잠금은 잎 잠금, 결정 5-3).
        IRHIDevice*     pDevice = nullptr;
        RHIBufferHandle arrBuffer[kSlotCount]{};
        {
            ScopedLock<mutex> lock{ _pState->_mutex };
            pDevice           = _pState->_pDevice;
            _pState->_pDevice = nullptr; // 이미 등록된 콜백은 도착해도 읽지 않는다
            for ( uint32 slot = 0; slot < kSlotCount; ++slot )
            {
                arrBuffer[slot]           = _pState->_arrBuffer[slot];
                _pState->_arrBuffer[slot] = 0;
            }
        }
        for ( const RHIBufferHandle buffer : arrBuffer )
        {
            if ( buffer != 0 && pDevice != nullptr )
                pDevice->releaseHandle( buffer ); // 백엔드가 GPU 가 쓰는 동안은 지우지 않는다(지연 해제)
        }
        _pState.reset();
        _elementSize  = 0;
        _elementCount = 0;
    }

    bool AsyncBufferReadback::request( IRHICommandList& commands, RHIBufferHandle source, RHIBufferState stateAfter, uint64 tag )
    {
        if ( _pState == nullptr || source == 0 )
            return false;
        IRHIDevice*     pDevice = nullptr;
        uint32          slot    = kSlotCount;
        RHIBufferHandle target  = 0;
        {
            ScopedLock<mutex> lock{ _pState->_mutex };
            pDevice = _pState->_pDevice;
            for ( uint32 index = 0; index < kSlotCount && slot == kSlotCount; ++index )
            {
                if ( _pState->_arrSlotState[index] == State::SlotState::Free )
                    slot = index;
            }
            if ( slot == kSlotCount || pDevice == nullptr )
                return false;
            _pState->_arrSlotState[slot] = State::SlotState::InFlight;
            _pState->_arrTag[slot]       = tag;
            target                       = _pState->_arrBuffer[slot];
        }
        commands.transitionBuffer( source, RHIBufferState::CopySource );
        commands.copyBufferToReadback( source, target, _pState->_sizeBytes );
        commands.transitionBuffer( source, stateAfter );
        // 이 프레임의 GPU 작업이 끝난 뒤에 `Completed` 로 불린다 — 그때 읽어도 기다리지 않는다. 못 기다린 비우기는 `Abandoned` 다.
        shared_ptr<State> pState = _pState;
        auto              onDone = [pState, slot]( RHIFrameCompletion result )
        { pState->onGPUCompletion( slot, result ); };
        pDevice->notifyFrameCompletion( SW_DELEGATE_LAMBDA( RHIFrameCompletionDelegate, onDone ) );
        return true;
    }

    bool AsyncBufferReadback::takeLatest( vector<uint8>& outBytes, uint64& outTag )
    {
        if ( _pState == nullptr )
            return false;
        ScopedLock<mutex> lock{ _pState->_mutex };
        if ( _pState->_bFresh == SW_FALSE )
            return false;
        outBytes         = _pState->_latest;
        outTag           = _pState->_latestTag;
        _pState->_bFresh = SW_FALSE;
        return true;
    }
} // namespace sw
