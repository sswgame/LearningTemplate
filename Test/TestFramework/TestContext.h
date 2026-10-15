/**
 * @file Test/TestFramework/TestContext.h
 * @brief 단일 테스트 케이스의 실행 결과와 상태
 */
#pragma once
#include "TestFramework/TestCase.h"

namespace test
{
    struct TestFailure
    {
        sw::string _condition;
        sw::string _file;
        int32      _line;
        sw::string _message;
        sw::string _trace; ///< 실패 순간 그 스레드에 걸려 있던 `ScopedTrace` 들(바깥부터, ` > ` 로 잇는다). 없으면 비어 있다
    };
} // namespace test

namespace test
{
    class TestContext
    {
    public:
        void begin( const sw::string& testName );
        void addFailure( const TestFailure& failure );
        /** @brief 이 케이스를 건너뜀으로 표시합니다(이유는 `TestRegistry::skipCurrentTest` 가 로그에 남긴다). */
        void skip() { _bSkipped = true; }
        /** @brief 이 케이스를 "구성에 @p feature 가 없다" 로 건너뜁니다(`SW_TEST_REQUIRES_FEATURE`). */
        void skipForMissingFeature( BuildFeature feature )
        {
            _bSkipped       = true;
            _missingFeature = feature;
        }
        void deferCleanup( sw::Delegate<void()> cleanup );
        void runCleanup();
        /** @brief 이 케이스가 `test::makeTempPath` 를 썼다고 표시합니다 — 끝나면 프레임워크가 그 접두어의 경로를 지운다. */
        void markTempPathUsed() { _bTempPathUsed = true; }
        /** @brief 이 케이스가 실행 씨앗(`test::getRandomSeed`)을 읽었다고 표시합니다 — 지면 재현 명령에 씨앗이 붙는다. */
        void markSeedUsed() { _bSeedUsed = true; }

        sw::string                     getTestName() const { return _testName; }
        const sw::vector<TestFailure>& getListFailure() const { return _listFailure; }
        bool                           hasFailed() const { return _listFailure.empty() == false; }
        bool                           isSkipped() const { return _bSkipped; }
        bool                           isTempPathUsed() const { return _bTempPathUsed; }
        bool                           isSeedUsed() const { return _bSeedUsed; }
        BuildFeature                   getMissingFeature() const { return _missingFeature; }

    private:
        sw::string                       _testName;
        sw::vector<TestFailure>          _listFailure;
        sw::vector<sw::Delegate<void()>> _listCleanup;
        BuildFeature                     _missingFeature{ BuildFeature::Count };
        bool                             _bSkipped{ false };
        bool                             _bTempPathUsed{ false };
        bool                             _bSeedUsed{ false };
    };
} // namespace test
