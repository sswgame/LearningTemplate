// 에디터 상태 저장소 — 값은 타입마다 하나씩 처음 물을 때 생기고, resetAll 은 값을 처음으로 되돌리되 칸과 주소는 그대로 둔다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Workspace/EditorStateStore.h"

namespace
{
    struct EditorStateStoreTestInternal
    {
        struct CounterState
        {
            uint32 _count{ 0 };
        };

        struct NameState
        {
            sw::string _name{};
        };
    };
} // namespace

/**
 * @brief [EditorStateStoreTest] 처음 물으면 값이 하나 생기고 다시 물으면 같은 값이다. resetAll 뒤에도 칸 수와 주소는 그대로이고 값만 처음으로 돌아간다
 */
SW_TEST_CASE( EditorStateStoreTest, StatesAreMadeOnFirstAskAndResetInPlace )
{
    using Internal = EditorStateStoreTestInternal;

    sw::editor::EditorStateStore store;
    SW_EXPECT_EQUAL( store.getStateCount(), 0u );

    Internal::CounterState& counter = store.get<Internal::CounterState>();
    counter._count                  = 3;
    SW_EXPECT_EQUAL( store.getStateCount(), 1u );
    SW_EXPECT_TRUE( &store.get<Internal::CounterState>() == &counter );
    SW_EXPECT_EQUAL( store.getStateCount(), 1u );

    store.get<Internal::NameState>()._name = "kept";
    SW_EXPECT_EQUAL( store.getStateCount(), 2u );

    store.resetAll();
    SW_EXPECT_EQUAL( store.getStateCount(), 2u );
    SW_EXPECT_EQUAL( counter._count, 0u ); // 앞서 받은 참조로 읽는다 — 주소가 그대로다
    SW_EXPECT_TRUE( store.get<Internal::NameState>()._name.empty() );
}
