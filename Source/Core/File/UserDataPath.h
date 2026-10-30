/**
 * @file UserDataPath.h
 * @brief 사용자 데이터 · 설정 폴더 — 사용자 설정 · 로컬 저장(세이브 · 프로필) · 장치 키가 같은 한 곳에서 경로를 얻습니다.
 * @details - Windows: 데이터 · 설정 모두 `%LOCALAPPDATA%/SWEngine/<game>/`.
 *          - 리눅스: 데이터는 `$XDG_DATA_HOME`(없으면 `~/.local/share`)`/swengine/<game>/`, 설정은 `$XDG_CONFIG_HOME`(없으면 `~/.config`)`/swengine/<game>/`.
 *          - 사용자 폴더를 모르면(환경 변수 없음) 멈춘다(`SW_USER_DATA_ROOT` 로 정한다 - 작업 폴더로 대신하지 않는다). `<game>` 은 소문자, 비면 `default`.
 *          - 환경 변수 `SW_USER_DATA_ROOT`(`kUserDataRootVariable`)가 있으면 데이터 · 설정 모두 그 아래 `<game>/` 이다. 엔진이 읽는 위치만 가리키므로(`LOCALAPPDATA` 를 옮기면
 *            드라이버 셰이더 캐시 같은 남의 폴더도 따라간다) 자동화가 `-gv_userSettingsFile` 을 모르는 배포본의 사용자 설정 · 세이브를 실행별 폴더로 가를 때 쓴다.
 *          언리얼 `FPlatformProcess::UserSettingsDir` · 유니티 `Application.persistentDataPath` 에 해당한다. 모바일 샌드박스는 지원 플랫폼이 되면 분기 하나로 추가한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 사용자 데이터 · 설정 폴더의 뿌리를 덮어쓰는 환경 변수 이름입니다(자동화용 — 정본은 이 줄이고 파이썬 러너가 이 헤더에서 읽습니다). */
    inline constexpr const utf8* kUserDataRootVariable = "SW_USER_DATA_ROOT";

    /**
     * @struct UserDataPath
     * @brief 사용자 폴더 경로 계산입니다(폴더를 만들지는 않는다).
     */
    struct SW_API UserDataPath
    {
        /** @brief 데이터 폴더(세이브 · 로컬 저장 · 장치 키)입니다. 끝에 구분자가 없습니다. */
        static string getDataDirectory( string_view gameName );
        /** @brief 설정 폴더(`usersettings.json`)입니다. 끝에 구분자가 없습니다. */
        static string getConfigDirectory( string_view gameName );
        /** @brief 로컬 저장 경로를 데이터 폴더 기준 절대 경로로 바꿉니다. 절대 경로 · SQLite `:memory:` 는 그대로입니다. */
        static string resolve( string_view gameName, string_view relativeOrAbsolutePath );
    };
} // namespace sw
