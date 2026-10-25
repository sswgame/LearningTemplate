#include "pch.h"

#include "Core/Subsystem/SubsystemScope.h"

#include "Editor/Common/Workspace/EditorExtensionSubsystems.h"

#include "EditorTest/Common/Workspace/EditorExtensionRegistryTestUtil.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief 확장이 기여하는 서브시스템 자리입니다. 초기화 · 종료 수를 셉니다. */
    class ProbeExtensionSubsystem final : public EditorSubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "ProbeExtensionSubsystem";
        static inline int32          s_liveCount{ 0 };

        const utf8*         getSubsystemName() const override { return kSubsystemName; }
        SubsystemInitResult initializeSubsystem() override
        {
            ++s_liveCount;
            return SubsystemInitResult::Succeeded;
        }
        void shutdownSubsystem() override { --s_liveCount; }
    };

    using ProbeRegistrar = EditorExtensionRegistrar<EditorSubsystemRegistration>;

    EditorSubsystemRegistration makeProbeRegistration()
    {
        return EditorSubsystemRegistration{
            { ProbeExtensionSubsystem::kSubsystemName, 0 },
            &EditorSubsystemRegistration::create<ProbeExtensionSubsystem>,
            &EditorSubsystemRegistration::bind<ProbeExtensionSubsystem>,
            &EditorSubsystemRegistration::unbind<ProbeExtensionSubsystem>
        };
    }
} // namespace

/**
 * @brief [EditorExtensionSubsystemsTest] 등록 줄이 생기면 만들어 초기화하고 범위에 걸며, 그 모듈이 내려가기 전에 범위에서 풀고 종료한다
 * @details 확장 모듈(에디터 플러그인)의 함수 정적 싱글턴을 대신한다 — 수명이 에디터 컨텍스트와 모듈 둘 중 짧은 쪽에 묶인다.
 */
SW_TEST_CASE( EditorExtensionSubsystemsTest, ContributedSubsystemFollowsTheModule )
{
    const ScopedEmptyEditorExtensionRegistry<EditorSubsystemRegistration> emptyRegistry; // 링크된 코어 파일의 서브시스템 등록을 가린다
    SubsystemScope                                                        scope;
    EditorExtensionSubsystems                                             extensions( scope );
    ProbeExtensionSubsystem::s_liveCount = 0;
    {
        const ProbeRegistrar registrar{ makeProbeRegistration() };
        extensions.syncWithRegistry();
        SW_EXPECT_EQUAL( 1, ProbeExtensionSubsystem::s_liveCount );
        SW_EXPECT_TRUE( scope.find<ProbeExtensionSubsystem>() != nullptr );
        extensions.syncWithRegistry(); // 세대가 같으면 다시 만들지 않는다
        SW_EXPECT_EQUAL( 1u, extensions.getSubsystemCount() );

        // 다른 모듈의 언로드는 건드리지 않는다.
        const int32 otherImage[2]{};
        SW_EXPECT_EQUAL( 0u, extensions.releaseSubsystemsWithin( &otherImage[0], &otherImage[2] ) );
        // 등록 줄이 든 이미지가 내려간다.
        const void* pBegin = &registrar;
        const void* pEnd   = &registrar + 1;
        SW_EXPECT_EQUAL( 1u, extensions.releaseSubsystemsWithin( pBegin, pEnd ) );
        SW_EXPECT_EQUAL( 0, ProbeExtensionSubsystem::s_liveCount );
        SW_EXPECT_TRUE( scope.find<ProbeExtensionSubsystem>() == nullptr );
    }
    // 등록 줄이 빠졌으니 다시 맞춰도 만들지 않는다.
    extensions.syncWithRegistry();
    SW_EXPECT_EQUAL( 0u, extensions.getSubsystemCount() );
}

/**
 * @brief [EditorExtensionSubsystemsTest] 모두 내린 뒤 다시 맞추면 다시 만든다(에디터 컨텍스트를 다시 세운 자리)
 */
SW_TEST_CASE( EditorExtensionSubsystemsTest, ReleaseAllThenSyncRecreates )
{
    const ScopedEmptyEditorExtensionRegistry<EditorSubsystemRegistration> emptyRegistry; // 링크된 코어 파일의 서브시스템 등록을 가린다
    SubsystemScope                                                        scope;
    EditorExtensionSubsystems                                             extensions( scope );
    ProbeExtensionSubsystem::s_liveCount = 0;
    const ProbeRegistrar registrar{ makeProbeRegistration() };
    extensions.syncWithRegistry();
    extensions.releaseAll();
    SW_EXPECT_EQUAL( 0, ProbeExtensionSubsystem::s_liveCount );
    SW_EXPECT_TRUE( scope.find<ProbeExtensionSubsystem>() == nullptr );
    extensions.syncWithRegistry();
    SW_EXPECT_EQUAL( 1, ProbeExtensionSubsystem::s_liveCount );
    extensions.releaseAll();
}
