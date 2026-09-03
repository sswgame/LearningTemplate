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
        void setPattern( const sw::string& filter );
        bool matches( const sw::string& fullName ) const;

    private:
        static bool matchGlob( const sw::string& pattern, const sw::string& text );

        sw::vector<sw::string> _listIncludePattern;
        sw::vector<sw::string> _listExcludePattern;
    };
} // namespace test
