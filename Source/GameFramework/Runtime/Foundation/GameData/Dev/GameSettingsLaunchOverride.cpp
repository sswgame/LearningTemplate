#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"

#include "GameFramework/Runtime/Foundation/GameData/GameSettings.h"
#include "GameFramework/Runtime/Foundation/GameData/GameSettingsOverride.h"

// 개발 구성에만 있다 — 배포는 이 폴더를 짓지 않아 게임은 팩의 gamesettings 그대로 선다(끝점 · 서버 주소는 운영자가 gamesettings 에 적는 값이다).
namespace sw
{
    /** @brief `-gv_onlineServerAddress=<주소:포트>`: gamesettings 의 온라인 서버 대신 붙을 주소입니다(로컬 실행 `Scripts/net/online_local_run.py` · 자동화). */
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_onlineServerAddress, "", "gamesettings 의 온라인 서버 대신 붙을 주소 (주소:포트, 비면 gamesettings)" );
    /** @brief `-gv_telemetryUploadEndpoint=<주소>`: gamesettings 의 텔레메트리 끝점 대신 쓸 주소입니다(로컬 수신기 `Scripts/net/telemetry_local_run.py` · 자동화). */
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_telemetryUploadEndpoint, "", "gamesettings 의 텔레메트리 업로드 끝점 대신 쓸 주소 (비면 gamesettings)" );
    /** @brief `-gv_crashUploadEndpoint=<주소>`: gamesettings 의 크래시 보고 끝점 대신 쓸 주소입니다(로컬 수신기 · 자동화). */
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_crashUploadEndpoint, "", "gamesettings 의 크래시 보고 업로드 끝점 대신 쓸 주소 (비면 gamesettings)" );

    namespace
    {
        /** @brief 실행 인자의 끝점 · 서버 주소를 gamesettings 칸에 넣습니다. 상태가 없다 — 값은 위의 변수다. */
        class GameSettingsLaunchOverride final : public IGameSettingsOverride
        {
        public:
            void apply( GameSettings& settings ) override
            {
                replace( settings._onlineServerAddress, gv_onlineServerAddress, "onlineServerAddress" );
                replace( settings._telemetryUploadEndpoint, gv_telemetryUploadEndpoint, "telemetryUploadEndpoint" );
                replace( settings._crashUploadEndpoint, gv_crashUploadEndpoint, "crashUploadEndpoint" );
            }

        private:
            /** @brief @p value 가 비어 있지 않으면 칸 @p field 를 그 값으로 바꾸고 알립니다(비면 gamesettings 그대로). */
            static void replace( string& field, const string& value, const utf8* pFieldName )
            {
                if ( value.empty() )
                    return;
                SW_LOG_INFO_ID( "GameSettingsLaunchOverride.FieldReplaced", "gamesettings %# '%#' is replaced by the launch argument '%#'", pFieldName, field.c_str(), value.c_str() );
                field = value;
            }
        };

        GameSettingsLaunchOverride                s_gameSettingsLaunchOverride;
        const IGameSettingsOverride::Registration s_gameSettingsLaunchOverrideRegistration{ s_gameSettingsLaunchOverride };
    } // namespace
} // namespace sw
