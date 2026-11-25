// ModuleABIStampTest — 모듈 C-ABI 표의 스탬프가 표 선언 글에서 저절로 따라오는지(nogpu). 판정은 대부분 컴파일 때(static_assert) 끝난다.
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "ModuleAPI/ABI/EditorAPI.h"
#include "ModuleAPI/ABI/GameAPI.h"
#include "ModuleAPI/ABI/ModuleABI.h"

using namespace sw;

namespace
{
    // 같은 표의 두 판 — 가운데에 항목 하나를 끼운 판과 순서만 바꾼 판. 손 판 번호를 올리지 않아도 스탬프가 달라야 한다.
#define SW_TEST_ABI_ENTRIES_BASE( X ) \
    X( void, create, () )             \
    X( void, update, ( float32 deltaTime ) )
#define SW_TEST_ABI_ENTRIES_INSERTED( X ) \
    X( void, create, () )                 \
    X( void, shutdown, () )               \
    X( void, update, ( float32 deltaTime ) )
#define SW_TEST_ABI_ENTRIES_REORDERED( X )   \
    X( void, update, ( float32 deltaTime ) ) \
    X( void, create, () )
#define SW_TEST_ABI_ENTRIES_SIGNATURE_CHANGED( X ) \
    X( void, create, () )                          \
    X( void, update, ( float64 deltaTime ) )

    constexpr uint64 kBaseStamp             = ModuleABIUtil::computeStamp( SW_TEST_ABI_ENTRIES_BASE( SW_MODULE_ABI_ENTRY_TEXT ) );
    constexpr uint64 kInsertedStamp         = ModuleABIUtil::computeStamp( SW_TEST_ABI_ENTRIES_INSERTED( SW_MODULE_ABI_ENTRY_TEXT ) );
    constexpr uint64 kReorderedStamp        = ModuleABIUtil::computeStamp( SW_TEST_ABI_ENTRIES_REORDERED( SW_MODULE_ABI_ENTRY_TEXT ) );
    constexpr uint64 kSignatureChangedStamp = ModuleABIUtil::computeStamp( SW_TEST_ABI_ENTRIES_SIGNATURE_CHANGED( SW_MODULE_ABI_ENTRY_TEXT ) );

    static_assert( kBaseStamp != kInsertedStamp, "inserting an entry in the middle must change the stamp" );
    static_assert( kBaseStamp != kReorderedStamp, "reordering entries must change the stamp" );
    static_assert( kBaseStamp != kSignatureChangedStamp, "changing a signature must change the stamp" );
    static_assert( kBaseStamp == ModuleABIUtil::computeStamp( SW_TEST_ABI_ENTRIES_BASE( SW_MODULE_ABI_ENTRY_TEXT ) ), "the same list must give the same stamp" );

    // 실제 표: 게임 · 에디터 경계는 서로 다른 스탬프를 갖고, 구조체 멤버 수가 목록 항목 수와 같다(멤버와 스탬프가 한 목록에서 나온다).
    static_assert( kGameModuleABIStamp != kEditorModuleABIStamp, "game and editor tables must not share a stamp" );
#define SW_TEST_ABI_COUNT_ENTRY( ReturnType, Name, Params ) +1
    static_assert( sizeof( GameAPI ) == sizeof( void* ) * ( 0 SW_GAME_API_ENTRIES( SW_TEST_ABI_COUNT_ENTRY ) ), "GameAPI holds exactly the listed entries" );
    static_assert( sizeof( EditorAPI ) == sizeof( void* ) * ( 0 SW_EDITOR_API_ENTRIES( SW_TEST_ABI_COUNT_ENTRY ) ), "EditorAPI holds exactly the listed entries" );
#undef SW_TEST_ABI_COUNT_ENTRY
} // namespace

SW_TEST_CASE( ModuleABIStampTest, InsertedEntryChangesStamp )
{
    // 판정은 위 static_assert 가 한다. 여기서는 실행 기록에 스위트가 보이도록 값 하나를 다시 본다.
    SW_REQUIRE_TRUE( kBaseStamp != kInsertedStamp );
    SW_REQUIRE_TRUE( kGameModuleABIStamp != 0 && kEditorModuleABIStamp != 0 );
}
