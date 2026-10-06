/**
 * @file UserDataPath.h
 * @brief 사용자 데이터 · 설정 폴더 — 사용자 설정 · 로컬 저장(세이브 · 프로필) · 장치 키가 같은 한 곳에서 경로를 얻습니다.
 * @details - Windows: 데이터 · 설정 모두 `%LOCALAPPDATA%/SWEngine/<game>/`.
 *          - 리눅스: 데이터는 `$XDG_DATA_HOME`(없으면 `~/.local/share`)`/swengine/<game>/`, 설정은 `$XDG_CONFIG_HOME`(없으면 `~/.config`)`/swengine/<game>/`.
 *          - 사용자 폴더를 모르면(환경 변수 없음) 작업 폴더의 `Saved/<game>/`. `<game>` 은 소문자, 비면 `default`.
 *          언리얼 `FPlatformProcess::UserSettingsDir` · 유니티 `Application.persistentDataPath` 와 같은 자리다. 모바일 샌드박스는 지원 플랫폼이 되면 갈래 하나로.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
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
