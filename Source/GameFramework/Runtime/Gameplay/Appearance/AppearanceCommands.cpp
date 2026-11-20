/**
 * @file AppearanceCommands.cpp
 * @brief 외형의 개발 명령(`appearance.preset` · `appearance.slot` · `appearance.value` · `appearance.hide` · `appearance.save` · `appearance.load`)입니다.
 * @details 게임 중 캐릭터 꾸미기(본 · 장비 · 염색 · 머리 모양 · 프리셋 저장)를 시나리오가 게임 길 그대로 부르는 입구다 — 컴포넌트의 공개 함수
 *          (`setPreset` · `setSlotItem` · `applySelection` · `setHiddenSlots`)와 공유 코드(`AppearanceSelection::encode` · `decode`)만 쓴다. 배포 구성은 등록하지 않는다(`SW_DEV_COMMAND`).
 *          대상은 활성 씬의 오브젝트 이름이다(`combat.hit` 와 같다).
 */
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceDatabase.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceSelection.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterAppearanceComponent.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CustomizationSchema.h"

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

            /** @brief `appearance.preset <object> <preset> [seed]` — 프리셋으로 다시 입힌다(선택 길을 푼다). */
            static bool runPreset( const vector<string>& listArgument, string& outReply )
            {
                uint64 seed = 0;
                if ( listArgument.size() != 2 && listArgument.size() != 3 )
                    return false;
                if ( listArgument.size() == 3 && ( StringUtil::parseUint64( listArgument[2], seed ) == false || seed > MathUtil::kMaxUInt32 ) )
                    return false;
                CharacterAppearanceComponent* pAppearance = findAppearance( listArgument[0], outReply );
                if ( pAppearance == nullptr )
                    return false;
                pAppearance->setPreset( hashed_string( listArgument[1] ), static_cast<uint32>( seed ) );
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "preset %#", listArgument[1] );
                outReply = string( replyBuilder.view() );
                return true;
            }

            /** @brief `appearance.slot <object> <slot> [item]` — 칸의 아이템을 바꾼다. 아이템을 빼면 칸을 비운다. */
            static bool runSlot( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 2 && listArgument.size() != 3 )
                    return false;
                CharacterAppearanceComponent* pAppearance = findAppearance( listArgument[0], outReply );
                if ( pAppearance == nullptr )
                    return false;
                const string item = listArgument.size() == 3 ? listArgument[2] : string{};
                pAppearance->setSlotItem( hashed_string( listArgument[1] ), hashed_string( item ) );
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "%# = %#", listArgument[1], item.empty() ? string_view( "(empty)" ) : string_view( item ) );
                outReply = string( replyBuilder.view() );
                return true;
            }

            /**
             * @brief `appearance.value <object> <parameter> <value…>` — 꾸미기 값 하나를 바꿔 게임 선택으로 입힌다(캐릭터 생성기의 슬라이더 · 색 · 고르기).
             * @details 값의 종류는 선택의 스키마가 정한다: 슬라이더는 수 하나, 색은 `r g b [a]`, 고르기 · 부착은 항목 이름. 대칭 짝도 함께 바뀐다(편집 창과 같다).
             */
            static bool runValue( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() < 3 )
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
                const CustomizationSchemaDef* pSchema = pDatabase != nullptr ? pDatabase->getSchemas().findSchema( selection._schema ) : nullptr;
                const CustomizationParamDef*  pParam  = pSchema != nullptr ? pSchema->findParameter( hashed_string( listArgument[1] ) ) : nullptr;
                if ( pParam == nullptr )
                {
                    StringBuilder<constant::buffer::kMax256> replyBuilder;
                    replyBuilder.appendFormat( "schema '%#' has no parameter '%#'", selection._schema.c_str(), listArgument[1] );
                    outReply = string( replyBuilder.view() );
                    return false;
                }
                CustomizationValue value;
                value._parameter       = pParam->_name;
                const size_t valueSize = listArgument.size() - 2;
                bool         bRead     = false;
                switch ( pParam->_kind )
                {
                    case CustomizationKind::Slider:
                        bRead = valueSize == 1 && StringUtil::parseFloat( listArgument[2], value._number._x );
                        break;
                    case CustomizationKind::Color:
                        value._number._w = 1.0f;
                        bRead            = ( valueSize == 3 || valueSize == 4 ) && StringUtil::parseFloat( listArgument[2], value._number._x ) &&
                                StringUtil::parseFloat( listArgument[3], value._number._y ) && StringUtil::parseFloat( listArgument[4], value._number._z ) &&
                                ( valueSize == 3 || StringUtil::parseFloat( listArgument[5], value._number._w ) );
                        break;
                    case CustomizationKind::Choice:
                    case CustomizationKind::Attachment:
                        value._option = hashed_string( listArgument[2] );
                        bRead         = valueSize == 1 && pParam->findOption( value._option ) != nullptr;
                        break;
                }
                if ( bRead == false )
                {
                    StringBuilder<constant::buffer::kMax256> replyBuilder;
                    replyBuilder.appendFormat( "parameter '%#' does not take that value (slider: one number, color: r g b [a], choice: an option name)", listArgument[1] );
                    outReply = string( replyBuilder.view() );
                    return false;
                }
                if ( pSchema->applyValue( value, true, selection._customization ) == false )
                    return false;
                pAppearance->applySelection( selection );
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "%# set", listArgument[1] );
                outReply = string( replyBuilder.view() );
                return true;
            }

            /** @brief `appearance.hide <object> [slot…]` — 이 칸들의 부품을 숨긴다(1인칭 머리 · 무기). 칸이 없으면 모두 보인다. */
            static bool runHide( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() )
                    return false;
                CharacterAppearanceComponent* pAppearance = findAppearance( listArgument[0], outReply );
                if ( pAppearance == nullptr )
                    return false;
                vector<hashed_string> listSlot;
                for ( size_t index = 1; index < listArgument.size(); ++index )
                {
                    listSlot.push_back( hashed_string( listArgument[index] ) );
                }
                pAppearance->setHiddenSlots( listSlot );
                outReply = listSlot.empty() ? string( "every slot shown" ) : string( "slots hidden" );
                return true;
            }

            /** @brief `appearance.save <object> <file>` — 지금 모습을 공유 코드(플레이어 프리셋 저장소의 형식)로 절대 경로(시나리오의 `{out}/…`)에 쓴다. */
            static bool runSave( const vector<string>& listArgument, string& outReply )
            {
                string path;
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
                    replyBuilder.appendFormat( "could not write %#", path );
                    outReply = string( replyBuilder.view() );
                    return false;
                }
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "saved %#", path );
                outReply = string( replyBuilder.view() );
                return true;
            }

            /** @brief `appearance.load <object> <file>` — 공유 코드를 읽어 게임 선택으로 입힌다(저장한 프리셋 불러오기). */
            static bool runLoad( const vector<string>& listArgument, string& outReply )
            {
                string path;
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
                        replyBuilder.appendFormat( "could not read %#", path );
                    outReply = string( replyBuilder.view() );
                    return false;
                }
                AppearanceSelection  selection;
                const expected<void> decoded = AppearanceSelection::decode( StringUtil::trim( code ), *pDatabase, selection );
                if ( decoded.has_value() == false )
                {
                    StringBuilder<constant::buffer::kMax256> replyBuilder;
                    replyBuilder.appendFormat( "bad appearance code in %#: %#", path, decoded.error().message() );
                    outReply = string( replyBuilder.view() );
                    return false;
                }
                pAppearance->applySelection( selection );
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "loaded %#", path );
                outReply = string( replyBuilder.view() );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( AppearancePreset, "appearance.preset", "appearance.preset <object> <preset> [seed]", "Dress a named character with an appearance preset (drops a selection it wore)",
                    &AppearanceCommandsInternal::runPreset );
    SW_DEV_COMMAND( AppearanceSlot, "appearance.slot", "appearance.slot <object> <slot> [item]", "Put an item in a character's equipment slot (no item empties the slot)",
                    &AppearanceCommandsInternal::runSlot );
    SW_DEV_COMMAND( AppearanceValue, "appearance.value", "appearance.value <object> <parameter> <number | r g b [a] | option>",
                    "Change one customization value (slider, dye colour, hair style) of a character as a game selection", &AppearanceCommandsInternal::runValue );
    SW_DEV_COMMAND( AppearanceHide, "appearance.hide", "appearance.hide <object> [slot...]", "Hide the parts of these slots (none shows every slot)", &AppearanceCommandsInternal::runHide );
    SW_DEV_COMMAND( AppearanceSave, "appearance.save", "appearance.save <object> <file>", "Write a character's current appearance as a share code to a file",
                    &AppearanceCommandsInternal::runSave );
    SW_DEV_COMMAND( AppearanceLoad, "appearance.load", "appearance.load <object> <file>", "Dress a character with the share code in a file", &AppearanceCommandsInternal::runLoad );
} // namespace sw
