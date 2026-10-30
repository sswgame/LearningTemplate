/**
 * @file AbsolutePath.h
 * @brief 엄격하게 확인한 절대 디스크 경로 형식입니다. 디스크 I/O(R2)가 받을 형식이며, 0 단계에서는 아직 아무도 쓰지 않습니다.
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
} // namespace sw
