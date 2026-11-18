#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/Model/CharacterEditorLocalStore.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/Local/LocalStore.h"
#include "GameFramework/Runtime/Gameplay/Appearance/UserAppearancePresetStore.h"

// CharacterEditorThumbnailSlotTest — 썸네일 슬롯은 이름에서 만들지 않고 번호로 준다: 대소문자 · 규칙 밖 글자만 다른 두 프리셋도 슬롯을 나누지 않고, 이미 든 슬롯은 그대로 쓴다.

using namespace sw;
using namespace sw::editor;

/** @brief [CharacterEditorThumbnailSlotTest] 이름이 겹쳐 보이는 두 프리셋(대소문자만 다르다)이 다른 슬롯을 받는다 */
SW_TEST_CASE( CharacterEditorThumbnailSlotTest, LookAlikeNamesGetDifferentSlots )
{
    UserAppearancePresetStore store;
    (void)store.savePreset( "Hero", AppearanceSelection{} );
    (void)store.savePreset( "hero", AppearanceSelection{} );

    const string first = CharacterEditorLocalStore::makeThumbnailSlot( store, "Hero" );
    SW_REQUIRE_TRUE( store.setThumbnailSlot( "Hero", first ) );
    const string second = CharacterEditorLocalStore::makeThumbnailSlot( store, "hero" );
    SW_EXPECT_FALSE( first == second );
    SW_EXPECT_TRUE( ILocalStore::isValidSlotName( first ) );
    SW_EXPECT_TRUE( ILocalStore::isValidSlotName( second ) );
}

/** @brief [CharacterEditorThumbnailSlotTest] 이미 슬롯을 든 프리셋은 같은 슬롯을 다시 받는다(새 썸네일이 앞 것을 덮는다) */
SW_TEST_CASE( CharacterEditorThumbnailSlotTest, PresetKeepsItsSlot )
{
    UserAppearancePresetStore store;
    (void)store.savePreset( "Hero", AppearanceSelection{} );
    const string slot = CharacterEditorLocalStore::makeThumbnailSlot( store, "Hero" );
    SW_REQUIRE_TRUE( store.setThumbnailSlot( "Hero", slot ) );
    SW_EXPECT_TRUE( slot == CharacterEditorLocalStore::makeThumbnailSlot( store, "Hero" ) );
}
