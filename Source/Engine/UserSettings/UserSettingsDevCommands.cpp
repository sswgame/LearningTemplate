#include "pch.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/Container/StringUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/UserSettings/UserSettingsManager.h"

#if SW_DEV_COMMANDS_ENABLED

namespace sw
{
    namespace
    {
        /**
         * @brief 개발 명령 `settings.apply` · `settings.confirm` — 사용자 설정을 옵션 메뉴와 같은 길(보류 → 적용 → 확인 카운트다운)로 바꿉니다.
         * @details 자동화 시나리오가 `<DevCommand>` 로 부른다. 확인이 필요한 설정(화면 방식 · 해상도)은 `settings.confirm` 이 없으면 카운트다운 뒤 되돌아간다 —
         *          메뉴의 "N 초 뒤 되돌립니다" 를 사람 손 없이 지나가게 한다. 개발 명령이라 Shipping 에는 없고, 이 명령을 쓰는 시나리오는 그 구성에서 건너뛴다.
         */
        struct UserSettingsDevCommandsInternal
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
                    const string_view text     = argument;
                    const size_t      equalPos = text.find( '=' );
                    if ( equalPos == string_view::npos )
                        return false;
                    const hashed_string        settingID( StringUtil::trim( text.substr( 0, equalPos ) ) );
                    const UserSettingSetResult result = pSettings->setPendingValue( settingID, StringUtil::trim( text.substr( equalPos + 1 ) ) );
                    const bool                 bTaken = result == UserSettingSetResult::Accepted || result == UserSettingSetResult::Clamped || result == UserSettingSetResult::Unchanged;
                    if ( bTaken == false )
                    {
                        pSettings->revertPending();
                        outReply = "settings: '" + argument + "' was not taken (result " + to_string( static_cast<uint32>( result ) ) + ") - nothing applied";
                        return true;
                    }
                }
                const UserSettingsApplyResult applied = pSettings->applyPending();
                outReply                              = "settings: applied " + to_string( applied._appliedCount ) + ", failed " + to_string( applied._failedCount ) + ", confirm " +
                           ( applied._bAwaitingConfirm ? "pending" : "not needed" );
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
                    &UserSettingsDevCommandsInternal::runApply );
    SW_DEV_COMMAND( SettingsConfirm, "settings.confirm", "settings.confirm", "Keep the display change that awaits confirmation", &UserSettingsDevCommandsInternal::runConfirm );
} // namespace sw
#endif
