/**
 * @file Engine/Module/ModuleRegistrationHost.h
 * @brief 모듈마다의 등록 상태(타입 등록자 머리 캐시 · 붙인 모듈 객체 등록자 · 모듈 서브시스템의 자식 범위)를 가진 서브시스템, 그리고 모듈이 공개한 것만 보는 조회 창구입니다.
 * @details 엔진이 서비스 범위에 둔다(`Engine::initialize`). 모듈을 올리고 내리는 길(`ModuleTypeRegistry.cpp`)의 상태는 모두 여기 있다 — 예전에는 그 파일의
 *          `ScopeOwned` 정적 맵 둘이 들었다(소유자 없는 엔진 수명 값 → 서비스 범위의 서브시스템이 소유). 모듈을 올리는 길(`engine::registerModuleTypes`)이 그 모듈의 등록자 사슬을 넘기면 고른 범위
 *          (프로세스 · 엔진)에 모듈 이름의 자식 범위를 매달아 서브시스템을 세우고, 내리는 길(`engine::unregisterModuleTypes`)이 그 자식 범위째 걷는다.
 *          Shipping 은 모듈을 내리지 않는다 — 자식 범위는 그것을 매단 범위가 사라질 때 함께 사라진다(`SubsystemScope::destroyAll` 이 자식을 먼저 지운다).
 *          조회 창구(`ISubsystemLocator`)는 매단 자식이 공개한 칸만 본다 — 모듈에 넘기는 서비스 범위가 이것을 부모로 두어(`GameModuleManager::fillModuleServiceScope`)
 *          게임 · 에디터 모듈이 호스트 전용 서비스는 못 보고 모듈이 공개한 구현(암호 제공자 …)만 찾게 한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Subsystem/ISubsystem.h"
#include "Core/Subsystem/SubsystemScope.h"

namespace sw
{
    class ModuleObjectRegistrar;
    class ModuleSubsystemRegistrar;
    struct EnumRegistrar;
    struct TypeRegistrar;

    /**
     * @class ModuleRegistrationHost
     * @brief 모듈 서브시스템의 자식 범위를 매달고 떼는 서브시스템이고, 매단 자식이 공개한 칸의 조회 창구(`getMountedLocator`)를 가집니다. 스레드: 모듈을 올리고 내리는 스레드만 부른다.
     */
    class SW_API ModuleRegistrationHost final : public ISubsystem
    {
    public:
        /** @brief 서브시스템 이름입니다. */
        static constexpr const utf8* kSubsystemName = "ModuleRegistrationHost";

        /**
         * @param processScope 프로세스 범위 — 범위 사슬의 뿌리입니다(엔진 기동 단계보다 오래 산다).
         * @param engineScope 엔진 범위(기동 단계)입니다.
         * @param hostMask 자식 범위의 서브시스템을 세울 호스트 종류입니다(엔진 범위와 같다).
         */
        ModuleRegistrationHost( SubsystemScope& processScope, SubsystemScope& engineScope, SubsystemHostMask hostMask );

        /**
         * @brief @p pHead 사슬의 등록자를 만든 순서로 걷어, 고른 범위마다 모듈 @p moduleName 의 자식 범위를 매달고 그 서브시스템을 세웁니다.
         * @return 자식 범위의 초기화가 실패했거나 같은 모듈의 자식 범위가 이미 있으면 false 입니다(오류를 남긴다 — 그 서브시스템을 찾는 쪽은 없다는 오류로 멈춘다).
         */
        [[nodiscard]] bool mountModule( string_view moduleName, const ModuleSubsystemRegistrar* pHead );
        /** @brief 모듈 @p moduleName 의 자식 범위를 모든 범위에서 떼고 지웁니다(그 서브시스템을 종료 · 역순 소멸). 없으면 아무것도 하지 않습니다. */
        void unmountModule( string_view moduleName );

        /** @brief 모듈 하나가 마지막으로 걷은 타입 · 열거형 등록자 머리입니다(핫 리로드가 같은 모듈을 다시 올릴 때 쓴다). */
        struct HeadRecord
        {
            TypeRegistrar* _pTypeHead{ nullptr };
            EnumRegistrar* _pEnumHead{ nullptr };
        };
        /** @brief 모듈 이름 → 마지막으로 걷은 등록자 머리입니다. */
        unordered_map<string, HeadRecord>& getHeadCache() { return _mapHead; }
        /** @brief 모듈 이름 → 붙인 모듈 객체 등록자(붙인 순서)입니다. 내리기 전에 떼고 지운다. */
        unordered_map<string, vector<ModuleObjectRegistrar*>>& getAttachedObjects() { return _mapAttachedObject; }

        // ISubsystem — 세울 것이 없다(자식 범위는 매단 범위가 소유한다).
        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /** @brief 매단 자식이 공개한 칸만 보는 조회 창구입니다(모듈 범위의 부모 — `GameModuleManager::fillModuleServiceScope`). 수명은 이 서브시스템과 같다. */
        const ISubsystemLocator& getMountedLocator() const { return _mountedLocator; }

    private:
        /**
         * @class MountedLocator
         * @brief 매단 자식이 공개한 칸만 보는 `ISubsystemLocator` 입니다(상위 범위 없음). 호스트가 그 인터페이스를 상속하지 않고 이 멤버를 내놓는다.
         */
        class MountedLocator final : public ISubsystemLocator
        {
        public:
            explicit MountedLocator( const ModuleRegistrationHost& owner )
                : _owner{ owner }
            {
            }

            void*                    findInScope( uint64 typeHash ) const override { return _owner.findInMounted( typeHash ); }
            const ISubsystemLocator* getParentScope() const override { return nullptr; }

        private:
            const ModuleRegistrationHost& _owner;
        };

    private:
        /** @brief @p scope 의 범위입니다. */
        SubsystemScope& resolveScope( uint8 scope ) const;
        /** @brief 매단 자식이 공개한 칸에서 @p typeHash 를 찾습니다(프로세스 범위의 자식이 먼저). 없으면 nullptr 입니다. */
        void* findInMounted( uint64 typeHash ) const;

    private:
        SubsystemScope*                                       _pProcessScope;     ///< 프로세스 범위(엔진보다 오래 산다)
        SubsystemScope*                                       _pEngineScope;      ///< 엔진 범위(엔진 기동 단계)
        SubsystemHostMask                                     _hostMask;          ///< 자식 서브시스템을 세울 호스트 종류
        unordered_map<string, HeadRecord>                     _mapHead;           ///< 모듈 이름 → 마지막으로 걷은 등록자 머리
        unordered_map<string, vector<ModuleObjectRegistrar*>> _mapAttachedObject; ///< 모듈 이름 → 붙인 모듈 객체 등록자
        MountedLocator                                        _mountedLocator;    ///< 매단 자식이 공개한 칸의 조회 창구
    };
} // namespace sw
