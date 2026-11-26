#include "pch.h"

#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/UserSettings/UserSettingsManager.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 개발 명령 `settings.apply` · `settings.confirm` — 사용자 설정을 옵션 메뉴와 같은 길(보류 → 적용 → 확인 카운트다운)로 바꿉니다.
         * @details 자동화 시나리오가 `<DevCommand>` 로 부른다. 확인이 필요한 설정(화면 방식 · 해상도)은 `settings.confirm` 이 없으면 카운트다운 뒤 되돌아간다 —
         *          메뉴의 "N 초 뒤 되돌립니다" 를 사람 손 없이 지나가게 한다. 개발 명령이라 Shipping 에는 없고, 이 명령을 쓰는 시나리오는 그 구성에서 건너뛴다.
         */
        struct UserSettingsCommandsInternal
        {
            static bool runApply( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() )
                    return false;
                UserSettingsManager* pSettings = engine::findSubsystem<UserSettingsManager>();
                if ( pSettings == nullptr )
                {
                    outReply = "settings: no UserSettingsManager service";
                    return true;
                }
                for ( const string& argument : listArgument )
                {
                    string_view key;
                    string_view value;
                    if ( StringUtil::trySplitKeyValue( argument, key, value ) == false )
                        return false;
                    const hashed_string        settingID( key );
                    const UserSettingSetResult result = pSettings->setPendingValue( settingID, value );
                    const bool                 bTaken = result == UserSettingSetResult::Accepted || result == UserSettingSetResult::Clamped || result == UserSettingSetResult::Unchanged;
                    if ( bTaken == false )
                    {
                        pSettings->revertPending();
                        StringBuilder<constant::buffer::kMax256> replyBuilder;
                        replyBuilder.appendFormat( "settings: '%#' was not taken (result %#) - nothing applied", argument, static_cast<uint32>( result ) );
                        outReply = string( replyBuilder.view() );
                        return true;
                    }
                }
                const UserSettingsApplyResult            applied = pSettings->applyPending();
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "settings: applied %#, failed %#, confirm %#", applied._appliedCount, applied._failedCount,
                                           applied._bAwaitingConfirm ? "pending" : "not needed" );
                outReply = string( replyBuilder.view() );
                return true;
            }

            static bool runConfirm( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                UserSettingsManager* pSettings = engine::findSubsystem<UserSettingsManager>();
                if ( pSettings == nullptr || pSettings->isAwaitingConfirm() == false )
                {
                    outReply = "settings: nothing awaits confirmation";
                    return true;
                }
                pSettings->confirmChanges();
                outReply = "settings: confirmed";
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( SettingsApply, "settings.apply", "settings.apply <id>=<value> ...", "Apply user settings the way the options menu does (pending, apply, confirm countdown)",
                    &UserSettingsCommandsInternal::runApply );
    SW_DEV_COMMAND( SettingsConfirm, "settings.confirm", "settings.confirm", "Keep the display change that awaits confirmation", &UserSettingsCommandsInternal::runConfirm );
} // namespace sw
