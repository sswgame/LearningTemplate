#include "pch.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/Model/CharacterEditorLocalStore.h"

#include "Core/File/FileUtil.h"
#include "Core/File/UserDataPath.h"

#include "Editor/Common/Workspace/EditorExtensionSubsystems.h"
#include "Editor/Common/Workspace/EditorFrameStep.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Config/GameSelection.h"

namespace sw::editor
{
    SW_LOG_CALLER( "CharacterEditorLocalStore" );

    namespace
    {
        struct CharacterEditorLocalStoreInternal
        {
            static constexpr const utf8* kThumbnailSlotPrefix = "charactereditor/thumb.";

            /** @brief 에셋 감시 다음 · 패널 앞 - 패널이 이번 프레임에 끝난 읽기 결과를 그린다. */
            static void pumpCompletions( EditorContext& context )
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
        string error;
        if ( _channel.open( GameSelection::getName(), *LocalStoreLocation::tryUnderUserData( kDirectory ), nullptr, error ) == false ) // 자리는 상수라 늘 풀린다
        {
            SW_LOG_ERROR( "Character editor local store could not open: %#", error.c_str() );
            return SubsystemInitResult::Failed;
        }
        return SubsystemInitResult::Succeeded;
    }

    void CharacterEditorLocalStore::shutdownSubsystem()
    {
        _channel.drain();
        _channel.close();
    }

    string CharacterEditorLocalStore::makeThumbnailSlot( string_view presetName )
    {
        string slot{ CharacterEditorLocalStoreInternal::kThumbnailSlotPrefix };
        for ( const utf8 character : presetName )
        {
            if ( slot.size() >= static_cast<size_t>( ILocalStore::kMaxSlotNameSize ) )
                break;
            const utf8 lower    = ( 'A' <= character && character <= 'Z' ) ? static_cast<utf8>( character - 'A' + 'a' ) : character;
            const bool bAllowed = ( 'a' <= lower && lower <= 'z' ) || ( '0' <= lower && lower <= '9' ) || lower == '_' || lower == '-';
            slot.push_back( bAllowed ? lower : '_' );
        }
        return slot;
    }

    string CharacterEditorLocalStore::makeCaptureImagePath()
    {
        return FileUtil::joinPath( FileUtil::joinPath( UserDataPath::getDataDirectory( GameSelection::getName() ), kDirectory ), "thumbnail.capture.png" );
    }

    SW_EDITOR_SUBSYSTEM( CharacterEditorLocalStore, 300 );
    SW_EDITOR_FRAME_STEP( CharacterEditorLocalStore, "charactereditor.localStore", EditorFrameStepOrder::kAssetReload + 50, &CharacterEditorLocalStoreInternal::pumpCompletions );
} // namespace sw::editor
