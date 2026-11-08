/**
 * @file Games/Shooter3D/Dev/ShooterDevPart.cpp
 * @brief Shooter3D 의 개발 전용 부분입니다 — 자동 플레이 근접 강제(`-gv_shooterAutoMelee`)와 움직임 기록(`-gv_shooterMotionTrace`)을 든다.
 * @details 개발 전용 소스다(Dev/ — 배포 구성이 짓지 않는다). 바탕 `GameInstance` 가 공장(`IGameDevPartFactory`)으로 하나 만들어 게임 갱신마다 부른다.
 *          게임 모드 · 자동 조준 조종자는 이 파일을 모른다 — 조종자의 근접 칸(`ShooterAutoAimControllerComponent::setMelee`)과 게임 모드의 읽기 창구만 쓴다.
 */
#include "pch.h"

#include "Core/Container/string.h"
#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Animation/Component/SkeletalAnimatorComponent.h"
#include "Engine/Animation/Component/SkeletalMeshComponent.h"
#include "Engine/Module/ModuleSubsystemRegistrar.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Runtime/Character/Camera/CameraDirectorComponent.h"
#include "GameFramework/Runtime/Character/Control/Controller/ControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/Flow/IGameDevPart.h"
#include "GameFramework/Runtime/Foundation/Framework/GameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

#include "Games/Shooter3D/ShooterAutoAimControllerComponent.h"
#include "Games/Shooter3D/ShooterGameModeComponent.h"
#include "Games/Shooter3D/ShooterPlayerComponent.h"

namespace sw
{
    /** @brief `-gv_shooterAutoMelee=1`: 자동 플레이 AI 가 닿는 거리의 적을 근접으로도 칩니다(네트 근접 시나리오 — 기본 꺼짐이라 자동 플레이 골든은 그대로다). */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_shooterAutoMelee, 0, "Shooter3D: 자동 플레이 AI 가 가까운 적을 근접으로도 (1=켜기)" );
    /** @brief `-gv_shooterMotionTrace=<경로>`: 프레임마다 플레이어 몸, 본, 카메라, 적의 그려진 위치를 CSV 로 남깁니다(튐 진단용이며 게임이 내려갈 때 씁니다). */
    SW_TEST_GLOBAL_VARIABLE( sw::GlobalDiskPath, gv_shooterMotionTrace, "", "Shooter3D: 프레임마다 몸 · 본 · 카메라 · 적 자리를 CSV 로 (경로, 비면 끔)" );

    namespace
    {
        /**
         * @class ShooterDevPart
         * @brief 활성 씬의 Shooter 게임 모드를 보고 두 개발 스위치를 겁니다. 게임 스레드의 게임 갱신에서 돈다(씬 틱 밖).
         */
        class ShooterDevPart final : public IGameDevPart
        {
        public:
            ShooterDevPart()
                : _motionTrace{}
                , _traceFrame{ 0 }
                , _bMeleeForced{ SW_FALSE }
            {
            }

            ~ShooterDevPart() override
            {
                if ( _motionTrace.empty() == false && FileUtil::writeTextFile( gv_shooterMotionTrace, _motionTrace ) == false )
                    SW_LOG_WARNING_ID( "ShooterGameMode.MotionTraceCouldNot", "[Shooter] motion trace '%#' could not be written", gv_shooterMotionTrace.c_str() );
            }

            void update( float32 deltaTime ) override
            {
                SceneManager* const      pSceneManager = game::findSubsystem<SceneManager>();
                Scene* const             pScene        = pSceneManager != nullptr ? pSceneManager->getActiveScene() : nullptr;
                GameObjectManager* const pManager      = pScene != nullptr ? pScene->getObjectManager() : nullptr;
                if ( pManager == nullptr )
                    return;
                applyMelee( *pManager );
                if ( gv_shooterMotionTrace.empty() )
                    return;
                // 게임 모드는 레지스트리에 없다 — 폰(레지스트리에 오른다)의 Shooter 플레이어가 가리키는 게임 모드를 쓴다(그 게임 모드의 플레이어가 기록 대상이다).
                for ( const PawnComponent* pPawn : pManager->getComponentRegistry().getAll<PawnComponent>() )
                {
                    const GameObject* const               pOwner    = pPawn->getOwner();
                    const ShooterPlayerComponent* const   pPlayer   = pOwner != nullptr ? pOwner->getComponent<ShooterPlayerComponent>() : nullptr;
                    const ShooterGameModeComponent* const pGameMode = pPlayer != nullptr ? GameModeComponent::resolve<ShooterGameModeComponent>( *pManager, pPlayer->getGameMode() ) : nullptr;
                    if ( pGameMode == nullptr || pGameMode->getPlayer() != pOwner->getHandle() )
                        continue;
                    appendMotionTrace( *pManager, *pGameMode, deltaTime );
                    return;
                }
            }

        private:
            /**
             * @brief `-gv_shooterAutoMelee` 가 켜져 있는 동안 자동 조준 조종자마다 근접을 켜고(늦게 선 조종자도), 꺼진 첫 프레임에 한 번 끈다.
             * @details shortcut: 끌 때 조종자 데이터의 원래 근접 값으로 돌리지 않고 끈다 — 데이터가 근접을 켠 조종자가 생기면 켜기 전 값을 기억한다.
             */
            void applyMelee( GameObjectManager& manager )
            {
                const bool bForce = gv_shooterAutoMelee != 0;
                if ( bForce == false && _bMeleeForced == SW_FALSE )
                    return;
                for ( ControllerComponent* pController : manager.getComponentRegistry().getAll<ControllerComponent>() )
                {
                    ShooterAutoAimControllerComponent* const pAutoAim = castTo<ShooterAutoAimControllerComponent>( pController );
                    if ( pAutoAim != nullptr )
                        pAutoAim->setMelee( bForce );
                }
                _bMeleeForced = bForce ? SW_TRUE : SW_FALSE;
            }

            /** @brief 지난 프레임에 그려진 플레이어 몸, 본, 카메라, 적 하나의 위치를 CSV 한 줄로 쌓습니다. 게임 갱신은 씬 틱 밖이라 읽는 월드 · 본은 지난 프레임에 그려진 그대로다. */
            void appendMotionTrace( GameObjectManager& manager, const ShooterGameModeComponent& gameMode, float32 deltaTime )
            {
                const GameObject* const             pPlayerObject = manager.resolveGameObject( gameMode.getPlayer() );
                const ShooterPlayerComponent* const pPlayer       = pPlayerObject != nullptr ? pPlayerObject->getComponent<ShooterPlayerComponent>() : nullptr;
                if ( pPlayer == nullptr )
                    return;
                if ( _motionTrace.empty() )
                    _motionTrace = "frame,dt,feetX,feetZ,lookYaw,bodyX,bodyY,bodyZ,bodyYaw,rootX,rootY,rootZ,hipsX,hipsY,hipsZ,headX,headY,headZ,handX,handY,handZ,"
                                   "camX,camY,camZ,camYaw,enemyX,enemyZ,enemyHipsY,state\n";
                static constexpr const utf8* kArrBone[4] = { "root", "hips", "head", "hand.r" };
                float3                       arrBone[4]{};
                float3                       body{};
                float32                      bodyYaw = 0.0f;
                hashed_string                state;
                const GameObject*            pBody = pPlayer->findBodyObject();
                if ( pBody != nullptr )
                {
                    const SkeletalMeshComponent* pUnit = pBody->getComponent<SkeletalMeshComponent>();
                    if ( pUnit != nullptr )
                    {
                        const float4x4 world = pUnit->getWorldMatrix();
                        body                 = world.getTranslation();
                        const float3 forward = float3::transformVector( float3{ 0.0f, 0.0f, 1.0f }, world );
                        bodyYaw              = MathUtil::atan2( forward._x, forward._z );
                        for ( uint32 boneIndex = 0; boneIndex < 4; ++boneIndex )
                        {
                            float4x4 bone;
                            if ( pUnit->findBoneModelTransform( hashed_string( kArrBone[boneIndex] ), bone ) )
                                arrBone[boneIndex] = bone.getTranslation();
                        }
                    }
                    const SkeletalAnimatorComponent* pAnimator = pBody->getComponent<SkeletalAnimatorComponent>();
                    if ( pAnimator != nullptr )
                        state = pAnimator->getCurrentStateName();
                }
                float3                 camera{};
                float32                cameraYaw = 0.0f;
                const GameObjectHandle player    = gameMode.getPlayer();
                for ( const CameraDirectorComponent* pCameraDirector : manager.getComponentRegistry().getAll<CameraDirectorComponent>() )
                {
                    const bool             bFollows = pCameraDirector->isPendingDestroy() == false && pCameraDirector->getTarget() == player;
                    const CameraComponent* pCamera  = bFollows ? pCameraDirector->getOwner()->getComponent<CameraComponent>() : nullptr;
                    if ( pCamera == nullptr )
                        continue;
                    const float4x4 world = pCamera->getWorldMatrix();
                    camera               = world.getTranslation();
                    const float3 forward = float3::transformVector( float3{ 0.0f, 0.0f, 1.0f }, world );
                    cameraYaw            = MathUtil::atan2( forward._x, forward._z );
                }
                float3  enemy{};
                float32 enemyHipsY = 0.0f;
                for ( uint32 enemyIndex = 0; enemyIndex < gameMode.getEnemyCount(); ++enemyIndex )
                {
                    const GameObject*            pEnemy = manager.resolveGameObject( gameMode.getEnemyObject( enemyIndex ) );
                    const SkeletalMeshComponent* pUnit  = pEnemy != nullptr ? pEnemy->getComponent<SkeletalMeshComponent>() : nullptr;
                    if ( pUnit == nullptr )
                        continue;
                    enemy = pUnit->getWorldPosition();
                    float4x4 hips;
                    if ( pUnit->findBoneModelTransform( hashed_string( "hips" ), hips ) )
                        enemyHipsY = hips.getTranslation()._y;
                    break;
                }
                const float3                              feet = pPlayer->getFeetPosition();
                StringBuilder<constant::buffer::kMax1024> row;
                row.appendFormat( "%#,%.5f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f", _traceFrame, deltaTime, feet._x, feet._z, pPlayer->getLookYaw(), body._x, body._y, body._z, bodyYaw );
                for ( const float3& bone : arrBone )
                {
                    row.appendFormat( ",%.4f,%.4f,%.4f", bone._x, bone._y, bone._z );
                }
                row.appendFormat( ",%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%#\n", camera._x, camera._y, camera._z, cameraYaw, enemy._x, enemy._z, enemyHipsY, state.c_str() );
                _motionTrace += row.c_str();
                ++_traceFrame;
            }

        private:
            string _motionTrace;      ///< 움직임 기록 CSV(게임이 내려갈 때 파일로 쓴다)
            uint32 _traceFrame;       ///< 움직임 기록의 프레임 번호
            uint8  _bMeleeForced : 1; ///< 지난 프레임에 근접을 켜 두었다 — 꺼진 첫 프레임에만 끈다
        };
    } // namespace

    /** @class ShooterDevPartFactory @brief Shooter3D 의 개발 전용 부분을 만드는 모듈 창구입니다(`SW_MODULE_PART`). 이름 해시가 함수 서명 글이라 `sw` 에 둔다. */
    class ShooterDevPartFactory final : public IGameDevPartFactory
    {
    public:
        static constexpr const utf8* kModulePartName = "ShooterDevPartFactory";

        unique_ptr<IGameDevPart> createDevPart() override { return make_unique<ShooterDevPart>(); }
    };

    SW_MODULE_PART( Engine, ShooterDevPartFactory, IGameDevPartFactory );
} // namespace sw
