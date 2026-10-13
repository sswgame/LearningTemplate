/**
 * @file Test/TestFramework/TestEnvironment.h
 * @brief 테스트 간 공유 엔진 작업을 정리하는 환경
 */
#pragma once
#include "Core/Subsystem/SubsystemScope.h"

namespace test
{
    class TestEnvironment
    {
    public:
        void tearDown() const;
    };
} // namespace test

namespace test
{
    /**
     * @brief 전역 엔진 서비스 표를 @p subsystems 에서 다시 찾아 묶습니다 — 테스트가 바인딩을 흔들 때 쓰는 **유일한** 창구.
     * @details 서비스 하나를 갈아 끼우려면 하네스의 범위(`sw::engine::getEngineSubsystems()`)를 부모로 둔 컬렉션에 그 서비스만 넣어 넘긴다 — 같은 이름이
     *          부모의 것을 가린다. 되돌리는 것은 부른 쪽의 몫이다(원래 범위로 다시 부른다) — 뒤따르는 테스트가 전부 이 표로 게이팅된다.
     * @return 비어 있는 첫 필수 서비스의 이름(모두 있으면 nullptr)입니다.
     */
    const utf8* rebindEngineServices( const sw::ISubsystemLocator& subsystems );

    /**
     * @class ScopedEngineServiceOverride
     * @brief 이 범위 동안 엔진 서비스 몇 개를 갈아 끼우고, 나갈 때 원래 범위로 되묶습니다(단언이 일찍 나가도).
     * @details 지금 묶인 범위를 부모로 둔 컬렉션에 넣은 서브시스템이 같은 이름의 부모 것을 가린다. 넣은 것은 초기화하지 않는다(초기화가 필요한 테스트는
     *          만든 쪽이 한다). 서브시스템이 아닌 행(타입 레지스트리 · 엔진 기본 에셋)은 그것을 소유한 서브시스템을 넣고 그 객체를 `bindObject` 로 건다.
     */
    class ScopedEngineServiceOverride
    {
    public:
        ScopedEngineServiceOverride();
        ~ScopedEngineServiceOverride();

        ScopedEngineServiceOverride( const ScopedEngineServiceOverride& )            = delete;
        ScopedEngineServiceOverride& operator=( const ScopedEngineServiceOverride& ) = delete;

        /** @brief @p subsystem 을 갈아 끼울 것으로 넣습니다. 묶는 것은 `bind` 입니다. */
        template <class TSubsystem>
        TSubsystem& add( sw::unique_ptr<TSubsystem> subsystem )
        {
            return _overrides.addSubsystem( std::move( subsystem ) );
        }
        /** @brief 서브시스템이 아닌 행(그것을 소유한 서브시스템 안의 객체)을 그 타입으로 겁니다. 이미 걸려 있으면 false 입니다. */
        template <class T>
        [[nodiscard]] bool bindObject( T* pObject )
        {
            return _overrides.bind( pObject );
        }
        /** @brief 넣은 것으로 다시 묶습니다. 비어 있는 첫 필수 서비스의 이름(모두 있으면 nullptr)입니다. */
        const utf8* bind();

    private:
        const sw::ISubsystemLocator* _pSaved;    ///< 들어올 때 묶여 있던 범위
        sw::SubsystemScope           _overrides; ///< 갈아 끼울 것(부모는 `_pSaved`)
    };
} // namespace test

namespace test
{
    /** @class ScopedUnboundEngineServices @brief 이 범위 동안 엔진 서비스를 풀고, 나갈 때 원래 범위로 되묶습니다(단언이 일찍 나가도). */
    class ScopedUnboundEngineServices
    {
    public:
        ScopedUnboundEngineServices();
        ~ScopedUnboundEngineServices();

        ScopedUnboundEngineServices( const ScopedUnboundEngineServices& )            = delete;
        ScopedUnboundEngineServices& operator=( const ScopedUnboundEngineServices& ) = delete;

    private:
        const sw::ISubsystemLocator* _pSaved; ///< 들어올 때 묶여 있던 범위
    };
} // namespace test
