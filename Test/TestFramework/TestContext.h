/**
 * @file Test/TestFramework/TestContext.h
 * @brief 단일 테스트 케이스의 실행 결과와 상태
 */
#pragma once
#include "Engine/EngineMinimal.h"

namespace test
{
    struct TestFailure
    {
        sw::string _condition;
        sw::string _file;
        int32      _line;
        sw::string _message;
    };

    class TestContext
    {
    public:
        void begin( const sw::string& testName );
        void addFailure( const sw::string& condition, const sw::string& file, int32 line, const sw::string& message = "" );
        void skip( const sw::string& reason, const sw::string& file, int32 line );
        void deferCleanup( sw::Delegate<void()> cleanup );
        void runCleanup();

        sw::string              getTestName() const { return _testName; }
        sw::vector<TestFailure> getListFailure() const { return _listFailure; }
        sw::string              getSkipReason() const { return _skipReason; }
        bool                    hasFailed() const { return _listFailure.empty() == false; }
        bool                    isSkipped() const { return _bSkipped; }

    private:
        sw::string                       _testName;
        sw::vector<TestFailure>          _listFailure;
        sw::vector<sw::Delegate<void()>> _listCleanup;
        sw::string                       _skipReason;
        bool                             _bSkipped{ false };
    };
} // namespace test
