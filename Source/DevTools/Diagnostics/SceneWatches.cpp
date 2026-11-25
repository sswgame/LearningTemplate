/**
 * @file SceneWatches.cpp
 * @brief 씬의 자동화 감시 값입니다(`Scene.*`: 오브젝트 수 · 올라 있는 스트리밍 셀 수 · 로드 큐 · HLOD 대리 · 전환 중 · 트랜스폼 해시 · 게임 카메라 요). 감시 값 레지스트리(`Engine/Automation/Watch`)가 Scene 보다 아래라 실행기가 대신 정의하지 않고 씬이 자기 감시 값을 등록합니다.
 * @details 감시 값과 결정성 진단 스위치(`gv_automationHashDump`)만 든 개발 장치라 DevTools 에 둔다 — 배포 구성은 이 파일을 짓지 않는다(`SW_DEV_ONLY_SOURCE_PATTERNS`).
 */
#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "DevTools/Diagnostics/DevSceneIterationAccess.h"
#include "DevTools/Diagnostics/DevSceneStreamingAccess.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Scene/SceneStreamingSubsystem.h"
#include "Engine/Scene/SceneSubLevelSubsystem.h"

namespace sw
{
    /** @brief 결정성이 깨졌을 때 원인 오브젝트를 찾는 진단 스위치입니다. 1 이면 `Scene.TransformHash` 를 읽을 때 오브젝트마다 이름과 위치를 로그로 남깁니다(두 실행의 로그를 비교한다). */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_automationHashDump, 0, "Scene.TransformHash 를 읽을 때 오브젝트마다 이름 · 위치를 로그로 (1=켜기, 결정성 진단)" );

    namespace
    {
        /** @brief `Scene.TransformHash` 가 위치를 자르는 단위(1 mm)입니다. */
        constexpr float32 kHashUnitsPerMeter = 1000.0f;

        /** @brief 감시 값 `Scene.ObjectCount` 입니다. 활성 씬의 오브젝트 수입니다(핫 리로드 · 백엔드 교체 앞뒤로 씬이 그대로인지 볼 때 씁니다). */
        [[nodiscard]] bool readSceneObjectCount( const GameObjectManager* pManager, float64& outValue )
        {
            if ( pManager == nullptr )
                return false;
            uint32 count = 0;
            pManager->forEachGameObject( DevSceneIterationAccess::getKey(), [&count]( const GameObject* /*pObject*/ )
            { ++count; } );
            outValue = static_cast<float64>( count );
            return true;
        }

        /**
         * @brief 감시 값 `Scene.LoadedCellCount` 입니다. 활성 씬의 스트리밍 셀 조각 가운데 올라 있는(다 지어 드러난) 수입니다 — 셀을 스트리밍하지 않으면 0 입니다.
         * @details 셀 경계 시나리오가 원점을 옮긴 뒤 셀이 오르고 내렸는지를 기다린다(`SceneStreamingSubsystem::getLoadedCellCount`).
         */
        [[nodiscard]] bool readSceneLoadedCellCount( const GameObjectManager* pManager, float64& outValue )
        {
            if ( pManager == nullptr )
                return false;
            outValue = static_cast<float64>( pManager->getSubsystem<SceneStreamingSubsystem>().getLoadedCellCount() );
            return true;
        }

        /**
         * @brief 감시 값 `Scene.SubLevelCount` 입니다. 활성 씬의 런타임 하위 레벨 수(짓는 중 · 빼는 중 포함)입니다(`SceneSubLevelSubsystem`).
         */
        [[nodiscard]] bool readSceneSubLevelCount( const GameObjectManager* pManager, float64& outValue )
        {
            if ( pManager == nullptr )
                return false;
            outValue = static_cast<float64>( pManager->getSubsystem<SceneSubLevelSubsystem>().getSubLevelCount() );
            return true;
        }

        /**
         * @brief 감시 값 `Scene.LoadedSubLevelCount` 입니다. 활성 씬의 런타임 하위 레벨 가운데 다 지어 드러난 수입니다 — 하위 레벨 시나리오가 더한 뒤 오르기를 기다린다.
         */
        [[nodiscard]] bool readSceneLoadedSubLevelCount( const GameObjectManager* pManager, float64& outValue )
        {
            if ( pManager == nullptr )
                return false;
            outValue = static_cast<float64>( pManager->getSubsystem<SceneSubLevelSubsystem>().getLoadedSubLevelCount() );
            return true;
        }

        /** @brief 감시 값 `Scene.WorldOriginX` · `Scene.WorldOriginZ` 입니다. 활성 씬의 월드 원점(절대 = 로컬 + 원점)입니다 — 원점 이동 시나리오가 옮긴 뒤 본다. */
        [[nodiscard]] bool readSceneWorldOrigin( bool bZ, float64& outValue )
        {
            if ( engine::areEngineServicesBound() == false )
                return false;
            const Scene* pScene = engine::getSubsystem<SceneManager>().getActiveScene();
            if ( pScene == nullptr )
                return false;
            outValue = bZ ? pScene->getWorldOrigin()._z : pScene->getWorldOrigin()._x;
            return true;
        }
        [[nodiscard]] bool readSceneWorldOriginX( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            return readSceneWorldOrigin( false, outValue );
        }
        [[nodiscard]] bool readSceneWorldOriginZ( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            return readSceneWorldOrigin( true, outValue );
        }

        /** @brief 감시 값 `Scene.PendingCellCount` 입니다. 원점 반경 안이라 올리려는데 아직 오르지 않은 셀 조각 수(로드 큐 — `DevSceneStreamingAccess::getPendingCellCount`)입니다. */
        [[nodiscard]] bool readScenePendingCellCount( const GameObjectManager* pManager, float64& outValue )
        {
            if ( pManager == nullptr )
                return false;
            outValue = static_cast<float64>( DevSceneStreamingAccess::getPendingCellCount( pManager->getSubsystem<SceneStreamingSubsystem>() ) );
            return true;
        }

        /** @brief 감시 값 `Scene.HLODCount` 입니다. HLOD 대리가 있는 셀 조각 수입니다(쿠커 · 소스 셀 나누기가 정적 메시로 굽는다). */
        [[nodiscard]] bool readSceneHLODCount( const GameObjectManager* pManager, float64& outValue )
        {
            if ( pManager == nullptr )
                return false;
            outValue = static_cast<float64>( DevSceneStreamingAccess::getHLODCount( pManager->getSubsystem<SceneStreamingSubsystem>() ) );
            return true;
        }

        /** @brief 감시 값 `Scene.HLODShownCount` 입니다. 셀 조각이 올라 있지 않아 대신 그리는(켜진) HLOD 대리 수입니다. */
        [[nodiscard]] bool readSceneHLODShownCount( const GameObjectManager* pManager, float64& outValue )
        {
            if ( pManager == nullptr )
                return false;
            outValue = static_cast<float64>( DevSceneStreamingAccess::getShownHLODCount( pManager->getSubsystem<SceneStreamingSubsystem>() ) );
            return true;
        }

        /**
         * @brief 감시 값 `Scene.Transitioning` 입니다. 비동기 씬 로드나 전환이 진행 중이면 1 입니다.
         * @details 씬 열기(`scene.open`)는 요청만 하고 돌아온다. 이미 같은 상태의 씬이 열려 있으면 다른 감시 값에 차이가 나타나지 않으므로, 열기가 끝났는지는 이 감시 값이 0 인 것으로 본다.
         */
        [[nodiscard]] bool readSceneTransitioning( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            if ( engine::areEngineServicesBound() == false )
                return false;
            outValue = engine::getSubsystem<SceneManager>().isTransitioning() ? 1.0 : 0.0;
            return true;
        }

        /**
         * @brief 감시 값 `Scene.CameraYaw` 입니다. 활성 씬의 게임 카메라가 보는 방향의 요(도, 월드 +Z 가 0, +X 쪽이 양수)입니다.
         * @details 마우스 시선 시나리오(커서 잠금 중 원시 입력)가 시선이 실제로 돌았는지 본다 — 입력 수만으로는 게임 카메라에 닿았는지 모른다. 카메라가 없으면 읽지 못한다.
         */
        [[nodiscard]] bool readSceneCameraYaw( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            if ( engine::areEngineServicesBound() == false )
                return false;
            const Scene*           pScene  = engine::getSubsystem<SceneManager>().getActiveScene();
            const CameraComponent* pCamera = pScene != nullptr ? pScene->getActiveGameCamera() : nullptr;
            if ( pCamera == nullptr )
                return false;
            const float3 forward = pCamera->getCameraForward();
            outValue             = static_cast<float64>( MathUtil::toDegree( MathUtil::atan2( forward._x, forward._z ) ) );
            return true;
        }

        /**
         * @brief 감시 값 `Scene.TransformHash` 입니다. 활성 씬 오브젝트마다 (이름, 1 mm 로 자른 월드 위치)를 해시해 더한 32 비트 값입니다.
         * @details 오브젝트 순서 · id 와 무관하다(합). 같은 시나리오를 같은 고정 틱으로 돌리면 백엔드와 상관없이 같아야 한다 — 게임 상태는 CPU 에서만 정해진다.
         *          32 비트라 `float64` 에 정확히 담기고 `equals` · `<Record>` 로 비교할 수 있다.
         */
        [[nodiscard]] bool readSceneTransformHash( const GameObjectManager* pManager, float64& outValue )
        {
            if ( pManager == nullptr )
                return false;
            uint32 sum = 0;
            pManager->forEachGameObject( DevSceneIterationAccess::getKey(), [&sum]( const GameObject* pObject )
            {
                const SceneComponent* pScene   = pObject->getPrimarySceneComponent();
                const float3          position = pScene != nullptr ? pScene->getWorldPosition() : float3{};
                uint64                hash     = static_cast<uint64>( pObject->getName().getHash() );
                hash                           = HashUtil::combine( hash, static_cast<uint64>( static_cast<int64>( MathUtil::round( position._x * kHashUnitsPerMeter ) ) ) );
                hash                           = HashUtil::combine( hash, static_cast<uint64>( static_cast<int64>( MathUtil::round( position._y * kHashUnitsPerMeter ) ) ) );
                hash                           = HashUtil::combine( hash, static_cast<uint64>( static_cast<int64>( MathUtil::round( position._z * kHashUnitsPerMeter ) ) ) );
                sum += static_cast<uint32>( HashUtil::mix64( hash ) );
                if ( gv_automationHashDump != 0 )
                    SW_LOG_INFO_ID( "Automation.HashItem", "[Scenario] hash item %# %# %# %#", pObject->getName().c_str(), position._x, position._y, position._z );
            } );
            outValue = static_cast<float64>( sum );
            return true;
        }
    } // namespace

    SW_AUTOMATION_WATCH( sceneObjectCount, "Scene.ObjectCount", "Objects in the active scene", &readSceneObjectCount );
    SW_AUTOMATION_WATCH( sceneLoadedCellCount, "Scene.LoadedCellCount", "Streaming cell fragments of the active scene that are loaded (built and shown) - 0 when it does not stream cells",
                         &readSceneLoadedCellCount );
    SW_AUTOMATION_WATCH( scenePendingCellCount, "Scene.PendingCellCount", "Streaming cell fragments within a source radius that are not loaded yet (the load queue; world.streamqueue lists them)",
                         &readScenePendingCellCount );
    SW_AUTOMATION_WATCH( sceneHLODCount, "Scene.HLODCount", "Streaming cell fragments that have an HLOD proxy (static meshes merged into instanced meshes at cook time)", &readSceneHLODCount );
    SW_AUTOMATION_WATCH( sceneHLODShownCount, "Scene.HLODShownCount", "HLOD proxies drawn in place of a cell fragment that is not loaded", &readSceneHLODShownCount );
    SW_AUTOMATION_WATCH( sceneSubLevelCount, "Scene.SubLevelCount", "Runtime sub-levels of the active scene, including ones still building or being removed", &readSceneSubLevelCount );
    SW_AUTOMATION_WATCH( sceneLoadedSubLevelCount, "Scene.LoadedSubLevelCount", "Runtime sub-levels of the active scene that are built and shown", &readSceneLoadedSubLevelCount );
    SW_AUTOMATION_WATCH( sceneWorldOriginX, "Scene.WorldOriginX", "Absolute X of the active scene's world origin (m) - object positions are local to it", &readSceneWorldOriginX );
    SW_AUTOMATION_WATCH( sceneWorldOriginZ, "Scene.WorldOriginZ", "Absolute Z of the active scene's world origin (m) - object positions are local to it", &readSceneWorldOriginZ );
    SW_AUTOMATION_WATCH( sceneCameraYaw, "Scene.CameraYaw", "Yaw (degrees) the active scene's game camera looks along (world +Z is 0, toward +X is positive)", &readSceneCameraYaw );
    // 활동 수 없음: 비동기 씬 로드는 워커 잡 하나가 다 지은 씬을 끝에 넘긴다 — 프리로드 진행(`SceneManager::tryGetLoadProgress`)은 쿠킹한 프리로드 목록이 있을 때만 있어 Dev 실행에서는 낼 수 없다.
    SW_AUTOMATION_WALL_WATCH( sceneTransitioning, "Scene.Transitioning", "1 while an asynchronous scene load or transition is in flight", &readSceneTransitioning, nullptr );
    SW_AUTOMATION_WATCH( sceneTransformHash, "Scene.TransformHash", "Order-independent hash of every object's name and world position (1 mm) - equal across backends",
                         &readSceneTransformHash );
} // namespace sw
