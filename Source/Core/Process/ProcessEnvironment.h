/**
 * @file ProcessEnvironment.h
 * @brief 프로세스의 환경 변수를 읽고 정하는 유일한 자리입니다. `std::getenv` · `GetEnvironmentVariable` 을 부르는 곳은 이 구현 파일뿐입니다(`CheckProcessEnvironment`).
 * @details 환경 변수에는 세 상태가 있다 — 없음(unset), 빈 값(`FOO=`), 값 있음. 곳곳에서 `getenv` 를 직접 부르면 빈 값을 없음으로 읽는 곳과 있음으로 읽는 곳이 갈린다.
 *          그래서 읽기를 두 함수로 나눠 호출 쪽이 정책을 이름으로 고른다.
 *          - `tryRead`: 빈 값도 "정해져 있음"으로 읽는다. 사용자가 일부러 비워 정한 값을 존중해야 하는 변수(허용 목록 · 비밀 · 명시적 선택)가 쓴다.
 *          - `tryReadNonEmpty`: 없음과 빈 값을 같게 읽는다. 경로 · 포트처럼 빈 글이 쓸모없는 변수가 쓴다(빈 `HOME` 으로 상대 경로를 만들지 않는다).
 *          Windows 는 `GetEnvironmentVariableW` 로 읽어 UTF-8 로 돌려준다(CRT 의 `getenv` 는 ANSI 코드 페이지라 한글 사용자 이름이 든 `LOCALAPPDATA` 가 깨진다). 정하기는 OS 환경과
 *          CRT 복사본을 함께 바꾼다(CRT 를 쓰는 서드파티 코드 — Tracy 클라이언트 — 가 `getenv` 로 읽는다). 메인 스레드 설정 단계에서 쓰는 것을 전제로 한다(`setenv` 는 스레드 안전하지 않다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
    /** @struct ProcessEnvironment @brief 환경 변수 읽기 · 정하기입니다(모두 정적). 씬 환경(`EnvironmentUtil`)과 무관합니다. */
    struct SW_API ProcessEnvironment
    {
        /**
         * @brief 변수 @p name 이 정해져 있으면 값을 @p outValue 에 담고 true 입니다. 빈 값도 true 이며 @p outValue 는 빈 글입니다.
         * @return 변수가 없으면 false 이고 @p outValue 는 비어 있습니다.
         */
        [[nodiscard]] static bool tryRead( string_view name, string& outValue );
        /** @brief `tryRead` 와 같지만 빈 값을 없음으로 읽습니다 — 값이 있을 때만 true 입니다. */
        [[nodiscard]] static bool tryReadNonEmpty( string_view name, string& outValue );
        /** @brief 변수 @p name 을 @p value 로 정합니다(이미 있으면 바꿉니다). 이름이 비었거나 `=` 를 품었거나 OS 가 거절하면 false 입니다. */
        [[nodiscard]] static bool trySet( string_view name, string_view value );
        /** @brief 변수 @p name 이 정해져 있는지(빈 값 포함)만 묻습니다 — 값이 필요 없는 "자식 역할이다" 같은 표식 검사용입니다. */
        [[nodiscard]] static bool isSet( string_view name );
        /** @brief 변수 @p name 을 지웁니다(없던 변수도 true). 이름이 비었거나 `=` 를 품었거나 OS 가 거절하면 false 입니다. */
        [[nodiscard]] static bool tryUnset( string_view name );
    };
} // namespace sw
