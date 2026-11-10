#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Module/ModuleEntryRegistrar.h"

using namespace sw;

namespace
{
    struct ModuleEntryRegistrarTestUtil
    {
        /** @brief 첫 등록만 받고 뺀 횟수를 세는 레지스트리입니다. */
        struct FakeRegistry
        {
            int32 _addCount{ 0 };
            int32 _removeCount{ 0 };
            int32 _acceptCount{ 1 };

            [[nodiscard]] bool registerEntry( const int32* )
            {
                ++_addCount;
                return _addCount <= _acceptCount;
            }
            void unregisterEntry( const int32* ) { ++_removeCount; }
        };

        /** @brief 레지스트리 하나만 찾아 주는 범위입니다. */
        struct FakeLocator final : ISubsystemLocator
        {
            FakeRegistry* _pRegistry{ nullptr };

            void*                    findInScope( const uint64 typeHash ) const override { return typeHash == getSubsystemTypeHash<FakeRegistry>() ? _pRegistry : nullptr; }
            const ISubsystemLocator* getParentScope() const override { return nullptr; }
        };

        /** @brief 만들 때 링크 목록에 달린 지역 등록자를 목록에서 다시 빼, 스택 객체가 목록에 남지 않게 합니다. */
        struct HeadGuard
        {
            RegistrationChain<ModuleObjectRegistrar>& _chain{ ModuleObjectRegistrar::getChain() };
            ModuleObjectRegistrar*                    _pSaved{ _chain.getHead() };

            ~HeadGuard() { _chain.resetHead( _pSaved ); }
        };
    };
} // namespace

/** @brief [ModuleEntryRegistrar] 받아들인 줄은 detach 가 한 번만 뺀다. */
SW_TEST_CASE( ModuleEntryRegistrar, DetachRemovesAcceptedEntryOnce )
{
    using Util = ModuleEntryRegistrarTestUtil;
    Util::HeadGuard    guard;
    Util::FakeRegistry registry;
    Util::FakeLocator  locator;
    locator._pRegistry = &registry;
    const int32 entry  = 0;

    ModuleEntryRegistrar<Util::FakeRegistry, const int32*> registrar{ &entry };
    registrar.attach( locator );
    registrar.detach( locator );
    registrar.detach( locator );

    SW_EXPECT_EQUAL( 1, registry._addCount );
    SW_EXPECT_EQUAL( 1, registry._removeCount );
}

/** @brief [ModuleEntryRegistrar] 거절된 줄은 빼지 않는다 — 먼저 오른 같은 이름의 줄을 건드리면 안 된다. */
SW_TEST_CASE( ModuleEntryRegistrar, DetachSkipsRefusedEntry )
{
    using Util = ModuleEntryRegistrarTestUtil;
    Util::HeadGuard    guard;
    Util::FakeRegistry registry;
    registry._acceptCount = 0;
    Util::FakeLocator locator;
    locator._pRegistry = &registry;
    const int32 entry  = 0;

    ModuleEntryRegistrar<Util::FakeRegistry, const int32*> registrar{ &entry };
    registrar.attach( locator );
    registrar.detach( locator );

    SW_EXPECT_EQUAL( 1, registry._addCount );
    SW_EXPECT_EQUAL( 0, registry._removeCount );
}
