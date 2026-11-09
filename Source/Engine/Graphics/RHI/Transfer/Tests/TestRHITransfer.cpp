/**
 * @file TestRHITransfer.cpp
 * @brief 비동기 전송 계약(`RHI/Transfer`) 테스트 — 티켓 · 제출 결과의 모양, 장치 세대, 폴링 · 재시도 대기, 시리얼 발급기, 펜스 줄, 읽기 보관표, 동기 변형, 영역 업로드 검사입니다.
 * @details 실제 백엔드의 전송은 시나리오가 본다. 여기서는 전송 계약만 흉내 내는 팩토리(`FakeTransferFactory`)로 대기 · 짝 맞추기를 본다.
 */
#include "pch.h"

#include "Core/Testing/ScopedAssertCapture.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/RHIDescriptors.h"
#include "Engine/Graphics/RHI/Transfer/RHITransferSerials.h"
#include "Engine/Graphics/RHI/Transfer/RHITransferUtil.h"

using namespace sw;

namespace
{
    constexpr uint32 kFakeDeviceGeneration = 7;

    /**
     * @brief 전송 계약만 흉내 내는 팩토리입니다. 나머지 가상 함수는 쓰이지 않는다(호출되면 0 · false 를 돌려준다).
     * @details 제출은 `_busyCountBeforeAccept` 번 `Busy` 를 돌려준 뒤 받아들이고, 폴링은 `_pendingCountBeforeFinal` 번 `Pending` 을 돌려준 뒤 `_finalStatus` 를 준다.
     *          읽기 결과를 가져가거나 버린 횟수를 세어 `RHITransferUtil` 이 짝을 맞추는지 본다. 인터페이스의 순수 가상을 모두 구현하므로 인터페이스가 바뀌면 컴파일이 깨진다.
     */
    class FakeTransferFactory final : public IRHIResourceFactory
    {
    public:
        uint32            _busyCountBeforeAccept{ 0 };
        uint32            _pendingCountBeforeFinal{ 0 };
        RHITransferStatus _finalStatus{ RHITransferStatus::Complete };
        uint8             _bRejectSubmit : 1;
        uint32            _submitCount{ 0 };
        uint32            _pollCount{ 0 };
        uint32            _takeCount{ 0 };
        uint32            _discardCount{ 0 };

        FakeTransferFactory()
            : _bRejectSubmit{ SW_FALSE }
        {
        }
        RHITransferSubmit submit()
        {
            ++_submitCount;
            if ( _bRejectSubmit )
                return RHITransferSubmit::rejected();
            if ( _submitCount <= _busyCountBeforeAccept )
                return RHITransferSubmit::busy();
            return RHITransferSubmit::accepted( _submitCount, kFakeDeviceGeneration );
        }

        RHITransferSubmit submitTextureUpload( RHITextureHandle, const RHITextureUploadDesc& ) override { return submit(); }
        RHITransferSubmit submitTextureRegionUpload( RHITextureHandle, const RHITextureRegionUploadDesc& ) override { return submit(); }
        RHITransferSubmit submitTextureReadback( RHITextureHandle, uint32, uint32, RHITextureMipSpan& ) override { return submit(); }
        RHITransferStatus pollTransfer( RHITransferTicket ) override
        {
            ++_pollCount;
            return _pollCount <= _pendingCountBeforeFinal ? RHITransferStatus::Pending : _finalStatus;
        }
        bool takeTextureReadback( RHITransferTicket, vector<uint8>& outBytes ) override
        {
            ++_takeCount;
            outBytes.assign( 4, 7 );
            return true;
        }
        void discardTextureReadback( RHITransferTicket ) override { ++_discardCount; }

        RHIPipelineStateHandle createPipelineStateNative( const RHIPipelineStateDesc& ) override { return 0; }
        RHIPipelineStateHandle createComputePipelineStateNative( string_view, string_view, const vector<string>& ) override { return 0; }
        void                   destroyPipelineState( RHIPipelineStateHandle ) override {}
        RHIRenderPassHandle    createRenderPass( const RHIRenderPassDesc& ) override { return 0; }
        void                   destroyRenderPass( RHIRenderPassHandle ) override {}
        RHIBufferHandle        createConstantBuffer( uint32 ) override { return 0; }
        void                   updateConstantBuffer( RHIBufferHandle, const void*, uint32 ) override {}
        RHIBufferHandle        createStructuredBuffer( uint32, uint32 ) override { return 0; }
        void                   updateStructuredBufferRegions( RHIBufferHandle, const void*, const RHIBufferCopyRegion*, uint32 ) override {}
        RHIBufferHandle        createVertexBuffer( const void*, uint32 ) override { return 0; }
        void                   updateGeometryBufferRegions( RHIBufferHandle, const void*, const RHIBufferCopyRegion*, uint32 ) override {}
        RHIBufferHandle        createIndexBuffer( const void*, uint32 ) override { return 0; }
        void                   destroyBuffer( RHIBufferHandle ) override {}
        RHIBufferHandle        createReadbackBuffer( uint32, uint32 ) override { return 0; }
        [[nodiscard]] bool     readReadbackBuffer( RHIBufferHandle, uint32, void* ) override { return false; }
        RHITextureHandle       createTexture2D( const RHITextureDesc& ) override { return 0; }
        void                   destroyTexture( RHITextureHandle ) override {}
        RHIFormat              getTextureFormat( RHITextureHandle ) const override { return RHIFormat::Unknown; }
        RHIDescriptorIndex     registerBindlessTexture( RHITextureHandle ) override { return kInvalidDescriptorIndex; }
        bool                   isTextureReadyToSample( RHITextureHandle ) override { return false; }
        void                   unregisterBindlessTexture( RHIDescriptorIndex ) override {}
        RHIDescriptorIndex     registerBindlessBufferSRV( RHIBufferHandle ) override { return kInvalidDescriptorIndex; }
        void                   unregisterBindlessResource( RHIDescriptorIndex ) override {}
        RHIDescriptorIndex     registerBindlessBufferUAV( RHIBufferHandle ) override { return kInvalidDescriptorIndex; }
        RHIDescriptorIndex     registerBindlessTextureUAV( RHITextureHandle ) override { return kInvalidDescriptorIndex; }
        void                   unregisterBindlessUAV( RHIDescriptorIndex ) override {}
    };
} // namespace

/**
 * @brief [RHITextureRegionUploadTest] 영역 업로드 검사 하나가 네 백엔드의 규칙이다 — 행 바이트 · 밉 크기 · 범위 · 포맷 · 데이터 크기
 * @details 64×32 R8 텍스처(밉 2 — 밉 1 은 32×16) · 면 2 로 분기마다 하나씩. 더하기가 넘치는 x + width 도 밖으로 본다.
 */
SW_TEST_CASE( RHITextureRegionUploadTest, ValidationAcceptsInsideAndRejectsTheRest )
{
    SW_EXPECT_LOG( "RHI.RegionUploadRejected" );
    uint8                      arrByte[16 * 8 * 4]{};
    RHITextureRegionUploadDesc good{};
    good._pData      = arrByte;
    good._sizeBytes  = 16 * 8;
    good._x          = 16;
    good._y          = 8;
    good._width      = 16;
    good._height     = 8;
    good._mip        = 1;
    good._arraySlice = 1;

    uint32 rowBytes{ 0 };
    SW_EXPECT_TRUE( validateTextureRegionUpload( RHIFormat::R8_UNORM, 64, 32, 2, 2, good, rowBytes ) );
    SW_EXPECT_EQUAL( 16u, rowBytes );
    RHITextureRegionUploadDesc rgba = good;
    rgba._sizeBytes                 = 16 * 8 * 4;
    SW_EXPECT_TRUE( validateTextureRegionUpload( RHIFormat::R8G8B8A8_UNORM, 64, 32, 2, 2, rgba, rowBytes ) );
    SW_EXPECT_EQUAL( 64u, rowBytes );

    RHITextureRegionUploadDesc bad = good;
    bad._x                         = 17; // 밉 1 은 32 폭 — 17 + 16 > 32
    SW_EXPECT_FALSE( validateTextureRegionUpload( RHIFormat::R8_UNORM, 64, 32, 2, 2, bad, rowBytes ) );
    bad        = good;
    bad._x     = 1;
    bad._width = constant::invalid_index::kUint32; // x + width 가 넘친다
    SW_EXPECT_FALSE( validateTextureRegionUpload( RHIFormat::R8_UNORM, 64, 32, 2, 2, bad, rowBytes ) );
    bad      = good;
    bad._mip = 2;
    SW_EXPECT_FALSE( validateTextureRegionUpload( RHIFormat::R8_UNORM, 64, 32, 2, 2, bad, rowBytes ) );
    bad             = good;
    bad._arraySlice = 2;
    SW_EXPECT_FALSE( validateTextureRegionUpload( RHIFormat::R8_UNORM, 64, 32, 2, 2, bad, rowBytes ) );
    bad         = good;
    bad._height = 0;
    SW_EXPECT_FALSE( validateTextureRegionUpload( RHIFormat::R8_UNORM, 64, 32, 2, 2, bad, rowBytes ) );
    bad            = good;
    bad._sizeBytes = 16 * 8 - 1;
    SW_EXPECT_FALSE( validateTextureRegionUpload( RHIFormat::R8_UNORM, 64, 32, 2, 2, bad, rowBytes ) );
    bad        = good;
    bad._pData = nullptr;
    SW_EXPECT_FALSE( validateTextureRegionUpload( RHIFormat::R8_UNORM, 64, 32, 2, 2, bad, rowBytes ) );
    SW_EXPECT_FALSE( validateTextureRegionUpload( RHIFormat::BC4_UNORM, 64, 32, 2, 2, good, rowBytes ) );
    SW_EXPECT_FALSE( validateTextureRegionUpload( RHIFormat::D24_UNORM_S8_UINT, 64, 32, 2, 2, good, rowBytes ) );
}

/**
 * @brief [RHITransferTest] 티켓과 제출 결과의 모양: 시리얼 0 은 무효이고, Busy · Rejected 는 티켓을 주지 않으며, 티켓은 장치 세대까지 같아야 같다
 */
SW_TEST_CASE( RHITransferTest, TicketAndSubmitShape )
{
    SW_EXPECT_FALSE( RHITransferTicket{}.isValid() );
    SW_EXPECT_TRUE( ( RHITransferTicket{ 1, 1 } ).isValid() );
    SW_EXPECT_TRUE( ( RHITransferTicket{ 7, 2 } ) == ( RHITransferTicket{ 7, 2 } ) );
    SW_EXPECT_TRUE( ( RHITransferTicket{ 7, 2 } ) != ( RHITransferTicket{ 8, 2 } ) );
    SW_EXPECT_TRUE( ( RHITransferTicket{ 7, 2 } ) != ( RHITransferTicket{ 7, 3 } ) ); // 같은 시리얼이라도 다른 장치의 티켓이다

    const RHITransferSubmit accepted = RHITransferSubmit::accepted( 5, 3 );
    SW_EXPECT_TRUE( accepted.isAccepted() );
    SW_EXPECT_EQUAL( uint64( 5 ), accepted._ticket._serial );
    SW_EXPECT_EQUAL( 3u, accepted._ticket._deviceGeneration );
    SW_EXPECT_FALSE( RHITransferSubmit::busy().isAccepted() );
    SW_EXPECT_FALSE( RHITransferSubmit::busy()._ticket.isValid() );
    SW_EXPECT_FALSE( RHITransferSubmit::rejected().isAccepted() );
    SW_EXPECT_FALSE( RHITransferSubmit{}.isAccepted() ); // 기본값은 Rejected
}

/**
 * @brief [RHITransferTest] 시리얼 0 이나 세대 0 으로 받아들인 제출을 만들면 Debug 에서 단언이 걸린다(백엔드의 실수를 잡는다)
 */
SW_TEST_CASE( RHITransferTest, AcceptingSerialZeroTripsAnAssert )
{
    test::ScopedAssertCapture assertCapture;
    const RHITransferSubmit   zeroSerial = RHITransferSubmit::accepted( 0, 1 );
    if constexpr ( test::ScopedAssertCapture::kAssertsAreActive )
        SW_EXPECT_EQUAL( 1u, assertCapture.getCount() );
    SW_EXPECT_FALSE( zeroSerial.isAccepted() ); // 단언 뒤에도 무효 티켓이라 받아들인 것으로 치지 않는다
    const RHITransferSubmit zeroGeneration = RHITransferSubmit::accepted( 4, 0 );
    if constexpr ( test::ScopedAssertCapture::kAssertsAreActive )
        SW_EXPECT_EQUAL( 2u, assertCapture.getCount() );
    (void)zeroGeneration; // 세대 0 은 단언만 확인한다
}

/**
 * @brief [RHITransferTest] 시리얼 발급기는 장치가 받은 세대를 티켓에 싣는다(세대 번호는 장치를 만드는 등록표가 붙인다 — `RHIBackendRegistry::createDevice`)
 */
SW_TEST_CASE( RHITransferTest, SerialsCarryTheDeviceGeneration )
{
    const RHITransferSerials serialsA{ 1 };
    const RHITransferSerials serialsB{ 2 };
    SW_EXPECT_TRUE( serialsA.getGeneration() == 1 );
    SW_EXPECT_TRUE( serialsA.getGeneration() != serialsB.getGeneration() );
}

/**
 * @brief [RHITransferTest] 폴링 대기: Pending 이 몇 번 돈 뒤 Complete, Failed 는 곧바로 돌아오고, 시간이 다 되면 TimedOut 이다(상태 열거형과 섞이지 않는다)
 */
SW_TEST_CASE( RHITransferTest, WaitWhilePendingStopsOnCompleteFailedAndTimeout )
{
    SW_EXPECT_LOG( "RHI.TransferTimedOut" );
    int32 pollCount{ 0 };
    SW_EXPECT_TRUE( RHITransferUtil::waitWhilePending( [&pollCount]()
    { return ++pollCount >= 3 ? RHITransferStatus::Complete : RHITransferStatus::Pending; } ) == RHITransferWaitResult::Complete );
    SW_EXPECT_EQUAL( 3, pollCount );

    pollCount = 0;
    SW_EXPECT_TRUE( RHITransferUtil::waitWhilePending( [&pollCount]()
    {
        ++pollCount;
        return RHITransferStatus::Failed;
    } ) == RHITransferWaitResult::Failed );
    SW_EXPECT_EQUAL( 1, pollCount );

    SW_EXPECT_TRUE( RHITransferUtil::waitWhilePending( []()
    { return RHITransferStatus::Pending; }, 0 ) == RHITransferWaitResult::TimedOut );
}

/**
 * @brief [RHITransferTest] 제출 재시도: Busy 는 다시 제출하고, Rejected 는 다시 시도하지 않으며, 시간이 다 되면 false(마지막 결과는 Busy 그대로)다
 */
SW_TEST_CASE( RHITransferTest, SubmitWhileBusyRetriesBusyButNotRejected )
{
    SW_EXPECT_LOG( "RHI.TransferTimedOut" );
    int32             submitCount{ 0 };
    RHITransferSubmit accepted{};
    SW_EXPECT_TRUE( RHITransferUtil::submitWhileBusy( [&submitCount]()
    { return ++submitCount >= 3 ? RHITransferSubmit::accepted( 9, 1 ) : RHITransferSubmit::busy(); }, accepted ) );
    SW_EXPECT_TRUE( accepted.isAccepted() );
    SW_EXPECT_EQUAL( 3, submitCount );

    submitCount = 0;
    RHITransferSubmit rejected{};
    SW_EXPECT_TRUE( RHITransferUtil::submitWhileBusy( [&submitCount]()
    {
        ++submitCount;
        return RHITransferSubmit::rejected();
    }, rejected ) );
    SW_EXPECT_FALSE( rejected.isAccepted() );
    SW_EXPECT_EQUAL( 1, submitCount );

    RHITransferSubmit stuck{};
    SW_EXPECT_FALSE( RHITransferUtil::submitWhileBusy( []()
    { return RHITransferSubmit::busy(); }, stuck, 0 ) );
    SW_EXPECT_TRUE( stuck._result == RHISubmitResult::Busy ); // Rejected 로 바꿔 쓰지 않는다
}

/**
 * @brief [RHITransferTest] 시리얼 발급기: 판정은 무효 · 다른 세대 · 발급 전 시리얼이 Failed, 워터마크 아래가 Complete, 나머지는 Pending 이고, 장치 손실 뒤 Pending 은 Failed 다
 */
SW_TEST_CASE( RHITransferTest, SerialsClassifyTicketsByWatermarkAndGeneration )
{
    SW_EXPECT_LOG( "RHI.RegisterBeforeTransferDone" );
    SW_EXPECT_LOG( "RHI.TransferTicketInvalid" );
    RHITransferSerials serials{ 1 };
    const uint64       first  = serials.issue();
    const uint64       second = serials.issue();
    SW_EXPECT_EQUAL( uint64( 1 ), first );
    SW_EXPECT_EQUAL( uint64( 2 ), second );

    const RHITransferTicket firstTicket{ first, serials.getGeneration() };
    const RHITransferTicket secondTicket{ second, serials.getGeneration() };
    SW_EXPECT_TRUE( serials.classify( firstTicket ) == RHITransferStatus::Pending );

    serials.advanceWatermark( first );
    SW_EXPECT_TRUE( serials.classify( firstTicket ) == RHITransferStatus::Complete );
    SW_EXPECT_TRUE( serials.classify( firstTicket ) == RHITransferStatus::Complete ); // 다시 물어도 Complete 다
    SW_EXPECT_TRUE( serials.classify( secondTicket ) == RHITransferStatus::Pending );

    serials.advanceWatermark( 0 ); // 워터마크는 내려가지 않는다
    SW_EXPECT_EQUAL( first, serials.getWatermark() );

    // bindless 등록 보호: 전송한 적 없거나 첫 업로드가 끝났으면 통과하고(뒤의 구간 갱신이 남아도), 첫 업로드가 남은 텍스처는 오류와 함께 거절한다.
    RHITextureUploadSerials neverUploaded;
    SW_EXPECT_TRUE( serials.isTextureReadyToRegister( neverUploaded, 11 ) );
    RHITextureUploadSerials updatedAfterFirst;
    updatedAfterFirst.recordWrite( first );
    updatedAfterFirst.recordWrite( second );
    SW_EXPECT_TRUE( serials.isTextureReadyToRegister( updatedAfterFirst, 11 ) );
    SW_EXPECT_EQUAL( second, updatedAfterFirst._lastSerial.load() ); // 파괴 미루기는 마지막 전송을 기다린다
    RHITextureUploadSerials firstPending;
    firstPending.recordWrite( second );
    firstPending.recordWrite( first ); // 늦게 적은 작은 시리얼은 마지막을 덮지 않는다
    SW_EXPECT_EQUAL( second, firstPending._lastSerial.load() );
    SW_EXPECT_FALSE( serials.isTextureReadyToRegister( firstPending, 11 ) );

    serials.markLost();
    SW_EXPECT_TRUE( serials.classify( firstTicket ) == RHITransferStatus::Complete ); // 이미 끝난 일은 그대로다
    SW_EXPECT_TRUE( serials.classify( secondTicket ) == RHITransferStatus::Failed );

    RHITransferSerials other{ 2 };
    SW_EXPECT_TRUE( other.classify( RHITransferTicket{} ) == RHITransferStatus::Failed );
    SW_EXPECT_TRUE( other.classify( RHITransferTicket{ 1, serials.getGeneration() } ) == RHITransferStatus::Failed ); // 다른 장치(세대)의 티켓
    SW_EXPECT_TRUE( other.classify( RHITransferTicket{ 1, other.getGeneration() } ) == RHITransferStatus::Failed );   // 발급한 적 없는 시리얼
}

/**
 * @brief [RHITransferTest] 펜스 줄: 끝난 제출은 앞에서부터 연속으로만 워터마크를 올린다. 뒤의 것이 먼저 끝나도 앞이 끝나기 전에는 Complete 가 아니다
 */
SW_TEST_CASE( RHITransferTest, FenceQueueRaisesWatermarkOnlyOverAContiguousPrefix )
{
    struct FakeProbe
    {
        bool _bDone;
    };

    RHITransferSerials               serials{ 1u }; // 장치 세대 — 시험 장치 하나
    RHITransferFenceQueue<FakeProbe> queue{ serials };
    const uint64                     first  = queue.enqueue( FakeProbe{ false } );
    const uint64                     second = queue.enqueue( FakeProbe{ true } ); // 뒤의 제출이 먼저 끝났다
    const uint64                     third  = queue.enqueue( FakeProbe{ false } );
    SW_EXPECT_EQUAL( 3u, queue.getPendingCount() );

    uint32     retiredCount{ 0 };
    const auto isDone = []( FakeProbe& probe )
    { return probe._bDone; };
    const auto retire = [&retiredCount]( FakeProbe& )
    { ++retiredCount; };

    SW_EXPECT_FALSE( queue.retireDone( isDone, retire ) ); // 맨 앞이 안 끝났다
    SW_EXPECT_EQUAL( uint64( 0 ), serials.getWatermark() );
    SW_EXPECT_FALSE( serials.isComplete( second ) );

    // 맨 앞이 끝나면 앞의 둘이 함께 빠지고 워터마크는 둘째까지 오른다. 셋째는 아직이다.
    // (probe 를 줄 안에서 바꿀 수 없으므로 isDone 이 시리얼 대신 호출 횟수로 답한다.)
    uint32     askCount{ 0 };
    const auto isDoneFirstTwo = [&askCount]( FakeProbe& )
    { return ++askCount <= 2; };
    SW_EXPECT_TRUE( queue.retireDone( isDoneFirstTwo, retire ) );
    SW_EXPECT_EQUAL( 2u, retiredCount );
    SW_EXPECT_EQUAL( second, serials.getWatermark() );
    SW_EXPECT_TRUE( serials.isComplete( first ) );
    SW_EXPECT_FALSE( serials.isComplete( third ) );
    SW_EXPECT_EQUAL( 1u, queue.getPendingCount() );

    queue.retireAll( retire ); // 장치가 쉬었다 — 남은 것이 모두 끝난 것이다
    SW_EXPECT_EQUAL( 3u, retiredCount );
    SW_EXPECT_TRUE( serials.isComplete( third ) );
    SW_EXPECT_EQUAL( 0u, queue.getPendingCount() );
}

/**
 * @brief [RHITransferTest] 읽기 보관표: 결과는 한 번만 나가고, 업로드 티켓(없는 시리얼)과 두 번째 가져가기는 false 다
 */
SW_TEST_CASE( RHITransferTest, PendingReadbackTableGivesEachResultOnce )
{
    RHIPendingReadbackTable<vector<uint8>> table;
    table.add( 4, vector<uint8>( 3, 9 ) );
    SW_EXPECT_EQUAL( 1u, table.getCount() );

    vector<uint8> bytes;
    SW_EXPECT_FALSE( table.take( 5, bytes ) ); // 읽기가 아닌 시리얼
    SW_EXPECT_TRUE( table.take( 4, bytes ) );
    SW_EXPECT_EQUAL( size_t( 3 ), bytes.size() );
    SW_EXPECT_FALSE( table.take( 4, bytes ) ); // 이미 가져갔다
    SW_EXPECT_EQUAL( 0u, table.getCount() );
}

/**
 * @brief [RHITransferTest] 동기 변형: Busy 와 Pending 을 지나 끝나고, 거절은 한 번에 false 이며, 읽기는 결과를 가져가거나 버리는 짝이 맞는다
 */
SW_TEST_CASE( RHITransferTest, AndWaitHelpersRetryBusyAndPairTakeOrDiscard )
{
    RHITextureUploadDesc       uploadDesc{};
    RHITextureRegionUploadDesc regionDesc{};
    FakeTransferFactory        uploadFactory;
    uploadFactory._busyCountBeforeAccept   = 2;
    uploadFactory._pendingCountBeforeFinal = 3;
    SW_EXPECT_TRUE( RHITransferUtil::uploadTexture2DAndWait( uploadFactory, 1, uploadDesc ) );
    SW_EXPECT_EQUAL( 3u, uploadFactory._submitCount ); // Busy 두 번 + 받아들임
    SW_EXPECT_EQUAL( 4u, uploadFactory._pollCount );   // Pending 세 번 + Complete
    SW_EXPECT_TRUE( RHITransferUtil::uploadTexture2DRegionAndWait( uploadFactory, 1, regionDesc ) );

    FakeTransferFactory rejectFactory;
    rejectFactory._bRejectSubmit = SW_TRUE;
    SW_EXPECT_FALSE( RHITransferUtil::uploadTexture2DAndWait( rejectFactory, 1, uploadDesc ) );
    SW_EXPECT_EQUAL( 1u, rejectFactory._submitCount ); // 거절은 다시 시도하지 않는다
    SW_EXPECT_EQUAL( 0u, rejectFactory._pollCount );

    vector<uint8>       bytes;
    RHITextureMipSpan   layout{};
    FakeTransferFactory readFactory;
    readFactory._pendingCountBeforeFinal = 1;
    SW_EXPECT_TRUE( RHITransferUtil::readbackTexture2DAndWait( readFactory, 1, 0, 0, bytes, layout ) );
    SW_EXPECT_EQUAL( 1u, readFactory._takeCount );
    SW_EXPECT_EQUAL( 0u, readFactory._discardCount ); // 가져갔으니 버릴 것이 없다
    SW_EXPECT_EQUAL( size_t( 4 ), bytes.size() );

    FakeTransferFactory failFactory;
    failFactory._finalStatus = RHITransferStatus::Failed;
    SW_EXPECT_FALSE( RHITransferUtil::readbackTexture2DAndWait( failFactory, 1, 0, 0, bytes, layout ) );
    SW_EXPECT_EQUAL( 0u, failFactory._takeCount );
    SW_EXPECT_EQUAL( 1u, failFactory._discardCount ); // 실패한 읽기는 결과를 버린다
}

/**
 * @brief [RHITransferTest] PendingReadback: 가져가지 않고 사라지면 소멸자가 버리고, 가져가면 버리지 않으며, 이동하면 소유가 옮겨 가고, 시간 초과 뒤에도 소멸자가 버린다
 */
SW_TEST_CASE( RHITransferTest, PendingReadbackDiscardsWhatIsNeverTaken )
{
    SW_EXPECT_LOG( "RHI.TransferTimedOut" );
    FakeTransferFactory factory;
    {
        RHITransferUtil::PendingReadback pending{
            factory, RHITransferTicket{ 1, kFakeDeviceGeneration }
        };
        SW_EXPECT_TRUE( pending.isValid() );
    }
    SW_EXPECT_EQUAL( 1u, factory._discardCount );

    {
        RHITransferUtil::PendingReadback pending{
            factory, RHITransferTicket{ 2, kFakeDeviceGeneration }
        };
        vector<uint8> bytes;
        SW_EXPECT_TRUE( pending.take( bytes ) );
        SW_EXPECT_FALSE( pending.isValid() );
    }
    SW_EXPECT_EQUAL( 1u, factory._discardCount ); // 가져간 것은 버리지 않는다

    {
        RHITransferUtil::PendingReadback first{
            factory, RHITransferTicket{ 3, kFakeDeviceGeneration }
        };
        RHITransferUtil::PendingReadback second{ std::move( first ) };
        SW_EXPECT_FALSE( first.isValid() ); // NOLINT(bugprone-use-after-move): 옮긴 뒤 비었는지가 이 단언이 보는 것이다
        SW_EXPECT_TRUE( second.isValid() );
    }
    SW_EXPECT_EQUAL( 2u, factory._discardCount ); // 이동한 뒤에도 한 번만 버린다

    factory._pendingCountBeforeFinal = 1000000;
    {
        RHITransferUtil::PendingReadback pending{
            factory, RHITransferTicket{ 4, kFakeDeviceGeneration }
        };
        const RHITransferWaitResult result = RHITransferUtil::waitWhilePending( [&pending]()
        { return pending.poll(); }, 0 );
        SW_EXPECT_TRUE( result == RHITransferWaitResult::TimedOut );
    }
    SW_EXPECT_EQUAL( 3u, factory._discardCount );
}

/**
 * @brief [RHITransferTest] 막힌 구간(`ScopedForbidBlockingTransfer`) 안에서 동기 변형을 부르면 Debug 에서 단언이 걸리고, 구간 밖에서는 걸리지 않는다
 */
SW_TEST_CASE( RHITransferTest, BlockingHelpersAssertInsideAForbiddenScope )
{
    FakeTransferFactory        factory;
    const RHITextureUploadDesc desc{};
    test::ScopedAssertCapture  assertCapture;

    SW_EXPECT_TRUE( RHITransferUtil::uploadTexture2DAndWait( factory, 1, desc ) );
    SW_EXPECT_EQUAL( 0u, assertCapture.getCount() );
    {
        const RHITransferUtil::ScopedForbidBlockingTransfer forbid;
        SW_EXPECT_TRUE( RHITransferUtil::uploadTexture2DAndWait( factory, 1, desc ) ); // 단언은 세어질 뿐 코드는 이어진다
        if constexpr ( test::ScopedAssertCapture::kAssertsAreActive )
            SW_EXPECT_TRUE( assertCapture.getCount() >= 1 );
    }
    const uint32 insideScopeCount = assertCapture.getCount();
    SW_EXPECT_TRUE( RHITransferUtil::uploadTexture2DAndWait( factory, 1, desc ) );
    SW_EXPECT_EQUAL( insideScopeCount, assertCapture.getCount() ); // 스코프를 벗어나면 다시 허용된다
}

/**
 * @brief [RHITransferTest] 읽기 티켓(`PendingReadback`)을 쥔 스레드가 제출하는 동기 변형을 부르면 Debug 에서 단언이 걸리고, 놓거나 가져간 뒤에는 걸리지 않는다
 */
SW_TEST_CASE( RHITransferTest, BlockingSubmitsAssertWhileAReadbackTicketIsHeld )
{
    FakeTransferFactory        factory;
    const RHITextureUploadDesc desc{};
    test::ScopedAssertCapture  assertCapture;

    {
        RHITransferUtil::PendingReadback pending{
            factory, RHITransferTicket{ 1, kFakeDeviceGeneration }
        };
        SW_EXPECT_TRUE( RHITransferUtil::uploadTexture2DAndWait( factory, 1, desc ) ); // 단언은 세어질 뿐 코드는 이어진다
        if constexpr ( test::ScopedAssertCapture::kAssertsAreActive )
            SW_EXPECT_TRUE( assertCapture.getCount() >= 1 );
    }
    const uint32 heldCount = assertCapture.getCount();
    SW_EXPECT_TRUE( RHITransferUtil::uploadTexture2DAndWait( factory, 1, desc ) );
    SW_EXPECT_EQUAL( heldCount, assertCapture.getCount() ); // 놓으면 다시 허용된다

    RHITransferUtil::PendingReadback taken{
        factory, RHITransferTicket{ 2, kFakeDeviceGeneration }
    };
    vector<uint8> bytes;
    SW_EXPECT_TRUE( taken.take( bytes ) );
    SW_EXPECT_TRUE( RHITransferUtil::uploadTexture2DAndWait( factory, 1, desc ) );
    SW_EXPECT_EQUAL( heldCount, assertCapture.getCount() ); // 가져간 티켓은 세지 않는다
}
