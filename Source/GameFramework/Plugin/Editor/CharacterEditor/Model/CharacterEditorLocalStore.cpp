#include "pch.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/Model/CharacterEditorLocalStore.h"

#include "Core/Foundation/Constants.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Workspace/EditorExtensionSubsystems.h"
#include "Editor/Common/Workspace/EditorFrameStep.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Config/GameSelection.h"

#include "GameFramework/Runtime/Gameplay/Appearance/UserAppearancePresetStore.h"

namespace sw::editor
{
    namespace
    {
        struct CharacterEditorLocalStoreInternal
        {
            static constexpr const utf8* kThumbnailSlotPrefix = "charactereditor/thumb.";

            /** @brief 에셋 감시 다음 · 패널 앞 - 패널이 이번 프레임에 끝난 읽기 결과를 그린다. */
            static void pollCompletions( EditorContext& context )
            {
                (void)context;
                CharacterEditorLocalStore* const pStore = editor::findSubsystem<CharacterEditorLocalStore>();
                if ( pStore != nullptr )
                    pStore->getChannel().update();
            }
        };
    } // namespace

    SubsystemInitResult CharacterEditorLocalStore::initializeSubsystem()
    {
        if ( const expected<void> opened = _channel.open( GameSelection::getName(), *LocalStoreLocation::tryUnderUserData( kDirectory ), nullptr ); opened.has_value() == false ) // 자리는 상수라 늘 풀린다
        {
            SW_LOG_ERROR_ID( "CharacterEditorLocalStore.CharacterEditorLocalStore", "Character editor local store could not open: %# (%#)", opened.error().message(), opened.error().detail() );
            return SubsystemInitResult::Failed;
        }
        return SubsystemInitResult::Succeeded;
    }

    void CharacterEditorLocalStore::shutdownSubsystem()
    {
        _channel.flush();
        _channel.close();
    }

    string CharacterEditorLocalStore::makeThumbnailSlot( const UserAppearancePresetStore& store, string_view presetName )
    {
        const vector<UserAppearancePreset>& listPreset = store.getPresets();
        const UserAppearancePreset*         pPreset    = store.findPresetByName( presetName );
        if ( pPreset != nullptr && pPreset->_thumbnailSlot.empty() == false )
            return pPreset->_thumbnailSlot;
        // 프리셋 수 + 1 개의 번호 가운데 하나는 반드시 비어 있다.
        for ( uint32 number = 0;; ++number )
        {
            fixed_string<constant::buffer::kMax64> slot;
            formatstring( slot.data(), slot.capacity(), "%#%#", CharacterEditorLocalStoreInternal::kThumbnailSlotPrefix, number );
            bool bTaken = false;
            for ( const UserAppearancePreset& preset : listPreset )
                bTaken = bTaken || string_view( preset._thumbnailSlot.c_str() ) == string_view( slot.c_str() );
            if ( bTaken == false )
                return string{ slot.c_str() };
        }
    }

    SW_EDITOR_SUBSYSTEM( CharacterEditorLocalStore, 300 );
    SW_EDITOR_FRAME_STEP( CharacterEditorLocalStore, "charactereditor.localStore", EditorFrameStepOrder::kAssetReload + 50, &CharacterEditorLocalStoreInternal::pollCompletions );
} // namespace sw::editor
