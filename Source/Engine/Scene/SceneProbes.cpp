/**
 * @file SceneProbes.cpp
 * @brief 씬의 자동화 탐침입니다(`Scene.*`). 탐침 레지스트리(`Engine/Probe`)가 Scene 보다 아래라 씬이 자기 탐침을 등록합니다 — 실행기가 대신 정의하지 않습니다.
 */
#include "pch.h"

#include "Core/Common/HashUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Probe/AutomationProbe.h"
#include "Engine/Scene/SceneManager.h"

namespace sw
{
    SW_LOG_CALLER( "Automation" );

    /** @brief 결정성이 깨졌을 때 원인 오브젝트를 찾는 진단 스위치입니다. 1 이면 `Scene.TransformHash` 를 읽을 때 오브젝트마다 이름과 위치를 로그로 남깁니다(두 실행의 로그를 비교한다). */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_automationHashDump, 0, "Scene.TransformHash 를 읽을 때 오브젝트마다 이름 · 위치를 로그로 (1=켜기, 결정성 진단)" );

    namespace
    {
        /** @brief `Scene.TransformHash` 가 위치를 자르는 단위(1 mm)입니다. */
        constexpr float32 kHashUnitsPerMeter = 1000.0f;

        /** @brief 탐침 `Scene.ObjectCount` 입니다. 활성 씬의 오브젝트 수입니다(핫 리로드 · 백엔드 교체 앞뒤로 씬이 그대로인지 볼 때 씁니다). */
        [[nodiscard]] bool readSceneObjectCount( const GameObjectManager* pManager, float64& outValue )
        {
            if ( pManager == nullptr )
                return false;
            uint32 count = 0;
            pManager->forEachGameObject( [&count]( GameObject* /*pObject*/ )
            { ++count; } );
            outValue = static_cast<float64>( count );
            return true;
        }

        /**
         * @brief 탐침 `Scene.Transitioning` 입니다. 비동기 씬 로드나 전환이 진행 중이면 1 입니다.
         * @details 씬 열기(`scene.open`)는 요청만 하고 돌아온다. 이미 같은 상태의 씬이 열려 있으면 다른 탐침에 차이가 나타나지 않으므로, 열기가 끝났는지는 이 탐침이 0 인 것으로 본다.
         */
        [[nodiscard]] bool readSceneTransitioning( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            if ( engine::areEngineServicesBound() == false )
                return false;
            outValue = engine::getSubsystem<SceneManager>().isTransitioning() ? 1.0 : 0.0;
            return true;
        }

        /**
         * @brief 탐침 `Scene.TransformHash` 입니다. 활성 씬 오브젝트마다 (이름, 1 mm 로 자른 월드 위치)를 해시해 더한 32 비트 값입니다.
         * @details 오브젝트 순서 · id 와 무관하다(합). 같은 시나리오를 같은 고정 틱으로 돌리면 백엔드와 상관없이 같아야 한다 — 게임 상태는 CPU 에서만 정해진다.
         *          32 비트라 `float64` 에 정확히 담기고 `equals` · `<Record>` 로 견줄 수 있다.
         */
        [[nodiscard]] bool readSceneTransformHash( const GameObjectManager* pManager, float64& outValue )
        {
            if ( pManager == nullptr )
                return false;
            uint32 sum = 0;
            pManager->forEachGameObject( [&sum]( GameObject* pObject )
            {
                const SceneComponent* pScene   = pObject->getPrimarySceneComponent();
                const float3          position = pScene != nullptr ? pScene->getWorldPosition() : float3{};
                uint64                hash     = static_cast<uint64>( pObject->getName().getHash() );
                hash                           = HashUtil::combine( hash, static_cast<uint64>( static_cast<int64>( MathUtil::round( position._x * kHashUnitsPerMeter ) ) ) );
                hash                           = HashUtil::combine( hash, static_cast<uint64>( static_cast<int64>( MathUtil::round( position._y * kHashUnitsPerMeter ) ) ) );
                hash                           = HashUtil::combine( hash, static_cast<uint64>( static_cast<int64>( MathUtil::round( position._z * kHashUnitsPerMeter ) ) ) );
                sum += static_cast<uint32>( HashUtil::mix64( hash ) );
                if ( gv_automationHashDump != 0 )
                    SW_LOG_INFO( "[Scenario] hash item %# %# %# %#", pObject->getName().c_str(), position._x, position._y, position._z );
            } );
            outValue = static_cast<float64>( sum );
            return true;
        }
    } // namespace

    SW_AUTOMATION_PROBE( sceneObjectCount, "Scene.ObjectCount", "Objects in the active scene", &readSceneObjectCount );
    SW_AUTOMATION_PROBE( sceneTransitioning, "Scene.Transitioning", "1 while an asynchronous scene load or transition is in flight", &readSceneTransitioning );
    SW_AUTOMATION_PROBE( sceneTransformHash, "Scene.TransformHash", "Order-independent hash of every object's name and world position (1 mm) - equal across backends",
                         &readSceneTransformHash );
} // namespace sw
