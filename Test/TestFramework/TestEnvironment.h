/**
 * @file Test/TestFramework/TestEnvironment.h
 * @brief 테스트 간 공유 엔진 작업을 정리하는 환경
 */
#pragma once

namespace sw
{
    struct EngineServices;
} // namespace sw

namespace test
{
    class TestEnvironment
    {
    public:
        void tearDown() const;
    };

    /**
     * @brief 전역 엔진 서비스 표를 다시 바인딩합니다 — 테스트가 바인딩을 흔들 때 쓰는 **유일한** 창구.
     * @details `bindEngineServices` 를 부르는 파일은 곧 "호스트" 다 — `CheckEngineServiceBinding` 이 그 호출로
     *          호스트를 찾아 필수 서비스를 전부 채우는지 대조한다. 테스트 파일이 직접 부르면 그 파일이 호스트로
     *          잡혀 게이트가 막는다. 정의는 진짜 호스트인 `main.cpp` 에 있다. 되돌리는 것은 부른 쪽의 몫이다 —
     *          뒤따르는 테스트가 전부 이 표로 게이팅된다.
     */
    void rebindEngineServices( const sw::EngineServices& services );
} // namespace test
