/**
 * @file Test/TestFramework/TestEnvironment.h
 * @brief 테스트 간 공유 엔진 작업을 정리하는 환경
 */
#pragma once

namespace test
{
    class TestEnvironment
    {
    public:
        void tearDown() const;
    };
} // namespace test
