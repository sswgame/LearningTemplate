/**
 * @file TestConstantBufferPagePool.cpp
 * @brief 상수 버퍼 페이지 풀(감사 R-4 ③)이 페이지 · 구간 디스크립터 · 전용 버퍼를 맞는 차례로 만들고 돌려주는가입니다(nogpu).
 * @details 디바이스는 가짜(`RHIFakeDevice.h` — 렌더 스레드가 없어 `releaseHandle` 이 바로 팩토리에 간다), 팩토리는 번호를 내고 사건을 적는 `FakePageFactory` 다.
 *          실패 주입(페이지 만들기 · 등록)으로 칸이 할당기에 돌아가는지도 본다.
 */
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RHI/ConstantBufferPagePool.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/RHIPipelineStateDesc.h"
#include "Engine/Graphics/RHI/Tests/RHIFakeDevice.h"

using namespace sw;

namespace
{
    /** @brief 팩토리 사건 하나입니다 — 'P'(페이지 · 전용 버퍼 만들기, 값 = 크기) · 'R'(구간 등록, 값 = 번호) · 'U'(번호 해제) · 'D'(버퍼 부수기, 값 = 핸들). */
    struct FactoryEvent
    {
        utf8   _kind;
        uint64 _value;

        bool operator==( const FactoryEvent& other ) const { return _kind == other._kind && _value == other._value; }
    };

    /** @brief 버퍼 핸들 · 번호를 차례로 내고 사건을 적는 팩토리입니다. 쓰지 않는 가상 함수는 0 · false 다. */
    class FakePageFactory final : public IRHIResourceFactory
    {
    public:
        vector<FactoryEvent>   _listEvent;
        uint64                 _nextBuffer{ 100 }; ///< 다음에 낼 버퍼 핸들의 packed 값
        uint32                 _nextIndex{ 1 };
        bool                   _bFailPage{ false };
        bool                   _bFailRegister{ false };
        RHIBufferSlice         _lastRegistered;
        vector<RHIBufferSlice> _listWritten; ///< 쓰기로 받은 구간(받은 순서)

        RHIBufferHandle createConstantBufferPage( uint32 size ) override
        {
            if ( _bFailPage )
                return 0;
            _listEvent.push_back( FactoryEvent{ 'P', size } );
            return RHIBufferHandle{ _nextBuffer++ };
        }
        RHIBufferSRVIndex registerBindlessBufferSRV( const RHIBufferSlice& slice ) override
        {
            if ( _bFailRegister )
                return kInvalidDescriptorIndex;
            _lastRegistered = slice;
            _listEvent.push_back( FactoryEvent{ 'R', _nextIndex } );
            return RHIBufferSRVIndex{ _nextIndex++ };
        }
        void unregisterBindlessResource( RHIBufferSRVIndex index ) override { _listEvent.push_back( FactoryEvent{ 'U', index.getValue() } ); }
        void writeConstantBufferSlice( const RHIBufferSlice& slice, const void*, uint32 ) override { _listWritten.push_back( slice ); }

        RHIBufferHandle         createVertexBuffer( const void*, uint32 ) override { return 0; }
        RHIBufferHandle         createGPUVertexBuffer( uint32 ) override { return 0; }
        RHIBufferHandle         createIndexBuffer( const void*, uint32 ) override { return 0; }
        void                    updateGeometryBufferRegionsNative( RHIBufferHandle, const void*, const RHIBufferCopyRegion*, uint32 ) override {}
        void                    updateStructuredBufferRegionsNative( RHIBufferHandle, const void*, const RHIBufferCopyRegion*, uint32 ) override {}
        RHITransferSubmit       submitTextureUpload( RHITextureHandle, const RHITextureUploadDesc& ) override { return RHITransferSubmit::rejected(); }
        RHITransferSubmit       submitTextureRegionUpload( RHITextureHandle, const RHITextureRegionUploadDesc& ) override { return RHITransferSubmit::rejected(); }
        RHITransferSubmit       submitTextureReadback( RHITextureHandle, uint32, uint32, RHITextureMipSpan& ) override { return RHITransferSubmit::rejected(); }
        RHITransferStatus       pollTransfer( RHITransferTicket ) override { return RHITransferStatus::Failed; }
        bool                    takeTextureReadback( RHITransferTicket, vector<uint8>& ) override { return false; }
        void                    discardTextureReadback( RHITransferTicket ) override {}
        RHIPipelineStateHandle  createPipelineStateNative( const RHIPipelineStateDesc&, const RHICompiledGraphicsStages& ) override { return 0; }
        RHIPipelineStateHandle  createComputePipelineStateNative( const RHICompiledShaderStage&, string_view ) override { return 0; }
        RHIBufferHandle         createConstantBuffer( uint32 ) override { return 0; }
        RHIBufferHandle         createStructuredBuffer( uint32, uint32 ) override { return 0; }
        RHIBufferHandle         createReadbackBuffer( uint32, uint32 ) override { return 0; }
        [[nodiscard]] bool      readReadbackBuffer( RHIBufferHandle, uint32, void* ) override { return false; }
        RHITextureHandle        createTexture2D( const RHITextureDesc& ) override { return 0; }
        RHIFormat               getTextureFormat( RHITextureHandle ) const override { return RHIFormat::Unknown; }
        RHITextureSRVIndex      registerBindlessTexture( RHITextureHandle ) override { return kInvalidDescriptorIndex; }
        bool                    rebindBindlessTexture( RHITextureSRVIndex, RHITextureHandle ) override { return false; }
        RHITextureSRVIndex      reserveBindlessTextureIndex() override { return kInvalidDescriptorIndex; }
        bool                    activateReservedBindlessTexture( RHITextureSRVIndex, RHITextureHandle ) override { return false; }
        bool                    isTextureReadyToSample( RHITextureHandle ) override { return false; }
        void                    unregisterBindlessTexture( RHITextureSRVIndex ) override {}
        RHIBufferSRVIndex       registerBindlessBufferSRV( RHIBufferHandle ) override { return kInvalidDescriptorIndex; }
        RHIUnorderedAccessIndex registerBindlessBufferUAV( RHIBufferHandle ) override { return kInvalidDescriptorIndex; }
        RHIUnorderedAccessIndex registerBindlessTextureUAV( RHITextureHandle ) override { return kInvalidDescriptorIndex; }
        void                    unregisterBindlessUAV( RHIUnorderedAccessIndex ) override {}

    protected:
        [[nodiscard]] bool destroyBufferNative( RHIBufferHandle buffer ) override
        {
            _listEvent.push_back( FactoryEvent{ 'D', buffer.getPacked() } );
            return true;
        }
        [[nodiscard]] bool destroyTextureNative( RHITextureHandle ) override { return true; }
        [[nodiscard]] bool destroyPipelineStateNative( RHIPipelineStateHandle ) override { return true; }
    };

    /** @brief 적힌 사건이 @p listExpected 와 같은지 봅니다. */
    bool hasEvents( const FakePageFactory& factory, const vector<FactoryEvent>& listExpected )
    {
        if ( factory._listEvent.size() != listExpected.size() )
            return false;
        for ( size_t index = 0; index < listExpected.size(); ++index )
        {
            if ( ( factory._listEvent[index] == listExpected[index] ) == false )
                return false;
        }
        return true;
    }

    constexpr uint64 kPageBytes = ConstantBufferSlotAllocator::kPageBytes;

    /** @brief 놓은 구간을 다시 쓸 수 있을 만큼 프레임을 엽니다. */
    void passReuseDelay( test::FakeRHIDevice& device )
    {
        for ( uint64 frame = 0; frame < ConstantBufferPagePool::kReuseDelayCount; ++frame )
        {
            device.beginFrame();
            device.endFrame();
        }
    }

    /** @brief 0 으로 찬 @p size 바이트를 씁니다. */
    bool writeZeros( ConstantBufferPagePool& pool, test::FakeRHIDevice& device, uint32 size, ConstantBufferPoolSlice& inoutSlice )
    {
        const vector<uint8> bytes( size, 0 );
        return pool.write( device, bytes.data(), size, inoutSlice );
    }
} // namespace

/**
 * @brief [ConstantBufferPagePoolTest] 쓸 때마다 새 구간을 받는다 — 같은 프레임의 두 번째 값이 첫 값의 구간을 덮지 않고, 놓은 칸 · 디스크립터는 그 프레임이 GPU 에서 끝난 뒤에 다시 쓴다
 * @details 예전 풀은 같은 구간을 덮어썼고 놓은 칸을 곧바로 다시 내줬다 — 이미 기록한 드로우가 새 값을 읽었다(DX12 · Vulkan · GL 프레임 칸 · WebGPU 큐 쓰기 · DX11 NO_OVERWRITE).
 *          변이: 옛 구간을 그대로 쓰면 둘째 단언이, 놓은 칸을 곧바로 돌려주면 넷째 쓰기가 첫 칸을 받아 단언과 쓰기 기록(`RHIConstantBufferWriteLog`)이 진다.
 */
SW_TEST_CASE( ConstantBufferPagePoolTest, EachWriteGetsItsOwnRangeUntilTheGPUIsDone )
{
    FakePageFactory     factory;
    test::FakeRHIDevice device;
    device._pResourceFactory     = &factory;
    ConstantBufferPagePool& pool = device.getConstantBufferPagePool();

    device.beginFrame();
    ConstantBufferPoolSlice slice;
    SW_REQUIRE_TRUE( writeZeros( pool, device, 200, slice ) );
    SW_EXPECT_TRUE( hasEvents( factory, {
                                            {'P', kPageBytes},
                                            {'R',          1}
    } ) );
    SW_EXPECT_EQUAL( 0u, slice.getRange()._offset );
    SW_EXPECT_EQUAL( 256u, slice.getRange()._size );
    SW_EXPECT_TRUE( slice.getDevice() == &device );

    SW_REQUIRE_TRUE( writeZeros( pool, device, 200, slice ) ); // 같은 프레임의 다른 값
    SW_EXPECT_EQUAL( 256u, slice.getRange()._offset );
    SW_EXPECT_EQUAL( RHIBufferSRVIndex{ 2 }, slice.getDescriptorIndex() );

    ConstantBufferPoolSlice other;
    slice.reset();
    SW_REQUIRE_TRUE( writeZeros( pool, device, 200, other ) ); // 방금 놓은 칸을 받지 않는다
    SW_EXPECT_EQUAL( 512u, other.getRange()._offset );
    SW_EXPECT_EQUAL( 1u, pool.getOpenPageCount() );
    device.endFrame();

    factory._listEvent.clear();
    passReuseDelay( device );
    SW_REQUIRE_TRUE( writeZeros( pool, device, 200, slice ) ); // 놓은 칸 둘이 돌아왔다
    SW_EXPECT_TRUE( hasEvents( factory, {
                                            {'U', 1},
                                            {'U', 2},
                                            {'R', 4}
    } ) );
    SW_EXPECT_TRUE( slice.getRange()._offset < 512u );
    SW_EXPECT_EQUAL( static_cast<size_t>( 4 ), factory._listWritten.size() );
    slice.reset();
    other.reset();
    pool.releaseAllPages( device );
}

/**
 * @brief [ConstantBufferPagePoolTest] 놓은 구간은 `kReuseDelayCount` 프레임 동안 남고, 그 사이의 쓰기는 다른 칸을 받는다
 * @details 변이: 경계 수를 하나 줄이면 마지막 프레임 전에 칸이 돌아와 첫 단언이 진다(GPU 가 아직 읽는다).
 */
SW_TEST_CASE( ConstantBufferPagePoolTest, RetiredRangeStaysUntilTheLastFrameInFlight )
{
    FakePageFactory     factory;
    test::FakeRHIDevice device;
    device._pResourceFactory     = &factory;
    ConstantBufferPagePool& pool = device.getConstantBufferPagePool();

    device.beginFrame();
    ConstantBufferPoolSlice neighbor; // 페이지를 열어 둔다
    ConstantBufferPoolSlice slice;
    SW_REQUIRE_TRUE( writeZeros( pool, device, 200, neighbor ) );
    SW_REQUIRE_TRUE( writeZeros( pool, device, 200, slice ) );
    const uint32 retiredOffset = slice.getRange()._offset;
    slice.reset();
    for ( uint64 frame = 1; frame < ConstantBufferPagePool::kReuseDelayCount; ++frame )
    {
        device.beginFrame();
        ConstantBufferPoolSlice probe;
        SW_REQUIRE_TRUE( writeZeros( pool, device, 200, probe ) );
        SW_EXPECT_TRUE( probe.getRange()._offset != retiredOffset );
    }
    device.beginFrame();
    SW_REQUIRE_TRUE( writeZeros( pool, device, 200, slice ) );
    SW_EXPECT_EQUAL( retiredOffset, slice.getRange()._offset );
    slice.reset();
    neighbor.reset();
    pool.releaseAllPages( device );
}

/**
 * @brief [ConstantBufferPagePoolTest] 페이지를 만들지 못하거나 구간을 등록하지 못하면 false 이고 칸은 할당기에 돌아간다(열린 페이지 0, 만든 페이지는 부순다). 든 구간은 그대로다
 */
SW_TEST_CASE( ConstantBufferPagePoolTest, FailuresReturnTheSlot )
{
    FakePageFactory     factory;
    test::FakeRHIDevice device;
    device._pResourceFactory     = &factory;
    ConstantBufferPagePool& pool = device.getConstantBufferPagePool();
    // 실패를 일부러 넣는다 — 페이지 만들기 실패 · 구간 등록 실패 · 널 데이터가 오류 한 줄씩 남긴다.
    SW_EXPECT_LOG( "ConstantBufferPagePool.CouldNotCreatePage" );
    SW_EXPECT_LOG( "ConstantBufferPagePool.CouldNotRegisterSlice" );
    SW_EXPECT_LOG( "ConstantBufferPagePool.WriteInvalid" );

    ConstantBufferPoolSlice slice;
    factory._bFailPage = true;
    SW_EXPECT_FALSE( writeZeros( pool, device, 200, slice ) );
    SW_EXPECT_FALSE( slice.isValid() );
    SW_EXPECT_EQUAL( 0u, pool.getOpenPageCount() );
    SW_EXPECT_TRUE( factory._listEvent.empty() );

    factory._bFailPage     = false;
    factory._bFailRegister = true;
    SW_EXPECT_FALSE( writeZeros( pool, device, 200, slice ) );
    SW_EXPECT_FALSE( slice.isValid() );
    SW_EXPECT_EQUAL( 0u, pool.getOpenPageCount() );
    SW_EXPECT_TRUE( hasEvents( factory, {
                                            {'P', kPageBytes},
                                            {'D',        100}
    } ) );
    SW_EXPECT_TRUE( factory._listWritten.empty() );

    factory._bFailRegister = false;
    SW_REQUIRE_TRUE( writeZeros( pool, device, 200, slice ) ); // 실패 뒤에도 같은 칸 번호를 다시 쓴다
    SW_EXPECT_EQUAL( 0u, slice.getSlot()._pageIndex );
    SW_EXPECT_EQUAL( 0u, slice.getSlot()._offset );

    SW_EXPECT_FALSE( pool.write( device, nullptr, 16, slice ) );
    SW_EXPECT_TRUE( slice.isValid() );
    SW_EXPECT_EQUAL( static_cast<size_t>( 1 ), factory._listWritten.size() );
    slice.reset();
    pool.releaseAllPages( device );
}

/**
 * @brief [ConstantBufferPagePoolTest] 등급 밖(4096 B 초과)은 전용 버퍼 하나와 그 전체 구간이다. 전용 ↔ 등급 칸을 오가며 놓은 디스크립터 · 전용 버퍼 · 빈 페이지는 경계가 지난 뒤 놓인다
 */
SW_TEST_CASE( ConstantBufferPagePoolTest, MovesBetweenDedicatedAndClassSlots )
{
    FakePageFactory     factory;
    test::FakeRHIDevice device;
    device._pResourceFactory     = &factory;
    ConstantBufferPagePool& pool = device.getConstantBufferPagePool();

    ConstantBufferPoolSlice slice;
    SW_REQUIRE_TRUE( writeZeros( pool, device, 5000, slice ) );
    SW_EXPECT_TRUE( hasEvents( factory, {
                                            {'P', 5120},
                                            {'R',    1}
    } ) ); // 256 B 로 올린 크기
    SW_EXPECT_FALSE( slice.getSlot().isValid() );
    SW_EXPECT_EQUAL( 0u, slice.getRange()._offset );
    SW_EXPECT_EQUAL( 5120u, factory._lastRegistered._size );
    SW_EXPECT_EQUAL( 0u, pool.getOpenPageCount() );

    factory._listEvent.clear();
    SW_REQUIRE_TRUE( writeZeros( pool, device, 100, slice ) ); // 등급 칸으로 — 옛 전용 버퍼는 남는다
    SW_EXPECT_TRUE( hasEvents( factory, {
                                            {'P', kPageBytes},
                                            {'R',          2}
    } ) );
    SW_EXPECT_TRUE( slice.getSlot().isValid() );

    factory._listEvent.clear();
    passReuseDelay( device );
    SW_REQUIRE_TRUE( writeZeros( pool, device, 9000, slice ) ); // 다시 전용 — 첫 전용 버퍼가 이제 놓인다
    SW_EXPECT_TRUE( hasEvents( factory, {
                                            {'U',    1},
                                            {'D',  100},
                                            {'P', 9216},
                                            {'R',    3}
    } ) );
    SW_EXPECT_EQUAL( 1u, pool.getOpenPageCount() ); // 놓은 등급 칸의 페이지는 아직 읽힌다

    factory._listEvent.clear();
    passReuseDelay( device );
    SW_REQUIRE_TRUE( writeZeros( pool, device, 9000, slice ) );
    SW_EXPECT_TRUE( hasEvents( factory, {
                                            {'U',    2},
                                            {'D',  101},
                                            {'P', 9216},
                                            {'R',    4}
    } ) );
    SW_EXPECT_EQUAL( 0u, pool.getOpenPageCount() );
    slice.reset();
    pool.releaseAllPages( device );
}

/**
 * @brief [ConstantBufferPagePoolTest] `releaseAllPages`(디바이스 종료)가 놓은 구간과 남은 페이지를 모두 돌려준다. `detach` 은 아무것도 돌려주지 않는다
 */
SW_TEST_CASE( ConstantBufferPagePoolTest, ReleaseAllPagesReturnsOpenPages )
{
    FakePageFactory     factory;
    test::FakeRHIDevice device;
    device._pResourceFactory     = &factory;
    ConstantBufferPagePool& pool = device.getConstantBufferPagePool();

    ConstantBufferPoolSlice first;
    ConstantBufferPoolSlice second;
    SW_REQUIRE_TRUE( writeZeros( pool, device, 200, first ) );
    SW_REQUIRE_TRUE( writeZeros( pool, device, 200, second ) );
    SW_EXPECT_EQUAL( 1u, pool.getOpenPageCount() );
    SW_EXPECT_EQUAL( 256u, second.getRange()._offset );

    factory._listEvent.clear();
    first.reset();
    SW_EXPECT_FALSE( first.isValid() );
    second.detach(); // 디바이스가 사라졌을 때처럼 핸들만 잊는다
    SW_EXPECT_TRUE( factory._listEvent.empty() );
    pool.releaseAllPages( device );
    SW_EXPECT_TRUE( hasEvents( factory, {
                                            {'U',   1},
                                            {'D', 100}
    } ) );
    SW_EXPECT_EQUAL( 0u, pool.getOpenPageCount() );
}
