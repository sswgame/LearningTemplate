/**
 * @file AbsolutePath.h
 * @brief 엄격하게 확인한 절대 디스크 경로 형식입니다. 디스크 I/O(R2) · 라이브러리 로드 · 실행 파일 경로가 받는다.
 * @details `FileUtil::isAbsolutePath` 는 `C:foo`(드라이브 상대) · `\foo`(드라이브 없는 루트)도 절대로 본다. 이 형식은 그것들을 거절한다 —
 *          Windows 는 `X:/` · `X:\` · UNC(`//서버/공유`), POSIX 는 `/` 로 시작해야 한다. 만드는 길은 `tryMake` 하나이고 구분자는 `/` 로 맞춘다(대소문자는 그대로).
 *          언리얼 `FPaths::IsRelative` 의 반대가 아니라, 만들 때 한 번 검증한 값을 들고 다니는 형식이다.
 *          로그 · 파일을 쓰지 않는 값 형식이라 낮은 티어(`Core/Path`)에 둔다 - 프로세스(`ProcessCommand`) · 파일 · 모듈이 함께 쓴다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"

namespace sw
{
    /**
     * @class AbsolutePath
     * @brief 검증을 지난 절대 경로 글입니다. 디스크 I/O(`FileUtil` 의 `exists` · `readTextFile` · `writeFile` …)는 이 형식만 받는다.
     * @details 문자열에서 만드는 길은 `tryMake` · `tryResolve`(빈 값을 돌려준다)와 `FileUtil::toDiskPath`(I/O 인자 자리 — 거절한 글은 빈 경로) 셋이다.
     *          빈 경로는 `toDiskPath` 가 빈 글 · 상대 경로를 받았을 때만 생기고, 그 값을 받은 I/O 는 실패한다. 글에서 암시적으로 바뀌지 않는다(아래 static_assert).
     */
    class SW_API AbsolutePath
    {
    public:
        /** @brief @p path 가 엄격한 절대 경로면 true 입니다(Windows: `X:/` · `X:\` · UNC, POSIX: `/`). */
        [[nodiscard]] static bool isStrictAbsolute( string_view path );
        /** @brief 엄격한 절대 경로면 구분자를 `/` 로 맞춘 값을, 아니면 빈 값을 돌려줍니다. */
        [[nodiscard]] static optional<AbsolutePath> tryMake( string_view path );
        /**
         * @brief 사람이 적은 경로 @p text 를 절대 경로로 풉니다 - 절대면 그 글, 상대면 @p baseDirectory 아래다(R5 - 기준은 부르는 쪽이 기동 때 정한 폴더).
         * @details 드라이브 상대(`C:foo`) · 드라이브 없는 루트(`\foo`, Windows)는 어느 쪽도 아니라 빈 값이다. 빈 글도 빈 값이다. 명령줄(`CommandLineDiskPath`) · 전역 변수 경로가 함께 쓴다.
         *          `.` · `..` 구간은 글로 풀어 없앤다(뿌리 위로 올라가면 빈 값) — 결과는 포함 판정(`FileUtil::tryMakePathUnderRoot`)이 거절하는 `..` 를 담지 않는다.
         */
        [[nodiscard]] static optional<AbsolutePath> tryResolve( string_view text, const AbsolutePath& baseDirectory );

        /**
         * @brief 이 경로 아래 @p relative 를 이은 경로입니다(언리얼 `FPaths::Combine` · `operator/` 자리) — 글로 잇고 다시 `toDiskPath` 로 올리는 왕복이 없다.
         * @details 구분자는 `/` 로 맞추고 이 경로의 끝 구분자 · @p relative 의 앞뒤 구분자 · 빈 구간 · `.` 구간은 버린다. `..` 구간은 이은 부분 안에서만 풀고,
         *          이 경로 위로 올라가면(상위 탈출) 빈 경로다 — 그 값을 받은 I/O 는 실패한다(폴백 없음). 결과는 포함 판정이 밖으로 보는 `..` 를 담지 않는다.
         *          빈 경로에 이으면 빈 경로, @p relative 가 비면 이 경로(끝 구분자를 뗀)다. 할당은 결과 하나다.
         */
        [[nodiscard]] AbsolutePath join( string_view relative ) const;
        /** @brief 부모 폴더입니다. 끝 구분자는 무시하고, 부모가 뿌리면 뿌리(`/a` → `/`, `C:/a` → `C:/`), 뿌리 자신의 부모는 빈 경로입니다(`FileUtil::getDirectoryPart` 와 같은 규칙). */
        [[nodiscard]] AbsolutePath getParent() const;
        /**
         * @brief 마지막 성분 끝에 @p suffix 를 붙인 경로입니다(`a/b.png` + `.meta` → `a/b.png.meta` — 곁 파일 · 기록 파일 이름).
         * @details 글로 꺼내 붙이고 `toDiskPath` 로 다시 올리는 왕복 대신 쓴다(할당은 결과 하나). @p suffix 가 구분자 · 널을 담거나 이 경로가 비었거나 끝이 구분자면(뿌리 · 폴더 표기) 빈 경로다
         *          — 성분을 바꾸는 일은 `join` · `getParent` 가 맡는다. 언리얼 `FPaths` 의 이름 붙이기 · `std::filesystem::path::operator+=` 와 같은 자리다.
         */
        [[nodiscard]] AbsolutePath appendSuffix( string_view suffix ) const;

        /** @brief 경로 글입니다(구분자 `/`). */
        [[nodiscard]] const string& str() const { return _path; }
        [[nodiscard]] const utf8*   c_str() const { return _path.c_str(); }
        /** @brief `FileUtil::toDiskPath` 가 거절한 글(빈 글 · 상대 경로)에서 만든 빈 경로면 true 입니다. */
        [[nodiscard]] bool empty() const { return _path.empty(); }

        [[nodiscard]] bool operator==( const AbsolutePath& other ) const { return _path == other._path; }
        [[nodiscard]] bool operator!=( const AbsolutePath& other ) const { return _path != other._path; }

    private:
        friend struct FileUtil; // `toDiskPath` 가 거절한 글을 빈 경로로 만든다

        explicit AbsolutePath( string path )
            : _path{ std::move( path ) }
        {
        }

    private:
        string _path;
    };

    // 논리 경로 글(리소스 id · `DataPath` 를 꺼낸 글)이 디스크 I/O 로 새지 않게 글에서 암시적으로 만들 수 없다.
    static_assert( std::is_convertible_v<string, AbsolutePath> == false && std::is_convertible_v<string_view, AbsolutePath> == false &&
                       std::is_convertible_v<const utf8*, AbsolutePath> == false,
                   "AbsolutePath is made explicitly (tryMake · tryResolve · FileUtil::toDiskPath)" );
} // namespace sw
