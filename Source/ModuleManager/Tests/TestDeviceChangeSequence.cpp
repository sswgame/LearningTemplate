// DeviceChangeSequenceTest — 장치 교체 · 다시 만들기의 순서(모듈 내리기 → 장치 → 모듈 세우기)와, 쓸 장치가 없거나 모듈을 다시 세우지 못하면 종료를 요청하는지(nogpu).
// 장치는 가짜 렌더 드라이버가 맡는다 — 진짜 `GameModuleManager` 를 세우면 소멸이 하네스의 게임 모듈 타입 등록을 걷어 내므로, 주인이 넘기는 순서(`DeviceChangeSequence`)를 본다.
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Foundation/QuitRequest.h"
#include "Engine/Graphics/RHI/RHIInitResult.h"
#include "Engine/Presentation/IEngineRenderDriver.h"

#include "ModuleManager/DeviceChangeSequence.h"

#include "sw/config/ProcessExitCodes.gen.h"

using namespace sw;

namespace
{
    /** @brief 이 TU 전용 도우미 모음입니다 — 순서 기록의 낱말과 기록을 채우는 순서 · 기록 대조. */
    struct TestDeviceChangeSequenceInternal
    {
        static constexpr const utf8* kSuspendEvent  = "suspend";
        static constexpr const utf8* kDeviceEvent   = "device";
        static constexpr const utf8* kRecreateEvent = "recreate";

        /** @brief 모듈 몫을 순서 기록으로 채운 순서입니다. @p bModulesRecreated 는 모듈 다시 세우기의 답입니다. */
        static DeviceChangeSequence makeRecordingSequence( vector<const utf8*>& outEvents, bool bModulesRecreated )
        {
            DeviceChangeSequence sequence;
            sequence._suspendModules  = SW_DELEGATE_LAMBDA( Delegate<void()>, [&outEvents]()
             { outEvents.push_back( kSuspendEvent ); } );
            sequence._recreateModules = SW_DELEGATE_LAMBDA( Delegate<bool()>, [&outEvents, bModulesRecreated]()
            {
                outEvents.push_back( kRecreateEvent );
                return bModulesRecreated;
            } );
            return sequence;
        }

        /** @brief 기록이 @p expected 와 같은 낱말 · 같은 순서인지입니다. */
        static bool isSameOrder( const vector<const utf8*>& events, std::initializer_list<const utf8*> expected )
        {
            if ( events.size() != expected.size() )
                return false;
            size_t index = 0;
            for ( const utf8* pExpected : expected )
            {
                if ( events[index] != pExpected )
                    return false;
                ++index;
            }
            return true;
        }
    };

    /** @brief 장치 교체만 흉내 내는 렌더 드라이버입니다. 예약 여부와 교체 결과를 케이스가 정하고, 교체를 부르면 순서 기록에 `device` 를 적습니다. */
    class FakeDeviceRenderDriver final : public IEngineRenderDriver
    {
    public:
        FakeDeviceRenderDriver( vector<const utf8*>& outEvents, bool bPending, BackendChangeResult result )
            : _events{ outEvents }
            , _result{ result }
            , _bPending{ static_cast<uint8>( bPending ? SW_TRUE : SW_FALSE ) }
            , _reserved{ 0 }
        {
        }

        void                onStartupFinished() override {}
        void                onShutdownBegin() override {}
        RHI*                findRHI() const override { return nullptr; }
        RHIInitResult       getRHIInitResult() const override { return RHIInitResult::NotStarted; }
        const IRHIDevice*   findDevice() const override { return nullptr; }
        RenderThread*       findRenderThread() const override { return nullptr; }
        void                flushSubmittedPackets() override {}
        void                waitRenderThreadIdle( RHIWaitIdleReason /*reason*/ ) override {}
        void                submitFrame( float32 /*deltaTime*/, const FrameViewports& /*views*/, const ViewCameraProviderDelegate& /*provider*/, FrameDrawMode /*drawMode*/ ) override {}
        bool                hasPendingBackendChange() const override { return _bPending == SW_TRUE; }
        BackendChangeResult applyPendingBackendChange() override
        {
            _events.push_back( TestDeviceChangeSequenceInternal::kDeviceEvent );
            _bPending = SW_FALSE;
            return _result;
        }
        void              discardViewTargets() override {}
        void              setScenarioCaptureEnabled( bool /*bEnabled*/ ) override {}
        void              setPresentInterval( uint32 /*interval*/ ) override {}
        uint32            countPendingCaptureJobs( const GameObjectManager* /*pManager*/ ) const override { return 0; }
        void              overrideDeviceLostPolicy( RHIDeviceLostPolicy /*policy*/ ) override {}
        const DevCounter& getDeviceRecreateCount() const override { return _unusedCounter; }
        const DevCounter& getFailedBackendChangeCount() const override { return _unusedCounter; }

    private:
        DevCounter                _unusedCounter; ///< 이 대역은 장치를 다시 만들지 않는다 — 늘 0 인 칸
        vector<const utf8*>&      _events;
        const BackendChangeResult _result;
        uint8                     _bPending : 1;
        [[maybe_unused]] uint8    _reserved : 7;
    };
} // namespace

/** @brief [DeviceChangeSequenceTest] 예약이 없으면 모듈도 장치도 건드리지 않고 성공이다 */
SW_TEST_CASE( DeviceChangeSequenceTest, NothingPendingTouchesNothing )
{
    using Util = TestDeviceChangeSequenceInternal;
    vector<const utf8*>        events;
    FakeDeviceRenderDriver     driver{ events, false, BackendChangeResult::Applied };
    QuitRequest                quit;
    const DeviceChangeSequence sequence = Util::makeRecordingSequence( events, true );
    SW_EXPECT_TRUE( sequence.apply( driver, quit ) );
    SW_EXPECT_TRUE( events.empty() );
    SW_EXPECT_FALSE( quit.isRequested() );
}

/** @brief [DeviceChangeSequenceTest] 모듈 인스턴스를 내린 뒤 장치를 다시 만들고, 그 뒤 모듈을 다시 세운다 — 순서를 어기면 모듈이 옛 장치의 자원을 쥔 채 장치가 바뀐다 */
SW_TEST_CASE( DeviceChangeSequenceTest, SuspendsThenRecreatesTheDeviceThenTheModules )
{
    using Util = TestDeviceChangeSequenceInternal;
    vector<const utf8*>        events;
    FakeDeviceRenderDriver     driver{ events, true, BackendChangeResult::Applied };
    QuitRequest                quit;
    const DeviceChangeSequence sequence = Util::makeRecordingSequence( events, true );
    SW_EXPECT_TRUE( sequence.apply( driver, quit ) );
    SW_EXPECT_TRUE_MSG( Util::isSameOrder( events, { Util::kSuspendEvent, Util::kDeviceEvent, Util::kRecreateEvent } ), "suspend modules -> recreate device -> recreate modules" );
    SW_EXPECT_FALSE( quit.isRequested() );
}

/** @brief [DeviceChangeSequenceTest] 쓸 장치가 없으면 모듈을 다시 세우지 않고 종료(DeviceLost)를 요청한다 — 장치 없이 돌지 않는다 */
SW_TEST_CASE( DeviceChangeSequenceTest, NoUsableDeviceQuitsWithoutRecreatingModules )
{
    using Util = TestDeviceChangeSequenceInternal;
    SW_EXPECT_LOG( "DeviceChangeSequence.NoUsableDevice" );
    vector<const utf8*>        events;
    FakeDeviceRenderDriver     driver{ events, true, BackendChangeResult::Failed };
    QuitRequest                quit;
    const DeviceChangeSequence sequence = Util::makeRecordingSequence( events, true );
    SW_EXPECT_FALSE( sequence.apply( driver, quit ) );
    SW_EXPECT_TRUE_MSG( Util::isSameOrder( events, { Util::kSuspendEvent, Util::kDeviceEvent } ), "the modules must stay down without a device" );
    SW_EXPECT_TRUE( quit.isRequested() );
    SW_EXPECT_TRUE( quit.getExitCode() == ProcessExitCode::DeviceLost );
}

/** @brief [DeviceChangeSequenceTest] 장치는 섰지만 모듈(게임 · 에디터)을 다시 세우지 못하면 종료(DeviceLost)를 요청한다 — 예전에는 로그만 남기고 게임 인스턴스 없이 돌았다 */
SW_TEST_CASE( DeviceChangeSequenceTest, ModuleRecreateFailureQuits )
{
    using Util = TestDeviceChangeSequenceInternal;
    SW_EXPECT_LOG( "DeviceChangeSequence.ModulesNotRecreated" );
    vector<const utf8*>        events;
    FakeDeviceRenderDriver     driver{ events, true, BackendChangeResult::Applied };
    QuitRequest                quit;
    const DeviceChangeSequence sequence = Util::makeRecordingSequence( events, false );
    SW_EXPECT_FALSE( sequence.apply( driver, quit ) );
    SW_EXPECT_TRUE( Util::isSameOrder( events, { Util::kSuspendEvent, Util::kDeviceEvent, Util::kRecreateEvent } ) );
    SW_EXPECT_TRUE( quit.isRequested() );
    SW_EXPECT_TRUE( quit.getExitCode() == ProcessExitCode::DeviceLost );
}

/** @brief [DeviceChangeSequenceTest] 요청한 백엔드를 못 만들어 앞 백엔드로 되돌렸으면 모듈은 다시 세우고 실패로 답하되 끝내지 않는다(개발 구성의 백엔드 교체 실패) */
SW_TEST_CASE( DeviceChangeSequenceTest, RestoredBackendRecreatesModulesWithoutQuitting )
{
    using Util = TestDeviceChangeSequenceInternal;
    SW_EXPECT_LOG( "DeviceChangeSequence.RestoredPreviousBackend" );
    vector<const utf8*>        events;
    FakeDeviceRenderDriver     driver{ events, true, BackendChangeResult::Restored };
    QuitRequest                quit;
    const DeviceChangeSequence sequence = Util::makeRecordingSequence( events, true );
    SW_EXPECT_FALSE( sequence.apply( driver, quit ) );
    SW_EXPECT_TRUE( Util::isSameOrder( events, { Util::kSuspendEvent, Util::kDeviceEvent, Util::kRecreateEvent } ) );
    SW_EXPECT_FALSE( quit.isRequested() );
}
