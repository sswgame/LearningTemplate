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
        vector<FactoryEvent> _listEvent;
        RHIBufferHandle      _nextBuffer{ 100 };
        RHIDescriptorIndex   _nextIndex{ 1 };
        bool                 _bFailPage{ false };     ///< NOLINT(sw-bool-flag): 테스트의 실패 주입 스위치
        bool                 _bFailRegister{ false }; ///< NOLINT(sw-bool-flag): 테스트의 실패 주입 스위치
        RHIBufferSlice       _lastRegistered;

        RHIBufferHandle createConstantBufferPage( uint32 size ) override
        {
            if ( _bFailPage )
                return 0;
            _listEvent.push_back( FactoryEvent{ 'P', size } );
            return _nextBuffer++;
        }
        RHIDescriptorIndex registerBindlessBufferSRV( const RHIBufferSlice& slice ) override
        {
            if ( _bFailRegister )
                return kInvalidDescriptorIndex;
            _lastRegistered = slice;
            _listEvent.push_back( FactoryEvent{ 'R', _nextIndex } );
            return _nextIndex++;
        }
        void unregisterBindlessResource( RHIDescriptorIndex index ) override { _listEvent.push_back( FactoryEvent{ 'U', index } ); }
        void destroyBuffer( RHIBufferHandle buffer ) override { _listEvent.push_back( FactoryEvent{ 'D', buffer } ); }
        void updateConstantBuffer( const RHIBufferSlice&, const void*, uint32 ) override {}

        RHIBufferHandle        createVertexBuffer( const void*, uint32 ) override { return 0; }
        RHIBufferHandle        createGPUVertexBuffer( uint32 ) override { return 0; }
        RHIBufferHandle        createIndexBuffer( const void*, uint32 ) override { return 0; }
        void                   updateGeometryBufferRegions( RHIBufferHandle, const void*, const RHIBufferCopyRegion*, uint32 ) override {}
        void                   updateStructuredBufferRegions( RHIBufferHandle, const void*, const RHIBufferCopyRegion*, uint32 ) override {}
        RHITransferSubmit      submitTextureUpload( RHITextureHandle, const RHITextureUploadDesc& ) override { return RHITransferSubmit::rejected(); }
        RHITransferSubmit      submitTextureRegionUpload( RHITextureHandle, const RHITextureRegionUploadDesc& ) override { return RHITransferSubmit::rejected(); }
        RHITransferSubmit      submitTextureReadback( RHITextureHandle, uint32, uint32, RHITextureMipSpan& ) override { return RHITransferSubmit::rejected(); }
        RHITransferStatus      pollTransfer( RHITransferTicket ) override { return RHITransferStatus::Failed; }
        bool                   takeTextureReadback( RHITransferTicket, vector<uint8>& ) override { return false; }
        void                   discardTextureReadback( RHITransferTicket ) override {}
        RHIPipelineStateHandle createPipelineStateNative( const RHIPipelineStateDesc& ) override { return 0; }
        RHIPipelineStateHandle createComputePipelineStateNative( string_view, string_view, const vector<string>& ) override { return 0; }
        void                   destroyPipelineState( RHIPipelineStateHandle ) override {}
        RHIRenderPassHandle    createRenderPass( const RHIRenderPassDesc& ) override { return 0; }
        void                   destroyRenderPass( RHIRenderPassHandle ) override {}
        RHIBufferHandle        createConstantBuffer( uint32 ) override { return 0; }
        void                   updateConstantBuffer( RHIBufferHandle, const void*, uint32 ) override {}
        RHIBufferHandle        createStructuredBuffer( uint32, uint32 ) override { return 0; }
        RHIBufferHandle        createReadbackBuffer( uint32, uint32 ) override { return 0; }
        [[nodiscard]] bool     readReadbackBuffer( RHIBufferHandle, uint32, void* ) override { return false; }
        RHITextureHandle       createTexture2D( const RHITextureDesc& ) override { return 0; }
        void                   destroyTexture( RHITextureHandle ) override {}
        RHIFormat              getTextureFormat( RHITextureHandle ) const override { return RHIFormat::Unknown; }
        RHIDescriptorIndex     registerBindlessTexture( RHITextureHandle ) override { return kInvalidDescriptorIndex; }
        bool                   rebindBindlessTexture( RHIDescriptorIndex, RHITextureHandle ) override { return false; }
        RHIDescriptorIndex     reserveBindlessTextureIndex() override { return kInvalidDescriptorIndex; }
        bool                   activateReservedBindlessTexture( RHIDescriptorIndex, RHITextureHandle ) override { return false; }
        bool                   isTextureReadyToSample( RHITextureHandle ) override { return false; }
        void                   unregisterBindlessTexture( RHIDescriptorIndex ) override {}
        RHIDescriptorIndex     registerBindlessBufferSRV( RHIBufferHandle ) override { return kInvalidDescriptorIndex; }
        RHIDescriptorIndex     registerBindlessBufferUAV( RHIBufferHandle ) override { return kInvalidDescriptorIndex; }
        RHIDescriptorIndex     registerBindlessTextureUAV( RHITextureHandle ) override { return kInvalidDescriptorIndex; }
        void                   unregisterBindlessUAV( RHIDescriptorIndex ) override {}
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
} // namespace

/**
 * @brief [ConstantBufferPagePoolTest] 같은 등급 안의 변화는 그대로이고, 등급이 커지면 옛 디스크립터 → 닫힌 옛 페이지 → 새 페이지 → 새 디스크립터 차례로 옮긴다
 * @details 변이: 옛 디스크립터를 놓지 않으면 'U' 가 빠지고, 새 칸이 닫힌 옛 번호를 받으면(할당 전에 놓기) 새 페이지 핸들이 부서진다.
 */
SW_TEST_CASE( ConstantBufferPagePoolTest, MovesToTheFittingClassInOrder )
{
    FakePageFactory     factory;
    test::FakeRHIDevice device;
    device._pResourceFactory     = &factory;
    ConstantBufferPagePool& pool = device.getConstantBufferPagePool();

    ConstantBufferPoolSlice slice;
    SW_REQUIRE_TRUE( pool.fit( device, 200, slice ) );
    SW_EXPECT_TRUE( hasEvents( factory, {
                                            {'P', kPageBytes},
                                            {'R',          1}
    } ) );
    SW_EXPECT_EQUAL( RHIBufferHandle{ 100 }, slice.getRange()._buffer );
    SW_EXPECT_EQUAL( 256u, slice.getRange()._size );
    SW_EXPECT_TRUE( slice.getDevice() == &device );

    factory._listEvent.clear();
    SW_REQUIRE_TRUE( pool.fit( device, 256, slice ) ); // 같은 등급 — 그대로
    SW_EXPECT_TRUE( factory._listEvent.empty() );
    SW_EXPECT_EQUAL( RHIDescriptorIndex{ 1 }, slice.getDescriptorIndex() );

    SW_REQUIRE_TRUE( pool.fit( device, 1500, slice ) ); // 2048 B 등급
    SW_EXPECT_TRUE( hasEvents( factory, {
                                            {'U',          1},
                                            {'D',        100},
                                            {'P', kPageBytes},
                                            {'R',          2}
    } ) );
    SW_EXPECT_EQUAL( RHIBufferHandle{ 101 }, slice.getRange()._buffer );
    SW_EXPECT_EQUAL( 2048u, slice.getRange()._size );
    SW_EXPECT_EQUAL( 1u, pool.getOpenPageCount() );

    factory._listEvent.clear();
    slice.reset(); // 마지막 칸 — 디스크립터를 놓고 페이지를 반납한다
    SW_EXPECT_TRUE( hasEvents( factory, {
                                            {'U',   2},
                                            {'D', 101}
    } ) );
    SW_EXPECT_EQUAL( 0u, pool.getOpenPageCount() );
    SW_EXPECT_FALSE( slice.isValid() );
}

/**
 * @brief [ConstantBufferPagePoolTest] 페이지를 만들지 못하거나 구간을 등록하지 못하면 false 이고 칸은 할당기에 돌아간다(열린 페이지 0, 만든 페이지는 부순다)
 */
SW_TEST_CASE( ConstantBufferPagePoolTest, FailuresReturnTheSlot )
{
    FakePageFactory     factory;
    test::FakeRHIDevice device;
    device._pResourceFactory     = &factory;
    ConstantBufferPagePool& pool = device.getConstantBufferPagePool();

    ConstantBufferPoolSlice slice;
    factory._bFailPage = true;
    SW_EXPECT_FALSE( pool.fit( device, 200, slice ) );
    SW_EXPECT_FALSE( slice.isValid() );
    SW_EXPECT_EQUAL( 0u, pool.getOpenPageCount() );
    SW_EXPECT_TRUE( factory._listEvent.empty() );

    factory._bFailPage     = false;
    factory._bFailRegister = true;
    SW_EXPECT_FALSE( pool.fit( device, 200, slice ) );
    SW_EXPECT_FALSE( slice.isValid() );
    SW_EXPECT_EQUAL( 0u, pool.getOpenPageCount() );
    SW_EXPECT_TRUE( hasEvents( factory, {
                                            {'P', kPageBytes},
                                            {'D',        100}
    } ) );

    factory._bFailRegister = false;
    factory._listEvent.clear();
    SW_REQUIRE_TRUE( pool.fit( device, 200, slice ) ); // 실패 뒤에도 같은 칸 번호를 다시 쓴다
    SW_EXPECT_EQUAL( 0u, slice.getSlot()._pageIndex );
    SW_EXPECT_EQUAL( 0u, slice.getSlot()._offset );
}

/**
 * @brief [ConstantBufferPagePoolTest] 등급 밖(4096 B 초과)은 전용 버퍼 하나와 그 전체 구간이고, 전용 ↔ 등급 칸을 오가며 옛 디스크립터 · 옛 버퍼를 놓는다
 */
SW_TEST_CASE( ConstantBufferPagePoolTest, MovesBetweenDedicatedAndClassSlots )
{
    FakePageFactory     factory;
    test::FakeRHIDevice device;
    device._pResourceFactory     = &factory;
    ConstantBufferPagePool& pool = device.getConstantBufferPagePool();

    ConstantBufferPoolSlice slice;
    SW_REQUIRE_TRUE( pool.fit( device, 5000, slice ) );
    SW_EXPECT_TRUE( hasEvents( factory, {
                                            {'P', 5120},
                                            {'R',    1}
    } ) ); // 256 B 로 올린 크기
    SW_EXPECT_FALSE( slice.getSlot().isValid() );
    SW_EXPECT_EQUAL( 0u, slice.getRange()._offset );
    SW_EXPECT_EQUAL( 5120u, factory._lastRegistered._size );
    SW_EXPECT_EQUAL( 0u, pool.getOpenPageCount() );

    factory._listEvent.clear();
    SW_REQUIRE_TRUE( pool.fit( device, 5100, slice ) ); // 전용 버퍼에 담긴다 — 그대로
    SW_EXPECT_TRUE( factory._listEvent.empty() );

    SW_REQUIRE_TRUE( pool.fit( device, 100, slice ) ); // 등급 칸으로
    SW_EXPECT_TRUE( hasEvents( factory, {
                                            {'U',          1},
                                            {'D',        100},
                                            {'P', kPageBytes},
                                            {'R',          2}
    } ) );
    SW_EXPECT_TRUE( slice.getSlot().isValid() );

    factory._listEvent.clear();
    SW_REQUIRE_TRUE( pool.fit( device, 9000, slice ) ); // 다시 전용 — 칸의 페이지가 닫힌다
    SW_EXPECT_TRUE( hasEvents( factory, {
                                            {'U',    2},
                                            {'D',  101},
                                            {'P', 9216},
                                            {'R',    3}
    } ) );
    SW_EXPECT_EQUAL( 0u, pool.getOpenPageCount() );

    factory._listEvent.clear();
    slice.reset();
    SW_EXPECT_TRUE( hasEvents( factory, {
                                            {'U',   3},
                                            {'D', 102}
    } ) );
}

/**
 * @brief [ConstantBufferPagePoolTest] 칸 하나를 놓아도 이웃이 남으면 페이지는 열려 있고, `releaseAllPages`(디바이스 종료)가 남은 페이지를 돌려준다. `detach` 은 아무것도 돌려주지 않는다
 */
SW_TEST_CASE( ConstantBufferPagePoolTest, ReleaseAllPagesReturnsOpenPages )
{
    FakePageFactory     factory;
    test::FakeRHIDevice device;
    device._pResourceFactory     = &factory;
    ConstantBufferPagePool& pool = device.getConstantBufferPagePool();

    ConstantBufferPoolSlice first;
    ConstantBufferPoolSlice second;
    SW_REQUIRE_TRUE( pool.fit( device, 200, first ) );
    SW_REQUIRE_TRUE( pool.fit( device, 200, second ) );
    SW_EXPECT_EQUAL( 1u, pool.getOpenPageCount() );
    SW_EXPECT_EQUAL( 256u, second.getRange()._offset );

    factory._listEvent.clear();
    first.reset();
    SW_EXPECT_TRUE( hasEvents( factory, {
                                            { 'U', 1 }
    } ) ); // 이웃이 남아 페이지는 그대로
    SW_EXPECT_EQUAL( 1u, pool.getOpenPageCount() );

    factory._listEvent.clear();
    second.detach(); // 디바이스가 사라졌을 때처럼 핸들만 잊는다
    SW_EXPECT_TRUE( factory._listEvent.empty() );
    pool.releaseAllPages( device );
    SW_EXPECT_TRUE( hasEvents( factory, {
                                            { 'D', 100 }
    } ) );
    SW_EXPECT_EQUAL( 0u, pool.getOpenPageCount() );
}
