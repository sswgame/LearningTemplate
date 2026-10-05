/**
 * @file Test/TestFramework/TestFilter.h
 * @brief 테스트 선택 필터
 */
#pragma once
#include "Engine/EngineMinimal.h"

namespace test
{
    class TestFilter
    {
    public:
        /**
         * @brief `--test_filter` 값을 읽습니다. gtest 와 같은 모양입니다 — 첫 `-` 앞은 고르는 패턴, 뒤는 빼는 패턴, 패턴 사이는 `:` 또는 `,`.
         * @details `A.*:B.*` · `A.*,B.*` 는 둘 다 고르고, `-A.*` 는 A 만 빼며, `A.*:-A.X` 는 A 에서 X 만 뺍니다. 패턴 안의 `*` 는 아무 글자열입니다.
         */
        void setPattern( const sw::string& filter );
        bool matches( const sw::string& fullName ) const;
        /** @brief 고르는 패턴(앞에 `-` 가 없는 것)이 하나라도 있는가 — 빼기만 적은 필터는 일부러 고르지 않는 것이다. */
        bool hasIncludePattern() const { return _listIncludePattern.empty() == false; }

    private:
        static bool matchGlob( const sw::string& pattern, const sw::string& text );

        sw::vector<sw::string> _listIncludePattern;
        sw::vector<sw::string> _listExcludePattern;
    };
} // namespace test
