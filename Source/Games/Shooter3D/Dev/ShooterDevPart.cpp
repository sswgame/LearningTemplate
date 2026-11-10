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
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Runtime/Character/Camera/CameraDirectorComponent.h"
#include "GameFramework/Runtime/Character/Control/Controller/ControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/Flow/IGameDevPart.h"
#include "GameFramework/Runtime/Foundation/Framework/GameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterAppearanceComponent.h"

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
                , _bBoneErrorShown{ SW_FALSE }
            {
                // 근접 스위치는 주소로 건다 — 이 부분의 갱신은 게임 플레이 중에만 돌지만 조종자는 에디터의 넷 시뮬 월드에서도 돈다(`ShooterAutoPlayOptions`).
                if ( ShooterAutoPlayOptions* const pOptions = game::findSubsystem<ShooterAutoPlayOptions>() )
                    pOptions->_pForceMelee = &gv_shooterAutoMelee;
            }

            ~ShooterDevPart() override
            {
                if ( ShooterAutoPlayOptions* const pOptions = game::findSubsystem<ShooterAutoPlayOptions>() )
                    pOptions->_pForceMelee = nullptr;
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
             * @brief 지난 프레임에 그려진 플레이어 몸, 카메라, 두 총(시점 총 · 몸 총), 두 손, 적 하나의 월드 위치를 CSV 한 줄로 쌓습니다.
             * @details 게임 갱신은 씬 틱 밖이라 읽는 월드 · 본은 지난 프레임에 그려진 그대로다. 총 · 손 · 카메라를 같은 월드 공간에 두어 떨림을 프레임별로 대조한다.
             *          뼈 이름은 몸 스켈레톤(UBC)의 이름이다 — 없으면 0 으로 적고 넘어가지 않고 오류를 한 번 남긴다(KayKit 이름이 남아 모든 뼈 칸이 0 이던 기록이었다).
             */
            void appendMotionTrace( GameObjectManager& manager, const ShooterGameModeComponent& gameMode, float32 deltaTime )
            {
                const GameObject* const             pPlayerObject = manager.resolveGameObject( gameMode.getPlayer() );
                const ShooterPlayerComponent* const pPlayer       = pPlayerObject != nullptr ? pPlayerObject->getComponent<ShooterPlayerComponent>() : nullptr;
                if ( pPlayer == nullptr )
                    return;
                if ( _motionTrace.empty() )
                    _motionTrace = "frame,dt,feetX,feetZ,lookYaw,bodyX,bodyY,bodyZ,bodyYaw,pelvisX,pelvisY,pelvisZ,spineX,spineY,spineZ,handRX,handRY,handRZ,handLX,handLY,handLZ,"
                                   "camX,camY,camZ,camYaw,viewGunX,viewGunY,viewGunZ,bodyGunX,bodyGunY,bodyGunZ,enemyX,enemyZ,state\n";
                static constexpr const utf8* kArrBone[4] = { "pelvis", "spine_03", "hand_r", "hand_l" };
                float3                       arrBone[4]{};
                float3                       body{};
                float3                       bodyGun{};
                float32                      bodyYaw = 0.0f;
                hashed_string                state;
                const GameObject*            pBody = pPlayer->findBodyObject();
                if ( pBody != nullptr )
                {
                    const SkeletalMeshComponent* pUnit = pBody->getComponent<SkeletalMeshComponent>();
                    if ( pUnit != nullptr && pUnit->getModelSpaceTransforms().empty() == false )
                    {
                        const float4x4 world = pUnit->getWorldMatrix();
                        body                 = world.getTranslation();
                        const float3 forward = float3::transformVector( float3{ 0.0f, 0.0f, 1.0f }, world );
                        bodyYaw              = MathUtil::atan2( forward._x, forward._z );
                        for ( uint32 boneIndex = 0; boneIndex < 4; ++boneIndex )
                        {
                            float4x4 bone;
                            if ( pUnit->findBoneModelTransform( hashed_string( kArrBone[boneIndex] ), bone ) )
                            {
                                arrBone[boneIndex] = float3::transform( bone.getTranslation(), world );
                            }
                            else if ( _bBoneErrorShown == SW_FALSE )
                            {
                                _bBoneErrorShown = SW_TRUE;
                                SW_LOG_ERROR_ID( "ShooterDevPart.MotionTraceBoneMissing", "[Shooter] motion trace: the body skeleton has no bone '%#' - its columns stay 0", kArrBone[boneIndex] );
                            }
                        }
                    }
                    const SkeletalAnimatorComponent* pAnimator = pBody->getComponent<SkeletalAnimatorComponent>();
                    if ( pAnimator != nullptr )
                        state = pAnimator->getCurrentStateName();
                    const CharacterAppearanceComponent* pAppearance = pBody->getComponent<CharacterAppearanceComponent>();
                    const GameObject*                   pGun        = pAppearance != nullptr ? manager.resolveGameObject( pAppearance->findSlotPartObject( hashed_string( "MainHand" ) ) ) : nullptr;
                    const SceneComponent*               pGunScene   = pGun != nullptr ? pGun->getPrimarySceneComponent() : nullptr;
                    if ( pGunScene != nullptr )
                        bodyGun = pGunScene->getWorldPosition();
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
                // 1인칭 시점 총(플레이어 오브젝트의 카메라 자식). 1인칭이 아니어도 자리는 카메라를 따라간다.
                float3              viewGun{};
                const hashed_string viewGunName( "ViewWeapon" );
                pPlayerObject->forEachComponentOfType<MeshComponent>( [&viewGun, &viewGunName]( MeshComponent* pMesh )
                {
                    if ( pMesh->getComponentName() == viewGunName )
                        viewGun = pMesh->getWorldPosition();
                } );
                float3 enemy{};
                for ( uint32 enemyIndex = 0; enemyIndex < gameMode.getEnemyCount(); ++enemyIndex )
                {
                    const GameObject*            pEnemy = manager.resolveGameObject( gameMode.getEnemyObject( enemyIndex ) );
                    const SkeletalMeshComponent* pUnit  = pEnemy != nullptr ? pEnemy->getComponent<SkeletalMeshComponent>() : nullptr;
                    if ( pUnit == nullptr )
                        continue;
                    enemy = pUnit->getWorldPosition();
                    break;
                }
                const float3                              feet = pPlayer->getFeetPosition();
                StringBuilder<constant::buffer::kMax1024> row;
                row.appendFormat( "%#,%.5f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f", _traceFrame, deltaTime, feet._x, feet._z, pPlayer->getLookYaw(), body._x, body._y, body._z, bodyYaw );
                for ( const float3& bone : arrBone )
                {
                    row.appendFormat( ",%.4f,%.4f,%.4f", bone._x, bone._y, bone._z );
                }
                row.appendFormat( ",%.4f,%.4f,%.4f,%.4f", camera._x, camera._y, camera._z, cameraYaw );
                row.appendFormat( ",%.4f,%.4f,%.4f,%.4f,%.4f,%.4f", viewGun._x, viewGun._y, viewGun._z, bodyGun._x, bodyGun._y, bodyGun._z );
                row.appendFormat( ",%.4f,%.4f,%#\n", enemy._x, enemy._z, state.c_str() );
                _motionTrace += row.c_str();
                ++_traceFrame;
            }

        private:
            string _motionTrace;         ///< 움직임 기록 CSV(게임이 내려갈 때 파일로 쓴다)
            uint32 _traceFrame;          ///< 움직임 기록의 프레임 번호
            uint8  _bBoneErrorShown : 1; ///< 기록할 뼈가 없다는 오류를 남겼다(한 번만)
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
