// AnimationSkinSlotTest — 스킨 칸: 놓은 번호는 곧바로 다시 주되 세대가 올라, 같은 프레임에 같은 번호를 받은 새 주인이 옛 주인과 같은 칸으로 보이지 않는다.
// 예전에는 번호뿐이라 모프 풀(`GPUMeshMorphPool::update`)이 (메시 · 칸 · 단계)가 같다고 지난 벌을 믿어 새 캐릭터의 첫 프레임 모션 벡터가 옛 캐릭터의 결과로 섞였다. nogpu.
#include "pch.h"

#include "Core/Memory/Memory.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Animation/Scene/AnimationSkinSlot.h"

using namespace sw;

/**
 * @brief [AnimationSkinSlotTest] 놓은 번호를 다시 받은 칸은 번호는 같고 정체성(번호 + 세대)은 다르다
 * @details 변이: 놓을 때 세대를 올리지 않으면 두 열쇠가 같아 마지막 단언이 진다.
 */
SW_TEST_CASE( AnimationSkinSlotTest, ReusedSlotNumberIsNewIdentity )
{
    unique_ptr<AnimationSkinSlot> pOld     = make_unique<AnimationSkinSlot>();
    const AnimationSkinSlotKey    oldKey   = pOld->getKey();
    const uint32                  slotUsed = pOld->get();
    pOld.reset(); // 캐릭터가 사라진다

    const AnimationSkinSlot    reused{}; // 같은 프레임에 새 캐릭터가 온다 — 놓은 번호를 먼저 받는다
    const AnimationSkinSlotKey newKey = reused.getKey();
    SW_EXPECT_EQUAL( slotUsed, reused.get() );
    SW_EXPECT_EQUAL( slotUsed, newKey._slot );
    SW_EXPECT_TRUE( newKey != oldKey );
}
