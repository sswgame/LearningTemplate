// FixedStepGroupTest — 물리 고정 스텝 훅이 틱 시스템의 고정 스텝 그룹으로 도는지 본다: 한 프레임 여러 스텝의 그룹 차례 · 스텝 수만큼 부름 · 단계 차례
// (바디 → 캐릭터) · 도는 중 켜고 끄기 · 0 스텝 프레임. 기록 컴포넌트(`FixedStepRecorderTestComponent.h`)는 onRegister 에서 명시 등록한 메서드로 불린다.
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Tick/FixedStepContext.h"
#include "Engine/Object/GameObject/Tick/FixedStepTickList.h"
#include "Engine/Physics/Scene/ScenePhysics.h"
#include "Engine/Physics/Scene/Tests/FixedStepRecorderTestComponent.h"

using namespace sw;

namespace
{
    /** @brief 바디 단계 기록 하나와 캐릭터 단계 기록 하나를 둔 씬입니다. 캐릭터 쪽을 먼저 만들어 단계 차례가 만든 차례가 아님을 본다. */
    struct FixedStepRig
    {
        GameObjectManager                   _manager;
        vector<FixedStepCallRecord>         _listLog;
        FixedStepLateRecorderTestComponent* _pLate{ nullptr };
        FixedStepRecorderTestComponent*     _pBody{ nullptr };

        FixedStepRig()
        {
            _pLate = _manager.createGameObject( hashed_string( "Late" ) )->addComponent<FixedStepLateRecorderTestComponent>();
            _pBody = _manager.createGameObject( hashed_string( "Body" ) )->addComponent<FixedStepRecorderTestComponent>();
            SW_REQUIRE_NOT_NULL( _pLate );
            SW_REQUIRE_NOT_NULL( _pBody );
            _pLate->_pLog = &_listLog;
            _pBody->_pLog = &_listLog;
        }

        ScenePhysics& getPhysics() { return _manager.getSubsystem<ScenePhysics>(); }

        /** @brief 0 스텝 프레임을 한 번 돌아 누적기를 설정에 맞추고 기록을 비운 뒤, 고정 스텝 길이를 돌려줍니다. */
        float32 configure()
        {
            getPhysics().step( GameSeconds{ 0.0f } );
            _listLog.clear();
            return getPhysics().getStepTimer().getStep().count();
        }

        /** @brief @p group 기록 수입니다(@p pComponent 가 있으면 그 구성원만). */
        uint32 count( FixedStepGroup group, const Component* pComponent = nullptr ) const
        {
            uint32 result = 0;
            for ( const FixedStepCallRecord& record : _listLog )
            {
                if ( record._group == group && ( pComponent == nullptr || record._pComponent == pComponent ) )
                    ++result;
            }
            return result;
        }
    };
} // namespace

/**
 * @brief [FixedStepGroupTest] 한 프레임 여러 스텝 — FrameBegin, (PreStep → PostStep) × 스텝 수, FrameEnd 차례이고, 그룹마다 바디 단계가 캐릭터 단계보다 먼저다
 */
SW_TEST_CASE( FixedStepGroupTest, GroupsRunInOrderOncePerStep )
{
    FixedStepRig  rig;
    const float32 step = rig.configure();
    SW_REQUIRE_TRUE( step > 0.0f );
    rig.getPhysics().step( GameSeconds{ step * 2.5f } );

    // 기록은 (그룹, 바디, 캐릭터) 쌍으로 온다 — 그룹마다 두 구성원이 단계 차례로 한 번씩.
    SW_REQUIRE_TRUE( rig._listLog.size() >= 4 && rig._listLog.size() % 2 == 0 );
    const uint32 stepCount = rig.count( FixedStepGroup::PreStep, rig._pBody );
    SW_REQUIRE_TRUE( stepCount >= 2 ); // 프레임당 상한이 두 스텝은 넘긴다(엔진 설정)
    SW_REQUIRE_EQUAL( size_t{ 2 * ( 2 + 2 * stepCount ) }, rig._listLog.size() );
    for ( size_t index = 0; index < rig._listLog.size(); index += 2 )
    {
        SW_EXPECT_TRUE( rig._listLog[index]._pComponent == rig._pBody );
        SW_EXPECT_TRUE( rig._listLog[index + 1]._pComponent == rig._pLate );
        SW_EXPECT_TRUE( rig._listLog[index]._group == rig._listLog[index + 1]._group );
    }
    SW_EXPECT_TRUE( rig._listLog.front()._group == FixedStepGroup::FrameBegin );
    SW_EXPECT_TRUE( rig._listLog.back()._group == FixedStepGroup::FrameEnd );
    for ( uint32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
    {
        const FixedStepCallRecord& pre  = rig._listLog[2 + 4 * stepIndex];
        const FixedStepCallRecord& post = rig._listLog[4 + 4 * stepIndex];
        SW_EXPECT_TRUE( pre._group == FixedStepGroup::PreStep );
        SW_EXPECT_TRUE( post._group == FixedStepGroup::PostStep );
        SW_EXPECT_EQUAL( stepIndex, pre._stepIndex );
        SW_EXPECT_EQUAL( stepIndex, post._stepIndex );
        SW_EXPECT_EQUAL( stepCount, pre._stepCount );
    }
    // 프레임 끝은 남은 시간의 보간 비를 받는다(스텝 길이의 반이 남았다).
    SW_EXPECT_TRUE( rig._listLog.back()._alpha > 0.0f && rig._listLog.back()._alpha < 1.0f );
}

/**
 * @brief [FixedStepGroupTest] 0 스텝 프레임 — 스텝 그룹은 돌지 않고 프레임 시작 · 끝만 한 번씩 돈다
 */
SW_TEST_CASE( FixedStepGroupTest, ZeroStepFrameRunsOnlyFrameGroups )
{
    FixedStepRig  rig;
    const float32 step = rig.configure();
    rig.getPhysics().step( GameSeconds{ step * 0.25f } );
    SW_EXPECT_EQUAL( 2u, rig.count( FixedStepGroup::FrameBegin ) );
    SW_EXPECT_EQUAL( 0u, rig.count( FixedStepGroup::PreStep ) );
    SW_EXPECT_EQUAL( 0u, rig.count( FixedStepGroup::PostStep ) );
    SW_EXPECT_EQUAL( 2u, rig.count( FixedStepGroup::FrameEnd ) );
}

/**
 * @brief [FixedStepGroupTest] 도는 중 켜고 끄기 — 스텝 앞 그룹에서 끈 구성원은 그 그룹의 이번 구간을 끝까지 돌고 다음 그룹부터 빠지며, 다시 켜면 다음 그룹부터 돈다
 * @details 0 번 스텝 PreStep 에서 캐릭터 쪽을 끄고(같은 구간 — 이번 PreStep 은 불린다) 1 번 스텝 PreStep 에서 켠다(같은 구간 — 이번 PreStep 은 빠진다).
 *          값으로 다루므로 끄고 곧바로 켜도 마지막 값이 이긴다.
 */
SW_TEST_CASE( FixedStepGroupTest, ToggleDuringRunAppliesFromNextGroup )
{
    FixedStepRig  rig;
    const float32 step         = rig.configure();
    rig._pBody->_pToggleTarget = rig._pLate;
    rig._pBody->_toggleFrame   = rig._pBody->_frameIndex; // 다음 물리 프레임
    rig.getPhysics().step( GameSeconds{ step * 3.5f } );
    const uint32 stepCount = rig.count( FixedStepGroup::PreStep, rig._pBody );
    SW_REQUIRE_TRUE( stepCount >= 3 );

    // 0 번 PreStep(불림) → 0 번 PostStep(빠짐) → 1 번 PreStep(빠짐 — 켜기는 다음 그룹부터) → 1 번 PostStep 부터 다시.
    SW_EXPECT_EQUAL( stepCount - 1, rig.count( FixedStepGroup::PreStep, rig._pLate ) );
    SW_EXPECT_EQUAL( stepCount - 1, rig.count( FixedStepGroup::PostStep, rig._pLate ) );
    SW_EXPECT_EQUAL( 1u, rig.count( FixedStepGroup::FrameEnd, rig._pLate ) );
    SW_EXPECT_TRUE( rig._manager.getFixedStepTickList().isEnabled( *rig._pLate ) );

    // 끄고 같은 틈에 다시 켜면 켜진 채다(늦은 비교 결함 없음).
    FixedStepTickList& list = rig._manager.getFixedStepTickList();
    SW_EXPECT_TRUE( list.setEnabled( *rig._pLate, false ) );
    SW_EXPECT_TRUE( list.setEnabled( *rig._pLate, true ) );
    rig._pBody->_pToggleTarget = nullptr;
    rig._listLog.clear();
    rig.getPhysics().step( GameSeconds{ step * 0.25f } );
    SW_EXPECT_EQUAL( 1u, rig.count( FixedStepGroup::FrameBegin, rig._pLate ) );
}

/**
 * @brief [FixedStepGroupTest] 구성원은 등록 범위다 — 오브젝트를 지우면 목록에서 빠지고, 꺼진 오브젝트(계층 비활성)도 빼지 않는다(물리는 꺼진 것의 바디를 프레임 시작에서 놓는다)
 */
SW_TEST_CASE( FixedStepGroupTest, MembershipFollowsRegistration )
{
    FixedStepRig       rig;
    FixedStepTickList& list = rig._manager.getFixedStepTickList();
    SW_EXPECT_EQUAL( 2u, list.getMemberCount() );
    rig._pLate->getOwner()->setActive( false );
    (void)rig.configure();
    rig.getPhysics().step( GameSeconds{ 0.0f } );
    SW_EXPECT_EQUAL( 1u, rig.count( FixedStepGroup::FrameBegin, rig._pLate ) );

    rig._manager.destroyGameObject( rig._pBody->getOwner() );
    rig._manager.update( 1.0f / 60.0f ); // 지연 파괴가 해제(`onUnregister`)에서 목록에서 뺀다
    rig._manager.update( 1.0f / 60.0f );
    SW_EXPECT_EQUAL( 1u, list.getMemberCount() );
}
