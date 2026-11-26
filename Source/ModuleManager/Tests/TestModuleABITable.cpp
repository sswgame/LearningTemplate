// ModuleABITableTest — 모듈 C-ABI 표의 구조체 멤버가 X 매크로 목록 그대로인지(nogpu). 판정은 컴파일 때(static_assert) 끝난다. 표가 바뀌었는지의 대조는 엔진 ABI 스탬프가 한다.
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "ModuleAPI/ABI/EditorAPI.h"
#include "ModuleAPI/ABI/GameAPI.h"

using namespace sw;

namespace
{
#define SW_TEST_ABI_COUNT_ENTRY( ReturnType, Name, Params ) +1
    constexpr size_t kGameEntryCount   = 0 SW_GAME_API_ENTRIES( SW_TEST_ABI_COUNT_ENTRY );
    constexpr size_t kEditorEntryCount = 0 SW_EDITOR_API_ENTRIES( SW_TEST_ABI_COUNT_ENTRY );
#undef SW_TEST_ABI_COUNT_ENTRY
    static_assert( sizeof( GameAPI ) == sizeof( void* ) * kGameEntryCount, "GameAPI holds exactly the listed entries" );
    static_assert( sizeof( EditorAPI ) == sizeof( void* ) * kEditorEntryCount, "EditorAPI holds exactly the listed entries" );
} // namespace

/** @brief [ModuleABITableTest] 표 구조체가 목록 항목만 든다 — 판정은 위 static_assert, 여기서는 실행 기록에 스위트가 보이게 한다 */
SW_TEST_CASE( ModuleABITableTest, TablesHoldExactlyTheListedEntries )
{
    SW_REQUIRE_TRUE( kGameEntryCount > 0 && kEditorEntryCount > 0 );
}
