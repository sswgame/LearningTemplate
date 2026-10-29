/**
 * @file Engine/Module/ModuleSubsystemRegistrar.h
 * @brief 모듈 이미지가 범위가 서기 전에(정적 초기화) 미리 적어 두는 서브시스템 등록입니다 — 모듈을 올릴 때 그 모듈의 자식 범위에 서고, 내리기 전에 함께 걷힙니다.
 * @details 언리얼은 모듈이 올라오면 그 모듈의 `UEngineSubsystem` · `UGameInstanceSubsystem` 클래스를 훑어 컬렉션에 더하고, 내릴 때 뺀다. 여기서는 클래스 순회 대신
 *          링크 때 목록(`getHead`)에 달린 등록자를 모듈을 올린 쪽(`engine::registerModuleTypes`)이 걷어 `ModuleSubsystemHost` 에 넘긴다 — 타입 · 데이터 종류 ·
 *          모듈 객체 등록자와 같은 사슬(`ModuleRegistrarHeads`)이다. 서브시스템은 고른 범위(`ModuleSubsystemScope`) 아래 모듈 이름의 **자식 범위**에 서고,
 *          등록이 공개한 타입(`SubsystemScope::publish`)만 그 범위의 조회에 보인다. 그래서 낮은 층(엔진)이 높은 층(GameFramework)의 구현을 함수 칸 · 정적 객체 없이
 *          인터페이스로 찾는다. Godot 의 엔진 싱글턴 등록(`Engine::add_singleton`)과 달리 이름이 아니라 타입으로 찾고, 모듈이 내려가면 자식 범위째 사라져 남는 칸이 없다.
 *          스레드: 모듈을 올리고 내리는 스레드만 부른다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/SubsystemScope.h"

namespace sw
{
    /** @brief 모듈 서브시스템이 설 범위입니다. 모듈 이름의 자식 범위가 이 범위에 매달린다. */
    enum class ModuleSubsystemScope : uint8
    {
        Process, ///< 프로세스 범위 — 범위 사슬의 뿌리(호스트가 넘긴 호스트 범위, 호스트 범위가 없는 실행은 코어 범위). 엔진 기동 단계 · 게임 인스턴스를 다시 세워도 남는다
        Engine,  ///< 엔진 범위(기동 단계) — 엔진 기동 단계와 함께 내려간다
    };

    /**
     * @class ModuleSubsystemRegistrar
     * @brief 이미지의 정적 객체로 두면 링크 때 목록(`getHead`)에 달리고, 모듈을 올린 쪽이 걷어 자식 범위에 서브시스템을 더합니다(`addTo`).
     * @details 직접 쓰지 말고 `SW_MODULE_SUBSYSTEM` 을 쓴다. 등록자는 이미지와 수명이 같고, 서브시스템은 자식 범위가 소유한다.
     */
    class SW_API ModuleSubsystemRegistrar
    {
    public:
        /** @brief 아직 아무도 걷어 가지 않은 등록자들의 목록 머리입니다(Engine 이미지가 가집니다). 걷어 간 쪽이 nullptr 로 비웁니다. 나중에 만든 것이 앞입니다. */
        static ModuleSubsystemRegistrar*& getHead();

        ModuleSubsystemRegistrar( const ModuleSubsystemRegistrar& )            = delete;
        ModuleSubsystemRegistrar& operator=( const ModuleSubsystemRegistrar& ) = delete;

        /** @brief 목록의 다음(먼저 만든 쪽)입니다. */
        ModuleSubsystemRegistrar* getNext() const { return _pNext; }
        /** @brief 서브시스템이 설 범위입니다. */
        ModuleSubsystemScope getTargetScope() const { return _targetScope; }
        /** @brief 서브시스템을 만들어 모듈의 자식 범위 @p moduleScope 에 더하고, 등록이 고른 타입으로 공개합니다. 초기화는 부른 쪽이 범위째 한다. */
        virtual void addTo( SubsystemScope& moduleScope ) const = 0;

    protected:
        explicit ModuleSubsystemRegistrar( ModuleSubsystemScope targetScope );
        ~ModuleSubsystemRegistrar() = default;

    private:
        ModuleSubsystemRegistrar* _pNext;
        ModuleSubsystemScope      _targetScope;
    };

    /**
     * @class ModuleSubsystemRegistration
     * @brief 서브시스템 @p TSubsystem 하나의 등록입니다. 기본 생성하고 @p TPublished 타입마다 공개합니다(제 타입을 공개하려면 목록에 넣는다).
     */
    template <class TSubsystem, class... TPublished>
    class ModuleSubsystemRegistration final : public ModuleSubsystemRegistrar
    {
    public:
        explicit ModuleSubsystemRegistration( ModuleSubsystemScope targetScope )
            : ModuleSubsystemRegistrar{ targetScope }
        {
        }

        void addTo( SubsystemScope& moduleScope ) const override
        {
            TSubsystem& subsystem  = moduleScope.addSubsystem( make_unique<TSubsystem>() );
            const bool  bPublished = ( moduleScope.template publish<TPublished>( subsystem ) && ... );
            SW_ASSERT( bPublished ); // 한 모듈 범위에서 같은 타입을 둘이 공개하면 등록의 잘못이다
            (void)bPublished;
        }
    };
} // namespace sw

/**
 * @brief 서브시스템 @p TSubsystem 을 이 이미지의 모듈 서브시스템으로 등록합니다. @p targetScope 는 `ModuleSubsystemScope` 의 열거자 이름, 나머지는 공개할 타입입니다.
 * @details 예: `SW_MODULE_SUBSYSTEM( Process, OpenSSLNetSecurityProvider, INetSecurityProvider );` — 프로세스 범위에 서고 `INetSecurityProvider` 로 찾힌다.
 *          .cpp 의 전역 자리(이름공간 밖)에 둔다. 유니티 빌드가 여러 .cpp 를 합쳐도 겹치지 않게 변수 이름에 타입 이름을 붙인다.
 */
#define SW_MODULE_SUBSYSTEM( targetScope, TSubsystem, ... )                                                       \
    static const ::sw::ModuleSubsystemRegistration<::sw::TSubsystem, __VA_ARGS__> sw_moduleSubsystem_##TSubsystem \
    {                                                                                                             \
        ::sw::ModuleSubsystemScope::targetScope                                                                   \
    }
