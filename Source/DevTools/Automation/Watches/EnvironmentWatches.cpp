/**
 * @file EnvironmentWatches.cpp
 * @brief 환경 계통(표면 발자국 · 변형 · 물 · 부력 · 배 · 지형 충돌 · 수풀 영향 · 바람)의 자동화 감시 값과 개발 명령입니다.
 * @details 기능 .cpp 끝에 있던 등록 블록을 그대로 옮겼다 — 배포 엔진에 컴파일되지 않게 개발 도구가 짓는다. 본문은 기능 형식의 공개 조회만 읽는다.
 */
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/BuildInfo.h"
#include "Core/Foundation/Types.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/LogOnce.h"
#include "Core/Log/Logger.h"
#include "Core/Math/AABB.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Task/TaskManager.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Environment/EnvironmentUtil.h"
#include "Engine/Environment/Foliage/FoliageComponent.h"
#include "Engine/Environment/Foliage/FoliageInfluencerComponent.h"
#include "Engine/Environment/SceneEnvironment.h"
#include "Engine/Environment/Surface/GroundSurfaceComponent.h"
#include "Engine/Environment/Surface/SurfaceDeformSystem.h"
#include "Engine/Environment/Surface/SurfaceFootprintSystem.h"
#include "Engine/Environment/Surface/SurfaceKind.h"
#include "Engine/Environment/Surface/SurfaceSample.h"
#include "Engine/Environment/Terrain/TerrainColliderComponent.h"
#include "Engine/Environment/Terrain/TerrainComponent.h"
#include "Engine/Environment/Terrain/TerrainHeightfield.h"
#include "Engine/Environment/Terrain/TerrainMeshBuilder.h"
#include "Engine/Environment/Water/BoatComponent.h"
#include "Engine/Environment/Water/BuoyancyComponent.h"
#include "Engine/Environment/Water/WaterBodyComponent.h"
#include "Engine/Environment/Wind/WindComponent.h"
#include "Engine/Foundation/DeterministicRun.h"
#include "Engine/Foundation/EngineParallel.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Registry/PrimitiveRegistry.h"
#include "Engine/Object/GameObject/Subsystem/ISceneSubsystem.h"
#include "Engine/Object/GameObject/Subsystem/SceneEngineSubsystems.h"
#include "Engine/Object/GameObject/Subsystem/SceneSubsystemRegistry.h"
#include "Engine/Object/GameObject/Tick/FixedStepContext.h"
#include "Engine/Physics/Collision/PhysicsQuery.h"
#include "Engine/Physics/Collision/PhysicsShape.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/PhysicsDesc.h"
#include "Engine/Physics/PhysicsSettings.h"
#include "Engine/Physics/PhysicsSystem.h"
#include "Engine/Physics/Scene/PhysicsNameValidation.h"
#include "Engine/Physics/Scene/ScenePhysics.h"
#include "Engine/Presentation/ISoundPlayer.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Reflection/ReflectionValidation.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Visual/MeshInstanceBatch.h"

// Engine/Environment/Surface/SurfaceFootprintSystem.cpp 에서 옮긴 개발 등록(배포 엔진에 컴파일되지 않게).
namespace sw
{
    namespace
    {
        struct SurfaceFootprintSystemInternal
        {
            static const SurfaceFootprintSystem* findSystem( const AutomationWatchQuery& query )
            {
                return query._pManager != nullptr ? &query._pManager->getSubsystem<SurfaceFootprintSystem>() : nullptr;
            }

            /** @brief 감시 값 `Surface.FootprintPlaced` 입니다. 이 씬에서 찍은 발자국 수(누적)입니다. */
            [[nodiscard]] static bool readFootprintPlaced( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SurfaceFootprintSystem* pSystem = findSystem( query );
                if ( pSystem == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pSystem->getPlacedCount() ) );
                return true;
            }

            /** @brief 감시 값 `Surface.WetPrintPlaced` 입니다. 이 씬에서 찍은 젖은 자국 수(누적)입니다. */
            [[nodiscard]] static bool readWetPrintPlaced( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SurfaceFootprintSystem* pSystem = findSystem( query );
                if ( pSystem == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pSystem->getWetPrintCount() ) );
                return true;
            }

            /** @brief 감시 값이 읽을 기록입니다 — @p bTarget 이면 대상 오브젝트의 기록, 아니면 씬 전체입니다. 없으면 @p outValue 에 까닭을 적고 nullptr 입니다. */
            static const SurfaceFootstepRecord* findRecord( const AutomationWatchQuery& query, bool bTarget, AutomationWatchValue& outValue )
            {
                const SurfaceFootprintSystem* pSystem = findSystem( query );
                if ( pSystem == nullptr )
                    return nullptr;
                if ( bTarget == false )
                    return &pSystem->getSceneRecord();
                const GameObject* pTarget = query.findTargetObject( outValue );
                if ( pTarget == nullptr )
                    return nullptr;
                const SurfaceFootstepRecord* pRecord = pSystem->findObjectRecord( pTarget->getObjectID() );
                if ( pRecord == nullptr )
                    outValue.setText( "the target has not stepped yet" );
                return pRecord;
            }

            /** @brief 기록이 있고 마지막 발걸음이 있으면 그 기록입니다. 없으면 까닭을 적고 nullptr 입니다. */
            static const SurfaceFootstepRecord* findLastStep( const AutomationWatchQuery& query, bool bTarget, AutomationWatchValue& outValue )
            {
                const SurfaceFootstepRecord* pRecord = findRecord( query, bTarget, outValue );
                if ( pRecord != nullptr && pRecord->_lastKind.empty() )
                {
                    outValue.setText( "no footstep yet" );
                    return nullptr;
                }
                return pRecord;
            }

            /** @brief 반응을 볼 종류의 기록입니다 — @p bByKind 면 키의 종류, 아니면 마지막 발걸음. 없으면 까닭을 적고 nullptr 입니다. */
            static const SurfaceFootstepKindNote* findNote( const AutomationWatchQuery& query, bool bTarget, bool bByKind, AutomationWatchValue& outValue )
            {
                const SurfaceFootstepRecord* pRecord = bByKind ? findRecord( query, bTarget, outValue ) : findLastStep( query, bTarget, outValue );
                if ( pRecord == nullptr )
                    return nullptr;
                const SurfaceFootstepKindNote* pNote = bByKind ? pRecord->findKind( hashed_string( query._key ) ) : pRecord->findLast();
                if ( pNote == nullptr )
                    outValue.setText( "no footstep on that surface kind yet" );
                return pNote;
            }

            /** @brief 종류 `key` 를 밟은 발걸음 수(누적)입니다. */
            template <bool bTarget>
            [[nodiscard]] static bool readCount( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SurfaceFootstepRecord* pRecord = findRecord( query, bTarget, outValue );
                // 대상이 아직 걷지 않았으면 0 이다 — 조건 대기가 첫 걸음을 기다릴 수 있게.
                const bool bNotYet = bTarget && pRecord == nullptr && query.findTargetObject() != nullptr;
                if ( pRecord == nullptr && bNotYet == false )
                    return false;
                outValue.setNumber( static_cast<float64>( pRecord != nullptr ? pRecord->getCount( hashed_string( query._key ) ) : 0u ) );
                return true;
            }

            /** @brief 마지막 발걸음의 표면 종류 이름입니다. */
            template <bool bTarget>
            [[nodiscard]] static bool readLastKind( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SurfaceFootstepRecord* pRecord = findLastStep( query, bTarget, outValue );
                if ( pRecord == nullptr )
                    return false;
                outValue.setText( pRecord->_lastKind.c_str() );
                return true;
            }

            /** @brief 마지막 발걸음이 고른 반응("dry" · "wet")입니다. */
            template <bool bTarget>
            [[nodiscard]] static bool readLastState( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SurfaceFootstepKindNote* pNote = findNote( query, bTarget, false, outValue );
                if ( pNote == nullptr )
                    return false;
                outValue.setText( pNote->_bLastWet == SW_TRUE ? "wet" : "dry" );
                return true;
            }

            /** @brief 마지막 발걸음 반응의 소리 이벤트("none" = 조용함)입니다. */
            template <bool bTarget, bool bByKind = false>
            [[nodiscard]] static bool readLastSound( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SurfaceFootstepKindNote* pNote = findNote( query, bTarget, bByKind, outValue );
                if ( pNote == nullptr )
                    return false;
                outValue.setText( pNote->_lastSound.empty() ? "none" : pNote->_lastSound.c_str() );
                return true;
            }

            /** @brief 마지막 발걸음 반응의 효과 경로("none" = 없음)입니다. */
            template <bool bTarget, bool bByKind = false>
            [[nodiscard]] static bool readLastEffect( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SurfaceFootstepKindNote* pNote = findNote( query, bTarget, bByKind, outValue );
                if ( pNote == nullptr )
                    return false;
                outValue.setText( pNote->_lastEffect.empty() ? "none" : pNote->_lastEffect.c_str() );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( surfaceFootprintPlaced, "Surface.FootprintPlaced", "Footprints stamped on the surface deform layer in this scene (cumulative)",
                               &SurfaceFootprintSystemInternal::readFootprintPlaced, false, false );
    SW_AUTOMATION_QUERY_WATCH( surfaceWetPrintPlaced, "Surface.WetPrintPlaced", "Wet prints left by wet feet on dry ground in this scene (cumulative)",
                               &SurfaceFootprintSystemInternal::readWetPrintPlaced, false, false );
    SW_AUTOMATION_QUERY_WATCH( surfaceFootstepCount, "Surface.FootstepCount", "Footsteps on the surface kind named key (cumulative)", &SurfaceFootprintSystemInternal::readCount<false>,
                               false, true );
    SW_AUTOMATION_QUERY_WATCH( surfaceLastFootstep, "Surface.LastFootstep", "Surface kind of the last footstep", &SurfaceFootprintSystemInternal::readLastKind<false>, false, false );
    SW_AUTOMATION_QUERY_WATCH( surfaceLastFootstepState, "Surface.LastFootstepState", "Response the last footstep took: dry or wet", &SurfaceFootprintSystemInternal::readLastState<false>,
                               false, false );
    SW_AUTOMATION_QUERY_WATCH( surfaceLastFootstepSound, "Surface.LastFootstepSound", "Audio event of the last footstep response (none: silent)",
                               &SurfaceFootprintSystemInternal::readLastSound<false>, false, false );
    SW_AUTOMATION_QUERY_WATCH( surfaceLastFootstepEffect, "Surface.LastFootstepEffect", "Effect of the last footstep response (none: no effect)",
                               &SurfaceFootprintSystemInternal::readLastEffect<false>, false, false );
    SW_AUTOMATION_QUERY_WATCH( surfaceObjectFootstepCount, "Surface.ObjectFootstepCount", "Footsteps of the target object on the surface kind named key (cumulative; 0 before its first step)",
                               &SurfaceFootprintSystemInternal::readCount<true>, true, true );
    SW_AUTOMATION_QUERY_WATCH( surfaceObjectLastFootstep, "Surface.ObjectLastFootstep", "Surface kind of the target object's last footstep", &SurfaceFootprintSystemInternal::readLastKind<true>,
                               true, false );
    SW_AUTOMATION_QUERY_WATCH( surfaceObjectLastFootstepState, "Surface.ObjectLastFootstepState", "Response the target object's last footstep took: dry or wet",
                               &SurfaceFootprintSystemInternal::readLastState<true>, true, false );
    SW_AUTOMATION_QUERY_WATCH( surfaceObjectLastFootstepSound, "Surface.ObjectLastFootstepSound", "Audio event of the target object's last footstep response (none: silent)",
                               &SurfaceFootprintSystemInternal::readLastSound<true>, true, false );
    SW_AUTOMATION_QUERY_WATCH( surfaceObjectLastFootstepEffect, "Surface.ObjectLastFootstepEffect", "Effect of the target object's last footstep response (none: no effect)",
                               &SurfaceFootprintSystemInternal::readLastEffect<true>, true, false );
    // 종류마다: 두 발이 한 프레임에 다른 종류를 디디면 "마지막 발걸음" 은 나중 것 하나다 — 그 종류를 마지막으로 밟은 반응을 키로 본다.
    SW_AUTOMATION_QUERY_WATCH( surfaceObjectFootstepSoundOn, "Surface.ObjectFootstepSoundOn", "Audio event of the target object's last footstep on the surface kind named key (none: silent)",
                               (&SurfaceFootprintSystemInternal::readLastSound<true, true>), true, true );
    SW_AUTOMATION_QUERY_WATCH( surfaceObjectFootstepEffectOn, "Surface.ObjectFootstepEffectOn", "Effect of the target object's last footstep on the surface kind named key (none: no effect)",
                               (&SurfaceFootprintSystemInternal::readLastEffect<true, true>), true, true );
} // namespace sw

// Engine/Environment/Surface/SurfaceDeformSystem.cpp 에서 옮긴 개발 등록(배포 엔진에 컴파일되지 않게).
namespace sw
{
    namespace
    {
        struct SurfaceDeformSystemInternal
        {

            static const SurfaceDeformSystem* findSystem( const AutomationWatchQuery& query )
            {
                return query._pManager != nullptr ? &query._pManager->getSubsystem<SurfaceDeformSystem>() : nullptr;
            }

            /** @brief 감시 값 `Surface.DeformDepth` 입니다. 키 "x z"(월드 m) 자리가 꺼진 깊이(m, 솟았으면 음수)입니다. */
            [[nodiscard]] static bool readDepth( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SurfaceDeformSystem* pSystem = findSystem( query );
                float32                    arrPoint[2]{};
                if ( pSystem == nullptr || StringUtil::parseExactFloats( query._key, " ", arrPoint, 2 ) == false )
                {
                    outValue.setText( "key must be 'x z' (world meters)" );
                    return false;
                }
                outValue.setNumber( static_cast<float64>( -pSystem->findHeightAt( arrPoint[0], arrPoint[1] ) ) );
                return true;
            }

            /** @brief 감시 값 `Surface.DeformActiveTexels` 입니다. 높이가 0 이 아닌(아직 메워지지 않은) 칸 수입니다. */
            [[nodiscard]] static bool readActiveTexels( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SurfaceDeformSystem* pSystem = findSystem( query );
                if ( pSystem == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pSystem->getGrid().countActiveTexels() ) );
                return true;
            }

            /** @brief 감시 값 `Surface.DeformDeepest` 입니다. 창 안에서 가장 깊이 꺼진 깊이(m)입니다. */
            [[nodiscard]] static bool readDeepest( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SurfaceDeformSystem* pSystem = findSystem( query );
                if ( pSystem == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pSystem->getGrid().computeMaxDepth() ) );
                return true;
            }

            /** @brief 감시 값 `Surface.DeformDeepestNear` 입니다. 키 "x z r"(월드 m) — (x, z) 둘레 한 변 2r 정사각형 안에서 가장 깊이 꺼진 깊이(m)입니다. */
            [[nodiscard]] static bool readDeepestNear( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SurfaceDeformSystem* pSystem = findSystem( query );
                float32                    arrArea[3]{};
                if ( pSystem == nullptr || StringUtil::parseExactFloats( query._key, " ", arrArea, 3 ) == false || arrArea[2] < 0.0f )
                {
                    outValue.setText( "key must be 'x z r' (world meters, r >= 0)" );
                    return false;
                }
                outValue.setNumber( static_cast<float64>( pSystem->getGrid().computeMaxDepthNear( arrArea[0], arrArea[1], arrArea[2] ) ) );
                return true;
            }

            /** @brief 감시 값 `Surface.DeformStamps` 입니다. 이 씬에서 찍은 도장 수(누적)입니다. */
            [[nodiscard]] static bool readStamps( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SurfaceDeformSystem* pSystem = findSystem( query );
                if ( pSystem == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pSystem->getStampCount() ) );
                return true;
            }

            /** @brief 감시 값 `Surface.DeformWetTexels` 입니다. 젖은 자국이 아직 마르지 않은 칸 수입니다. */
            [[nodiscard]] static bool readWetTexels( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SurfaceDeformSystem* pSystem = findSystem( query );
                if ( pSystem == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pSystem->getGrid().countWetTexels() ) );
                return true;
            }

            /** @brief 감시 값 `Surface.DeformWetness` 입니다. 키 "x z"(월드 m) 자리의 젖은 자국 젖음(0..1)입니다. */
            [[nodiscard]] static bool readWetness( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SurfaceDeformSystem* pSystem = findSystem( query );
                float32                    arrPoint[2]{};
                if ( pSystem == nullptr || StringUtil::parseExactFloats( query._key, " ", arrPoint, 2 ) == false )
                {
                    outValue.setText( "key must be 'x z' (world meters)" );
                    return false;
                }
                outValue.setNumber( static_cast<float64>( pSystem->getGrid().findWetnessAt( arrPoint[0], arrPoint[1] ) ) );
                return true;
            }

            /** @brief 감시 값 `Surface.DeformHeld` 입니다. 지금 붙잡힌 도장 수입니다. */
            [[nodiscard]] static bool readHeld( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SurfaceDeformSystem* pSystem = findSystem( query );
                if ( pSystem == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pSystem->getPersistentStampCount() ) );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( surfaceDeformDepth, "Surface.DeformDepth", "Depth (m) the surface deform layer sank at key 'x z' (negative: raised)", &SurfaceDeformSystemInternal::readDepth, false,
                               true );
    SW_AUTOMATION_QUERY_WATCH( surfaceDeformActiveTexels, "Surface.DeformActiveTexels", "Texels of the surface deform layer not yet filled back", &SurfaceDeformSystemInternal::readActiveTexels,
                               false, false );
    SW_AUTOMATION_QUERY_WATCH( surfaceDeformDeepest, "Surface.DeformDeepest", "Deepest sink (m) in the surface deform window", &SurfaceDeformSystemInternal::readDeepest, false, false );
    SW_AUTOMATION_QUERY_WATCH( surfaceDeformDeepestNear, "Surface.DeformDeepestNear", "Deepest sink (m) within the square of half side r around key 'x z r'",
                               &SurfaceDeformSystemInternal::readDeepestNear, false, true );
    SW_AUTOMATION_QUERY_WATCH( surfaceDeformStamps, "Surface.DeformStamps", "Stamps put on the surface deform layer in this scene (cumulative)", &SurfaceDeformSystemInternal::readStamps, false,
                               false );
    SW_AUTOMATION_QUERY_WATCH( surfaceDeformHeld, "Surface.DeformHeld", "Stamps held on the surface deform layer now", &SurfaceDeformSystemInternal::readHeld, false, false );
    SW_AUTOMATION_QUERY_WATCH( surfaceDeformWetTexels, "Surface.DeformWetTexels", "Texels of the surface deform layer still wet from wet footprints", &SurfaceDeformSystemInternal::readWetTexels,
                               false, false );
    SW_AUTOMATION_QUERY_WATCH( surfaceDeformWetness, "Surface.DeformWetness", "Wetness (0..1) of the wet footprint layer at key 'x z'", &SurfaceDeformSystemInternal::readWetness, false, true );
} // namespace sw

// Engine/Environment/Water/BoatComponent.cpp 에서 옮긴 개발 등록(배포 엔진에 컴파일되지 않게).
namespace sw
{
    namespace
    {
        struct BoatWatchesInternal
        {
            /** @brief 질의 대상의 배입니다. 없으면 까닭을 적고 nullptr 입니다. */
            static const BoatComponent* findTargetBoat( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                return query.findTargetComponent<BoatComponent>( "Boat", outValue );
            }

            /** @brief 감시 값 `Boat.ForwardSpeed` 입니다. 선수 방향 속도(m/s, 땅 기준)입니다. */
            [[nodiscard]] static bool readForwardSpeed( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const BoatComponent* pBoat = findTargetBoat( query, outValue );
                if ( pBoat == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pBoat->getState()._forwardSpeed ) );
                return true;
            }

            /** @brief 감시 값 `Boat.TurnRadius` 입니다. 선수 방향 속도 / 위 축 회전 속도(m)이고, 거의 돌지 않으면 글 `straight` 입니다. */
            [[nodiscard]] static bool readTurnRadius( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const BoatComponent* pBoat = findTargetBoat( query, outValue );
                if ( pBoat == nullptr )
                    return false;
                const BoatState& state = pBoat->getState();
                if ( MathUtil::abs( state._yawRate ) < 1.0e-3f )
                    outValue.setText( "straight" );
                else
                    outValue.setNumber( static_cast<float64>( MathUtil::abs( state._forwardSpeed / state._yawRate ) ) );
                return true;
            }

            /** @brief 감시 값 `Boat.RollPeak` 입니다. 최근 4 ~ 8 초의 옆 기울기 크기 최댓값(rad)입니다. */
            [[nodiscard]] static bool readRollPeak( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const BoatComponent* pBoat = findTargetBoat( query, outValue );
                if ( pBoat == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pBoat->getRollPeak() ) );
                return true;
            }

            /** @brief 감시 값 `Boat.PitchPeak` 입니다. 최근 4 ~ 8 초의 앞뒤 기울기 크기 최댓값(rad)입니다. */
            [[nodiscard]] static bool readPitchPeak( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const BoatComponent* pBoat = findTargetBoat( query, outValue );
                if ( pBoat == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pBoat->getPitchPeak() ) );
                return true;
            }

            /** @brief 감시 값 `Boat.SailForce` 입니다. 마지막 스텝에 돛이 낸 앞 힘(N)입니다. */
            [[nodiscard]] static bool readSailForce( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const BoatComponent* pBoat = findTargetBoat( query, outValue );
                if ( pBoat == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pBoat->getState()._sailForce ) );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( boatForwardSpeed, "Boat.ForwardSpeed", "Forward speed (m/s, over ground) of the boat on target", &BoatWatchesInternal::readForwardSpeed, true, false );
    SW_AUTOMATION_QUERY_WATCH( boatTurnRadius, "Boat.TurnRadius", "Turning radius (m) of the boat on target: forward speed / yaw rate (text straight when not turning)",
                               &BoatWatchesInternal::readTurnRadius, true, false );
    SW_AUTOMATION_QUERY_WATCH( boatRollPeak, "Boat.RollPeak", "Largest roll (rad) of the boat on target over the last 4-8 seconds", &BoatWatchesInternal::readRollPeak, true, false );
    SW_AUTOMATION_QUERY_WATCH( boatPitchPeak, "Boat.PitchPeak", "Largest pitch (rad) of the boat on target over the last 4-8 seconds", &BoatWatchesInternal::readPitchPeak, true, false );
    SW_AUTOMATION_QUERY_WATCH( boatSailForce, "Boat.SailForce", "Forward force (N) the sail of the boat on target gave in the last physics step", &BoatWatchesInternal::readSailForce, true, false );
} // namespace sw

// Engine/Environment/Water/WaterBodyComponent.cpp 에서 옮긴 개발 등록(배포 엔진에 컴파일되지 않게).
namespace sw
{
    namespace
    {
        struct WaterBodyWatchesInternal
        {
            /** @brief 감시 값 `Water.RiverPointCount` 입니다. 대상 물의 강 경로 점 수입니다(스플라인 어댑터가 몰면 곡선을 샘플한 수). */
            [[nodiscard]] static bool readRiverPointCount( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const WaterBodyComponent* pWater = query.findTargetComponent<WaterBodyComponent>( "water body", outValue );
                if ( pWater == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pWater->getRiverPoints().size() ) );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( waterRiverPointCount, "Water.RiverPointCount", "River path points of the target's water body", &WaterBodyWatchesInternal::readRiverPointCount, true, false );
} // namespace sw

// Engine/Environment/Water/BuoyancyComponent.cpp 에서 옮긴 개발 등록(배포 엔진에 컴파일되지 않게).
namespace sw
{
    SW_AUTOMATION_GETTER_WATCH( buoyancySubmergedPontoons, "Buoyancy.SubmergedPontoons", "Pontoons of the first buoyancy component that touched water in the last step", BuoyancyComponent,
                                &BuoyancyComponent::getSubmergedPontoonCount );
} // namespace sw

// Engine/Environment/Terrain/TerrainColliderComponent.cpp 에서 옮긴 개발 등록(배포 엔진에 컴파일되지 않게).
namespace sw
{
    SW_AUTOMATION_GETTER_WATCH( terrainColliderBodyCount, "TerrainCollider.BodyCount", "Static chunk bodies the first terrain collider holds (0 before play or without a terrain)", TerrainColliderComponent,
                                &TerrainColliderComponent::getBodyCount );
} // namespace sw

// Engine/Environment/Foliage/FoliageInfluencerComponent.cpp 에서 옮긴 개발 등록(배포 엔진에 컴파일되지 않게).
namespace sw
{
    namespace
    {
        struct FoliageInfluencerWatchesInternal
        {
            /** @brief 감시 값 `Foliage.InfluencerCount` 입니다. 씬의 켜진 풀 휘게 하는 구 수입니다(셰이더는 카메라에 가까운 넷을 싣는다). */
            [[nodiscard]] static bool readInfluencerCount( const GameObjectManager* pManager, float64& outValue )
            {
                if ( pManager == nullptr )
                    return false;
                uint32 count = 0;
                for ( const FoliageInfluencerComponent* pInfluencer : pManager->getComponentRegistry().getAll<FoliageInfluencerComponent>() )
                {
                    if ( pInfluencer->isActive() && pInfluencer->getRadius() > 0.0f )
                        ++count;
                }
                outValue = static_cast<float64>( count );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_WATCH( foliageInfluencerCount, "Foliage.InfluencerCount", "Active foliage influencer spheres in the scene (grass bends around them)", &FoliageInfluencerWatchesInternal::readInfluencerCount );
} // namespace sw

// Engine/Environment/Wind/WindComponent.cpp 에서 옮긴 개발 등록(배포 엔진에 컴파일되지 않게).
namespace sw
{
    namespace
    {
        struct WindWatchesInternal
        {
            /** @brief 감시 값 `Wind.Speed` 입니다. 씬의 바람 풍속(m/s, 적은 풍속 × 날씨 배율)입니다. */
            [[nodiscard]] static bool readSpeed( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                WindSettings wind;
                if ( query._pManager == nullptr || WindComponent::findWind( *query._pManager, wind ) == false )
                {
                    outValue.setText( "no wind in the scene" );
                    return false;
                }
                outValue.setNumber( static_cast<float64>( wind._speed ) );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( windSpeed, "Wind.Speed", "Wind speed of the scene (m/s, authored speed x weather scale, without gusts)", &WindWatchesInternal::readSpeed, false, false );
} // namespace sw

// Engine/Environment/Surface/GroundSurfaceComponent.cpp 에서 옮긴 개발 등록(배포 엔진에 컴파일되지 않게).
namespace sw
{
    namespace
    {
        struct GroundSurfaceComponentWatchesInternal
        {
            /** @brief 감시 값 `Surface.GroundKind` 입니다. 대상의 `GroundSurfaceComponent` 가 마지막으로 읽은 표면 종류입니다(디딘 것이 없으면 값이 없다). */
            [[nodiscard]] static bool readGroundKind( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GroundSurfaceComponent* pGround = query.findTargetComponent<GroundSurfaceComponent>( "GroundSurfaceComponent", outValue );
                if ( pGround == nullptr )
                    return false;
                if ( pGround->getGroundKind().empty() )
                {
                    outValue.setText( "no ground under the target" );
                    return false;
                }
                outValue.setText( pGround->getGroundKind().c_str() );
                return true;
            }

            /** @brief 감시 값 `Surface.MoveScale` 입니다. 대상 캐릭터 컨트롤러가 지난 물리 프레임에 걷는 속도에 곱한 계수입니다. */
            [[nodiscard]] static bool readMoveScale( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const CharacterControllerComponent* pController = query.findTargetComponent<CharacterControllerComponent>( "CharacterControllerComponent", outValue );
                if ( pController == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pController->getMoveScale() ) );
                return true;
            }

            /** @brief `surface.probe <object> [length]` — 오브젝트 발 아래의 표면(종류 · 형식 · 재질 · 지형 레이어 · 법선 · 표면 속도 · 물 깊이 · 젖음 · 이동 계수)을 적습니다. */
            static bool runProbe( const vector<string>& listArgument, string& outReply )
            {
                float32    length  = 1.0f;
                const bool bParsed = ( listArgument.size() == 1 || listArgument.size() == 2 ) && ( listArgument.size() == 1 || StringUtil::parseFloat( listArgument[1], length ) );
                if ( bParsed == false || length <= 0.0f )
                    return false;
                GameObject* pObject = engine::findActiveObject( hashed_string( listArgument[0] ) );
                if ( pObject == nullptr )
                {
                    StringBuilder<constant::buffer::kMax256> missingReply;
                    missingReply.appendFormat( "surface.probe: no object named '%#' in the active scene", listArgument[0] );
                    outReply.assign( missingReply.view() );
                    return true;
                }
                SurfaceSample sample;
                if ( GroundSurfaceComponent::sampleUnder( *pObject, length, sample ) == false )
                {
                    StringBuilder<constant::buffer::kMax256> missingReply;
                    missingReply.appendFormat( "surface.probe: nothing with a surface kind under '%#' (see Surface.KindMissing in the log)", listArgument[0] );
                    outReply.assign( missingReply.view() );
                    return true;
                }
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "surface %# kind=%# source=%# material=%# layer=%# point=%#,%#,%# normal=%#,%#,%# velocity=%#,%#,%# water=%# wetness=%# moveScale=%#", listArgument[0],
                                           sample._pKind->_name.c_str(), SurfaceSample::getSourceName( sample._source ), sample._material.c_str(),
                                           sample._terrainLayer == SurfaceSample::kNoTerrainLayer ? -1 : static_cast<int32>( sample._terrainLayer ), sample._point._x, sample._point._y,
                                           sample._point._z, sample._normal._x, sample._normal._y, sample._normal._z, sample._surfaceVelocity._x, sample._surfaceVelocity._y,
                                           sample._surfaceVelocity._z, sample._waterDepth, sample._wetness, sample._pKind->_moveScale );
                outReply = string( replyBuilder.view() );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( surfaceGroundKind, "Surface.GroundKind", "Surface kind the target's GroundSurfaceComponent stood on last", &GroundSurfaceComponentWatchesInternal::readGroundKind, true, false );
    SW_AUTOMATION_QUERY_WATCH( surfaceMoveScale, "Surface.MoveScale", "Move scale the target's character controller applied to its walk speed last physics frame",
                               &GroundSurfaceComponentWatchesInternal::readMoveScale, true, false );
    SW_DEV_COMMAND( SurfaceProbe, "surface.probe", "surface.probe <object> [length]",
                    "Print the surface under an object's feet: kind, source (collider, terrain layer, water), material, normal, surface velocity, water depth, wetness, move scale",
                    &GroundSurfaceComponentWatchesInternal::runProbe );
} // namespace sw

// Engine/Environment/Foliage/FoliageTerrainFollower.cpp 에서 옮긴 감시 읽기(아래 등록이 부른다).
namespace sw
{
    namespace
    {
        /**
         * @brief 활성 씬의 폴리지가 지형 편집을 아직 따라가지 못했으면 진행 중(따라가는 폴리지 수)입니다 - 편집 뒤 가라앉는 시간과 다시 계산 사이에 찍힌 사진은 편집 전 배치를 담는다.
         *        시나리오가 고정 대기로 그 시간을 넉넉히 흘려 숨기던 것을 캡처 쪽이 기다린다(flowterrain 의 지형 편집 뒤 나무 한 그루가 남았다).
         *        주인 곁에서 등록한다 - 예전에는 자동화 층(11)의 공용 파일이 이 층(8) 헤더를 읽었다.
         */
        AutomationReadiness readFoliageFollowingTerrain()
        {
            const GameObjectManager* pManager = engine::findActiveObjectManager();
            uint64                   behind   = 0;
            if ( pManager != nullptr )
                for ( const FoliageComponent* pFoliage : pManager->getComponentRegistry().getAll<FoliageComponent>() )
                {
                    behind += pFoliage->isBehindTerrainEdits() ? 1u : 0u;
                }
            return behind > 0 ? AutomationReadiness::waiting( behind, "foliage still follows a terrain edit" ) : AutomationReadiness::ready();
        }
    } // namespace
} // namespace sw

// Engine/Environment/Foliage/FoliageTerrainFollower.cpp 에서 옮긴 개발 등록(배포 엔진에 컴파일되지 않게).
namespace sw
{
    SW_AUTOMATION_CAPTURE_BLOCKER( foliageFollowingTerrain, "FoliageFollowingTerrain", &readFoliageFollowingTerrain );
} // namespace sw

// Engine/Environment/Foliage/FoliageComponent.cpp 에서 옮긴 개발 등록(배포 엔진에 컴파일되지 않게).
namespace sw
{
    SW_AUTOMATION_GETTER_WATCH( foliageCutCount, "Foliage.CutCount", "Instances of the first foliage component hidden by cuts now", FoliageComponent, &FoliageComponent::getCutInstanceCount );
} // namespace sw
