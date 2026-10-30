/**
 * @file AbsolutePath.h
 * @brief 엄격하게 확인한 절대 디스크 경로 형식(`AbsolutePath`)과 기동 때 한 번 정하는 루트 폴더 모음(`ProcessRoots`)입니다. 디스크 I/O(R2)가 받을 형식이며, 0 단계에서는 아직 아무도 쓰지 않습니다.
 * @details `FileUtil::isAbsolutePath` 는 `C:foo`(드라이브 상대) · `\foo`(드라이브 없는 루트)도 절대로 본다. 이 형식은 그것들을 거절한다 —
 *          Windows 는 `X:/` · `X:\` · UNC(`//서버/공유`), POSIX 는 `/` 로 시작해야 한다. 만드는 길은 `tryMake` 하나이고 구분자는 `/` 로 맞춘다(대소문자는 그대로).
 *          언리얼 `FPaths::IsRelative` 의 반대가 아니라, 만들 때 한 번 검증한 값을 들고 다니는 형식이다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"

#include <optional>

namespace sw
{
    /**
     * @class AbsolutePath
     * @brief 검증을 지난 절대 경로 글입니다. 빈 값은 만들 수 없다.
     */
    class SW_API AbsolutePath
    {
    public:
        /** @brief @p path 가 엄격한 절대 경로면 true 입니다(Windows: `X:/` · `X:\` · UNC, POSIX: `/`). */
        [[nodiscard]] static bool isStrictAbsolute( string_view path );
        /** @brief 엄격한 절대 경로면 구분자를 `/` 로 맞춘 값을, 아니면 빈 값을 돌려줍니다. */
        [[nodiscard]] static std::optional<AbsolutePath> tryMake( string_view path );

        /** @brief 경로 글입니다(구분자 `/`). */
        [[nodiscard]] const string& str() const { return _path; }
        [[nodiscard]] const utf8*   c_str() const { return _path.c_str(); }

        [[nodiscard]] bool operator==( const AbsolutePath& other ) const { return _path == other._path; }
        [[nodiscard]] bool operator!=( const AbsolutePath& other ) const { return _path != other._path; }

    private:
        explicit AbsolutePath( string path )
            : _path{ std::move( path ) }
        {
        }

        string _path;
    };

    /**
     * @struct ProcessRoots
     * @brief 프로세스가 기동 때 한 번 정하는 루트 폴더 모음입니다(R5). 상대 경로는 실행 중 작업 폴더가 아니라 이 루트 기준으로 입력 경계에서 한 번 절대화한다.
     * @details 0 단계에서는 형식만 있고 아직 아무도 설치하지 않는다. 2 단계가 App · Server · 테스트 하네스 기동에서 `install` 하고, 지금 Saved 를 따로 구하는 일곱 곳을 `get()` 으로 바꾼다.
     *          못 정했으면 `get()` 이 멈춘다(폴백 없음 - 언리얼 `FPaths::ProjectSavedDir` 도 기동 때 정해진다).
     */
    struct SW_API ProcessRoots
    {
        AbsolutePath _binary;   ///< 실행 파일 폴더
        AbsolutePath _project;  ///< 프로젝트(저장소) 루트 - `-project=`
        AbsolutePath _content;  ///< 리소스 도메인 루트(`Content`)
        AbsolutePath _saved;    ///< 개발 파생물(셰이더 캐시 · 임시)
        AbsolutePath _logs;     ///< 로그
        AbsolutePath _crash;    ///< 크래시 덤프
        AbsolutePath _userData; ///< 사용자 설정 · 세이브
        AbsolutePath _packs;    ///< 팩(`.pack`)
        AbsolutePath _ddc;      ///< 파생 데이터 캐시

        /** @brief 루트를 설치합니다. 두 번 부르면 멈춥니다(기동 한 번). */
        static void install( ProcessRoots roots );
        /** @brief 설치된 루트입니다. 설치 전이면 멈춥니다. */
        [[nodiscard]] static const ProcessRoots& get();
    };
} // namespace sw
