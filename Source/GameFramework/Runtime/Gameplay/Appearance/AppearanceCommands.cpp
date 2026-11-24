/**
 * @file AppearanceCommands.cpp
 * @brief 외형의 개발 명령(`appearance.save` · `appearance.load` — 공유 코드를 파일로)입니다.
 * @details 나머지 꾸미기는 컴포넌트의 `FUNCTION( Exec )` 다(`exec <오브젝트> setPreset · setSlotItem · setSlotHidden · applySliderValue · applyColorValue · applyOptionValue`).
 *          이 둘은 디스크 경로를 받아 파일을 쓰고 읽으므로 컴포넌트 메서드가 아니라 개발 명령이다 — 배포 구성은 등록하지 않는다(`SW_DEV_COMMAND`).
 *          대상은 활성 씬의 오브젝트 이름이다(`combat.hit` 와 같다).
 */
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceDatabase.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceSelection.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterAppearanceComponent.h"

namespace sw
{
    namespace
    {
        struct AppearanceCommandsInternal
        {
            /** @brief 활성 씬에서 이름 @p objectName 의 외형 컴포넌트입니다. 없으면 까닭을 @p outReply 에 적고 nullptr 입니다. */
            static CharacterAppearanceComponent* findAppearance( const string& objectName, string& outReply )
            {
                GameObject*                   pObject     = engine::findActiveObject( hashed_string( objectName ) );
                CharacterAppearanceComponent* pAppearance = pObject != nullptr ? pObject->getComponent<CharacterAppearanceComponent>() : nullptr;
                if ( pAppearance == nullptr )
                {
                    StringBuilder<constant::buffer::kMax256> replyBuilder;
                    replyBuilder.appendFormat( "no object named '%#' with a CharacterAppearanceComponent in the active scene", objectName );
                    outReply = string( replyBuilder.view() );
                }
                return pAppearance;
            }

            /** @brief 프리셋 길에서 아직 조립하지 않은 외형을 편집하려 할 때의 답입니다 — 담을 선택이 없다(`captureSelection`). */
            static constexpr const utf8* kNotDressedReply = "the appearance is not dressed yet - wait for Appearance.Idle before editing";

            /** @brief `appearance.save <object> <file>` — 지금 모습을 공유 코드(플레이어 프리셋 저장소의 형식)로 절대 경로(시나리오의 `{out}/…`)에 쓴다. */
            static bool runSave( const vector<string>& listArgument, string& outReply )
            {
                AbsolutePath path;
                if ( listArgument.size() != 2 || DevCommandRegistry::readDiskPathArgument( listArgument[1], path, outReply ) == false )
                    return false;
                CharacterAppearanceComponent* pAppearance = findAppearance( listArgument[0], outReply );
                const AppearanceDatabase*     pDatabase   = pAppearance != nullptr ? pAppearance->findDatabase() : nullptr;
                AppearanceSelection           selection;
                if ( pAppearance == nullptr )
                    return false;
                pAppearance->captureSelection( selection );
                if ( selection._basePresetID.empty() )
                {
                    outReply = kNotDressedReply;
                    return false;
                }
                if ( pDatabase == nullptr )
                {
                    outReply = "no AppearanceDatabase game service";
                    return false;
                }
                if ( FileUtil::writeTextFile( path, AppearanceSelection::encode( selection, *pDatabase ) ) == false )
                {
                    StringBuilder<constant::buffer::kMax256> replyBuilder;
                    replyBuilder.appendFormat( "could not write %#", path.str() );
                    outReply = string( replyBuilder.view() );
                    return false;
                }
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "saved %#", path.str() );
                outReply = string( replyBuilder.view() );
                return true;
            }

            /** @brief `appearance.load <object> <file>` — 공유 코드를 읽어 게임 선택으로 입힌다(저장한 프리셋 불러오기). */
            static bool runLoad( const vector<string>& listArgument, string& outReply )
            {
                AbsolutePath path;
                if ( listArgument.size() != 2 || DevCommandRegistry::readDiskPathArgument( listArgument[1], path, outReply ) == false )
                    return false;
                CharacterAppearanceComponent* pAppearance = findAppearance( listArgument[0], outReply );
                const AppearanceDatabase*     pDatabase   = pAppearance != nullptr ? pAppearance->findDatabase() : nullptr;
                string                        code;
                if ( pAppearance == nullptr )
                    return false;
                if ( pDatabase == nullptr || FileUtil::readTextFile( path, code ) == false )
                {
                    StringBuilder<constant::buffer::kMax256> replyBuilder;
                    if ( pDatabase == nullptr )
                        replyBuilder.append( "no AppearanceDatabase game service" );
                    else
                        replyBuilder.appendFormat( "could not read %#", path.str() );
                    outReply = string( replyBuilder.view() );
                    return false;
                }
                AppearanceSelection  selection;
                const expected<void> decoded = AppearanceSelection::decode( StringUtil::trim( code ), *pDatabase, selection );
                if ( decoded.has_value() == false )
                {
                    StringBuilder<constant::buffer::kMax256> replyBuilder;
                    replyBuilder.appendFormat( "bad appearance code in %#: %#", path.str(), decoded.error().message() );
                    outReply = string( replyBuilder.view() );
                    return false;
                }
                pAppearance->applySelection( selection );
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "loaded %#", path.str() );
                outReply = string( replyBuilder.view() );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( AppearanceSave, "appearance.save", "appearance.save <object> <file>", "Write a character's current appearance as a share code to a file",
                    &AppearanceCommandsInternal::runSave );
    SW_DEV_COMMAND( AppearanceLoad, "appearance.load", "appearance.load <object> <file>", "Dress a character with the share code in a file", &AppearanceCommandsInternal::runLoad );
} // namespace sw
