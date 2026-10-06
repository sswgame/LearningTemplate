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
} // namespace test

namespace test
{
    class TestContext
    {
    public:
        void begin( const sw::string& testName );
        void addFailure( const sw::string& condition, const sw::string& file, int32 line, const sw::string& message = "" );
        /** @brief 이 케이스를 건너뜀으로 표시합니다(이유는 `TestRegistry::skipCurrentTest` 가 로그에 남긴다). */
        void skip() { _bSkipped = true; }
        void deferCleanup( sw::Delegate<void()> cleanup );
        void runCleanup();
        /** @brief 이 케이스가 `test::makeTempPath` 를 썼다고 표시합니다 — 끝나면 프레임워크가 그 접두어의 경로를 지운다. */
        void markTempPathUsed() { _bTempPathUsed = true; }

        sw::string              getTestName() const { return _testName; }
        sw::vector<TestFailure> getListFailure() const { return _listFailure; }
        bool                    hasFailed() const { return _listFailure.empty() == false; }
        bool                    isSkipped() const { return _bSkipped; }
        bool                    isTempPathUsed() const { return _bTempPathUsed; }

    private:
        sw::string                       _testName;
        sw::vector<TestFailure>          _listFailure;
        sw::vector<sw::Delegate<void()>> _listCleanup;
        bool                             _bSkipped{ false };
        bool                             _bTempPathUsed{ false };
    };
} // namespace test
