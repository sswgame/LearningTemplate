/**
 * @file SystemFontLocator.h
 * @brief OS 시스템 글꼴 폴더와 그 안의 글꼴 파일을 찾습니다(런타임 글자 · 에디터 ImGui 가 함께 씁니다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    /**
     * @struct SystemFontLocator
     * @brief 시스템 글꼴 폴더 목록 · 파일 이름으로 찾기입니다. 게임 스레드(또는 에디터 UI 스레드)에서 부릅니다.
     */
    struct SW_API SystemFontLocator
    {
        /**
         * @brief 이 플랫폼의 시스템 글꼴 폴더들입니다(있는 것만).
         * @details Windows 는 `%WINDIR%/Fonts` · 사용자 글꼴 `%LOCALAPPDATA%/Microsoft/Windows/Fonts`, 리눅스는 `/usr/share/fonts` · `/usr/local/share/fonts` ·
         *          `~/.local/share/fonts` 입니다.
         */
        static vector<string> getSystemFontDirectories();

        /**
         * @brief 파일 이름으로 시스템 글꼴 파일을 찾습니다. 없으면 빈 문자열입니다.
         * @details 폴더 바로 아래를 먼저 보고, 리눅스는 하위 폴더까지 훑습니다(배포판이 `truetype/<가족>/` 처럼 나눈다 — 훑은 결과는 프로세스에 한 번 캐시).
         *          이름 비교는 대소문자를 가리지 않습니다.
         */
        static string findSystemFontFile( string_view fileName );
    };
} // namespace sw
