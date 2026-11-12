/**
 * @file PathKey.h
 * @brief 비교 · 맵 키용으로 접은 경로 글(구분자 `/`, 늘 소문자)의 형식입니다. `FileUtil::normalizePath` 가 만듭니다.
 * @details 접은 글로 디스크 경로의 포함 · 같음을 보면 리눅스에서 대소문자만 다른 바깥 폴더가 안으로 보인다(자동화 쓰기 문이 `Content/SCRATCH` 를
 *          사본으로 보고 열어 줬다). 그래서 이 형식은 **어떤 글로도 암시적으로 바뀌지 않는다** — 디스크 판정(`FileUtil::isPathAtOrUnderRoot` 등, `string_view` 를 받는다)에도,
 *          `string x = normalizePath( … )` 같은 지역 변수에도 그대로 들어가지 않는다. 키 · id 로 꺼낼 때는 `str()` · `view()` 를 눈에 보이게 적는다
 *          (rvalue 의 `str()` 은 옮기므로 복사가 늘지 않는다). 디스크 경로끼리는 `FileUtil::makeComparableDiskPath`(OS 규칙)를 쓴다.
 *          언리얼은 패키지 이름을 대소문자 무시 `FName` 으로, 디스크 경로는 `FString` 으로 갈라 둔다 — 같은 가름을 형식으로 둔다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"

namespace sw
{
    struct FileUtil;

    /**
     * @class PathKey
     * @brief 소문자 · `/` 로 접은 경로 키입니다. 만드는 길은 `FileUtil::normalizePath` 하나입니다.
     */
    class PathKey
    {
    public:
        /** @brief 빈 키입니다. */
        PathKey() = default;

        /** @brief 키 글입니다(맵 키 · 리소스 id 로 쓴다 — 디스크 판정에 넘기지 않는다). */
        [[nodiscard]] const string& str() const& noexcept { return _text; }
        /** @brief 임시 키의 글을 옮겨 꺼냅니다(복사하지 않는다). */
        [[nodiscard]] string str() && noexcept { return std::move( _text ); }
        /** @brief 키 글의 뷰입니다. */
        [[nodiscard]] string_view view() const noexcept { return _text; }
        /** @brief 널로 끝나는 키 글입니다. */
        [[nodiscard]] const utf8* c_str() const noexcept { return _text.c_str(); }
        /** @brief 비었는지 봅니다. */
        [[nodiscard]] bool empty() const noexcept { return _text.empty(); }

        [[nodiscard]] bool operator==( const PathKey& other ) const noexcept { return _text == other._text; }
        [[nodiscard]] bool operator!=( const PathKey& other ) const noexcept { return _text != other._text; }

    private:
        friend struct FileUtil;

        explicit PathKey( string text ) noexcept
            : _text{ std::move( text ) }
        {
        }

    private:
        string _text;
    };
} // namespace sw
