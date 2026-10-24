#include "pch.h"

#include "Engine/Graphics/RHI/AsyncBufferReadback.h"

#include "Core/Concurrency/mutex.h"

#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"

namespace sw
{
    SW_LOG_CALLER( "AsyncBufferReadback" );

    /**
     * @brief 해제 큐 콜백과 소비자가 함께 쥐는 상태입니다. 콜백이 `shutdown` 뒤에도 도착하므로 고리와 수명을 따로 두고 `shared_ptr` 로 쥔다.
     */
    struct AsyncBufferReadback::State
    {
        mutex           _mutex;
        IRHIDevice*     _pDevice{ nullptr }; ///< `shutdown` 이 null 로 만든다 — 그 뒤의 콜백은 읽지 않는다
        uint32          _sizeBytes{ 0 };
        RHIBufferHandle _arrBuffer[kSlotCount]{};
        uint64          _arrTag[kSlotCount]{};
        uint8           _arrBusy[kSlotCount]{}; ///< GPU 가 쓰는 중(요청했고 콜백이 아직 안 왔다)
        vector<uint8>   _latest{};              ///< 가장 최근에 도착한 내용
        uint64          _latestTag{ 0 };
        uint8           _bHasLatest{ SW_FALSE };
        uint8           _bFresh{ SW_FALSE }; ///< `_latest` 를 아직 아무도 가져가지 않았다

        /** @brief 요청 하나의 GPU 작업이 끝났을 때(해제 큐 콜백) 부릅니다. 읽어 오고 슬롯을 비웁니다. */
        void onGPUDone( uint32 slot )
        {
            std::scoped_lock<mutex> lock{ _mutex };
            _arrBusy[slot] = SW_FALSE;
            if ( _pDevice == nullptr )
                return;
            vector<uint8> bytes( _sizeBytes );
            if ( _pDevice->getResourceFactory()->readReadbackBuffer( _arrBuffer[slot], _sizeBytes, bytes.data() ) == false )
            {
                SW_LOG_WARNING( "Failed to read back slot %# (tag %#); the previous value stays", slot, _arrTag[slot] );
                return;
            }
            // 콜백 순서는 요청 순서와 같지만, 더 새 내용이 이미 도착해 있으면 덮지 않는다.
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
        shared_ptr<State> pState = make_shared<State>();
        pState->_pDevice         = pDevice;
        pState->_sizeBytes       = elementSize * elementCount;
        for ( uint32 slot = 0; slot < kSlotCount; ++slot )
        {
            pState->_arrBuffer[slot] = pDevice->getResourceFactory()->createReadbackBuffer( elementSize, elementCount );
            if ( pState->_arrBuffer[slot] != 0 )
                continue;
            SW_LOG_ERROR( "Failed to create readback buffer %# (%# x %# bytes)", slot, elementSize, elementCount );
            for ( uint32 made = 0; made < slot; ++made )
            {
                pDevice->getResourceFactory()->destroyBuffer( pState->_arrBuffer[made] );
            }
            return false;
        }
        _pState       = std::move( pState );
        _elementSize  = elementSize;
        _elementCount = elementCount;
        return true;
    }

    void AsyncBufferReadback::shutdown()
    {
        if ( _pState == nullptr )
            return;
        {
            std::scoped_lock<mutex> lock{ _pState->_mutex };
            IRHIDevice*             pDevice = _pState->_pDevice;
            _pState->_pDevice               = nullptr; // 이미 등록된 콜백은 도착해도 읽지 않는다
            for ( RHIBufferHandle& buffer : _pState->_arrBuffer )
            {
                if ( buffer != 0 && pDevice != nullptr )
                    pDevice->getResourceFactory()->destroyBuffer( buffer ); // 백엔드가 GPU 가 쓰는 동안은 지우지 않는다(지연 해제)
                buffer = 0;
            }
        }
        _pState.reset();
        _elementSize  = 0;
        _elementCount = 0;
    }

    bool AsyncBufferReadback::request( IRHICommandList& commands, RHIBufferHandle source, RHIBufferState stateAfter, uint64 tag )
    {
        if ( _pState == nullptr || source == 0 )
            return false;
        IRHIDevice* pDevice = nullptr;
        uint32      slot    = kSlotCount;
        {
            std::scoped_lock<mutex> lock{ _pState->_mutex };
            pDevice = _pState->_pDevice;
            for ( uint32 index = 0; index < kSlotCount && slot == kSlotCount; ++index )
            {
                if ( _pState->_arrBusy[index] == SW_FALSE )
                    slot = index;
            }
            if ( slot == kSlotCount || pDevice == nullptr )
                return false;
            _pState->_arrBusy[slot] = SW_TRUE;
            _pState->_arrTag[slot]  = tag;
        }
        commands.transitionBuffer( source, RHIBufferState::CopySource );
        commands.copyBufferToReadback( source, _pState->_arrBuffer[slot], _pState->_sizeBytes );
        commands.transitionBuffer( source, stateAfter );
        // 이 프레임의 GPU 작업이 끝난 뒤에 불린다 — 그때 읽어도 기다리지 않는다.
        shared_ptr<State> pState = _pState;
        auto              onDone = [pState, slot]()
        { pState->onGPUDone( slot ); };
        pDevice->enqueueGPURelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, onDone ) );
        return true;
    }

    bool AsyncBufferReadback::takeLatest( vector<uint8>& outBytes, uint64& outTag )
    {
        if ( _pState == nullptr )
            return false;
        std::scoped_lock<mutex> lock{ _pState->_mutex };
        if ( _pState->_bFresh == SW_FALSE )
            return false;
        outBytes         = _pState->_latest;
        outTag           = _pState->_latestTag;
        _pState->_bFresh = SW_FALSE;
        return true;
    }
} // namespace sw
