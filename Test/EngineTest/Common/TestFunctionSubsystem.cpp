#include "pch.h"

#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"
#include "Core/Subsystem/SubsystemCollection.h"

#include "Engine/Common/FunctionSubsystem.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief 서브시스템이 불린 순서를 적습니다 — "I:" 초기화, "P:" 멈춤(stop), "S:" 종료, "D:" 소멸. */
    struct SubsystemRecorder
    {
        vector<string>      _listEvent{};
        string              _failName{}; ///< 이 이름의 초기화가 `_failResult` 를 돌려준다
        SubsystemInitResult _failResult{ SubsystemInitResult::Succeeded };

        /** @brief @p kind 로 시작하는 사건의 이름들을 공백으로 이어 붙입니다. */
        string joinEvents( utf8 kind ) const
        {
            StringBuilder<constant::kMaxBuffer512> sb;
            for ( const string& event : _listEvent )
            {
                if ( event[0] != kind )
                    continue;
                if ( sb.size() > 0 )
                    sb.append( ' ' );
                sb.append( string_view{ event }.substr( 2 ) );
            }
            return string( sb.c_str() );
        }
    };

    /** @brief 이름 · 의존 · 대상 · 태그를 받아 불린 순서를 적는 서브시스템입니다. 소멸자도 적는다(소멸 순서를 본다). */
    class RecordingSubsystem final : public ISubsystem
    {
    public:
        RecordingSubsystem( SubsystemRecorder& recorder, const utf8* pName, vector<const utf8*> listDependency )
            : _recorder{ recorder }
            , _pName{ pName }
            , _listDependency{ std::move( listDependency ) }
        {
        }
        ~RecordingSubsystem() override { _recorder._listEvent.push_back( string( "D:" ) + _pName ); }

        SubsystemInitResult initializeSubsystem() override
        {
            _recorder._listEvent.push_back( string( "I:" ) + _pName );
            return _recorder._failName == _pName ? _recorder._failResult : SubsystemInitResult::Succeeded;
        }
        void stopSubsystem() override { _recorder._listEvent.push_back( string( "P:" ) + _pName ); }
        void shutdownSubsystem() override { _recorder._listEvent.push_back( string( "S:" ) + _pName ); }

        const utf8*             getSubsystemName() const override { return _pName; }
        span<const utf8* const> getSubsystemDependencies() const override { return _listDependency; }

    private:
        SubsystemRecorder&  _recorder;
        const utf8*         _pName;
        vector<const utf8*> _listDependency;
    };

    /** @brief `FunctionSubsystem` 의 델리게이트 대상입니다. */
    struct FunctionTarget
    {
        SubsystemInitResult initialize()
        {
            ++_initializeCount;
            return SubsystemInitResult::Succeeded;
        }
        void shutdown() { ++_shutdownCount; }

        uint32 _initializeCount{ 0 };
        uint32 _shutdownCount{ 0 };
    };

    /** @brief 해제 · 소멸을 적는 객체입니다(`FunctionSubsystem::takeOwnership` 의 소멸 자리를 본다). */
    struct OwnedProbe
    {
        OwnedProbe( SubsystemRecorder& recorder, const utf8* pName )
            : _recorder{ recorder }
            , _pName{ pName }
        {
        }
        ~OwnedProbe() { _recorder._listEvent.push_back( string( "D:" ) + _pName ); }

        void release() { _recorder._listEvent.push_back( string( "R:" ) + _pName ); }

        SubsystemRecorder& _recorder;
        const utf8*        _pName;
    };
} // namespace

/**
 * @brief [FunctionSubsystemTest] `FunctionSubsystem` 은 묶은 델리게이트를 부르고, 묶지 않은 초기화는 성공 · 종료는 아무것도 하지 않는다
 * @details `FunctionSubsystem` 은 `stopSubsystem` 을 덮어쓰지 않는다 — 기본(아무것도 하지 않음)으로 두 패스를 지나도 종료는 한 번이다.
 */
SW_TEST_CASE( FunctionSubsystemTest, FunctionSubsystemCallsItsDelegates )
{
    FunctionTarget               target;
    SubsystemCollection          collection;
    static constexpr const utf8* kArrBoundDependency[] = { "Unbound" };
    collection.add( make_unique<FunctionSubsystem>( FunctionSubsystemDesc{ "Bound", kArrBoundDependency, SubsystemHostMask::All, MemoryTag::EngineMisc },
                                                    SW_DELEGATE_METHOD( SubsystemInitializeDelegate, &FunctionTarget::initialize, &target ),
                                                    SW_DELEGATE_METHOD( SubsystemShutdownDelegate, &FunctionTarget::shutdown, &target ) ) );
    collection.add( make_unique<FunctionSubsystem>( FunctionSubsystemDesc{ "Unbound", {}, SubsystemHostMask::All, MemoryTag::EngineMisc } ) );
    SW_ASSERT_TRUE_MSG( collection.initializeAll( SubsystemHostMask::All ), collection.getError().c_str() );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( collection.getInitializedIndices().size() ) );
    SW_EXPECT_EQUAL( 1u, target._initializeCount );
    collection.shutdownAll();
    SW_EXPECT_EQUAL( 1u, target._shutdownCount );
}

/**
 * @brief [FunctionSubsystemTest] `FunctionSubsystem` 이 넘겨받은 객체는 그 서브시스템의 소멸 자리(초기화의 역순)에서 함께 소멸한다
 * @details Core 서비스(태스크 · 코덱 레지스트리)의 소유를 기동 순서로 옮기는 근거다 — 그에 기대는 것이 모두 소멸한 뒤, 그것이 기대는 것보다 먼저다.
 */
SW_TEST_CASE( FunctionSubsystemTest, FunctionSubsystemDestroysWhatItOwnsInItsPlace )
{
    SubsystemRecorder   recorder;
    SubsystemCollection collection;
    collection.add( sw::make_unique<RecordingSubsystem>( recorder, "Later", vector<const utf8*>{ "Owner" } ) );
    static constexpr const utf8* kArrOwnerDependency[] = { "Earlier" };
    FunctionSubsystem&           owner                 = collection.add( make_unique<FunctionSubsystem>( FunctionSubsystemDesc{ "Owner", kArrOwnerDependency, SubsystemHostMask::All, MemoryTag::EngineMisc } ) );
    OwnedProbe&                  first                 = owner.takeOwnership( make_unique<OwnedProbe>( recorder, "First" ) );
    SW_EXPECT_TRUE( &first._recorder == &recorder );
    (void)owner.takeOwnership<&OwnedProbe::release>( make_unique<OwnedProbe>( recorder, "Second" ) );
    collection.add( sw::make_unique<RecordingSubsystem>( recorder, "Earlier", vector<const utf8*>{} ) );
    SW_ASSERT_TRUE_MSG( collection.initializeAll( SubsystemHostMask::All ), collection.getError().c_str() );

    // 여럿은 넘긴 역순으로, 해제 함수를 준 것은 그것을 부른 뒤 소멸한다.
    collection.destroyAll();
    SW_EXPECT_STREQ( "Later Second First Earlier", recorder.joinEvents( 'D' ).c_str() );
    SW_EXPECT_STREQ( "Second", recorder.joinEvents( 'R' ).c_str() );
}
