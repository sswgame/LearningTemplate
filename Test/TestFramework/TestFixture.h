/**
 * @file Test/TestFramework/TestFixture.h
 * @brief 테스트 본문에서 사용하는 엔진 접근 및 정리 도우미
 */
#pragma once
#include "TestFramework/TestContext.h"

namespace sw
{
    class EventDispatcher;
    class GameObjectManager;
    class Scene;
} // namespace sw

namespace test
{
    class TestFixture
    {
    public:
        explicit TestFixture( TestContext& context );
        ~TestFixture() = default;

        sw::Scene*             getActiveScene() const;
        sw::GameObjectManager* getObjectManager() const;
        sw::EventDispatcher&   getEventDispatcher() const;
        void                   deferCleanup( sw::Delegate<void()> cleanup ) const;

    private:
        TestContext*           _pContext{ nullptr };
        sw::GameObjectManager* _pObjectManager{ nullptr };
        sw::vector<uint64>     _listBaselineObjectId;
    };
} // namespace test
