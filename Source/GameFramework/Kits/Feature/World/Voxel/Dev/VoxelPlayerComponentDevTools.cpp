/**
 * @file VoxelPlayerComponentDevTools.cpp
 * @brief `VoxelPlayerComponent` 의 자동화 감시 값입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/FormatDesc.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Feature/World/Voxel/Catalog/VoxelBlock.h"
#include "GameFramework/Kits/Feature/World/Voxel/Component/VoxelAutoPlayControllerComponent.h"
#include "GameFramework/Kits/Feature/World/Voxel/Component/VoxelGameModeComponent.h"
#include "GameFramework/Kits/Feature/World/Voxel/Component/VoxelPlayerComponent.h"
#include "GameFramework/Runtime/Character/Control/Controller/PlayerControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/FirstPersonCameraComponent.h"
#include "GameFramework/Runtime/Character/Control/Intent/ControlIntent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Presentation/GameSound.h"
#include "GameFramework/Runtime/Foundation/Save/StatefulComponentRegistrar.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

namespace sw
{
    namespace
    {
        struct VoxelPlayerComponentDevInternal
        {
            [[nodiscard]] static bool readWalkedDistance( const VoxelPlayerComponent& player, float64& outValue )
            {
                const float3 delta = player.getBody().getPosition() - player.getStartPosition()._value;
                outValue           = static_cast<float64>( MathUtil::sqrt( delta._x * delta._x + delta._z * delta._z ) );
                return true;
            }

            [[nodiscard]] static bool readHotbarSlot( const VoxelPlayerComponent& player, float64& outValue )
            {
                outValue = player.getHotbar().getSelectedIndex();
                return true;
            }

            [[nodiscard]] static bool readControlYaw( const VoxelPlayerComponent& player, float64& outValue )
            {
                const GameObject*    pOwner = player.getOwner();
                const PawnComponent* pPawn  = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
                if ( pPawn == nullptr )
                    return false;
                outValue = static_cast<float64>( pPawn->getIntent()._controlYaw );
                return true;
            }

            [[nodiscard]] static bool readControlPitch( const VoxelPlayerComponent& player, float64& outValue )
            {
                const GameObject*    pOwner = player.getOwner();
                const PawnComponent* pPawn  = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
                if ( pPawn == nullptr )
                    return false;
                outValue = static_cast<float64>( pPawn->getIntent()._controlPitch );
                return true;
            }

            /** @brief 몸이 땅에 놓였고 땅을 딛고 있으면 1 입니다(첫 놓기 뒤 떨어져 자리를 잡았다). */
            [[nodiscard]] static bool readOnGround( const VoxelPlayerComponent& player, float64& outValue )
            {
                outValue = player.getBody().isOnGround() ? 1.0 : 0.0;
                return true;
            }

            /** @brief 플레이어 폰을 누가 조종하는지 반환합니다. 0은 플레이어 조종자, 1은 자동 플레이 AI, −1은 아무도 조종하지 않는 경우입니다. */
            [[nodiscard]] static bool readControllerKind( const VoxelPlayerComponent& player, float64& outValue )
            {
                GameObject*          pOwner        = player.getOwner();
                GameObjectManager*   pOwnerManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
                const PawnComponent* pPawn         = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
                if ( pPawn == nullptr || pOwnerManager == nullptr )
                    return false;
                Component* pController = pPawn->isPossessed() ? pOwnerManager->resolveComponent( pPawn->getController() ) : nullptr;
                if ( pController == nullptr )
                    outValue = -1.0;
                else if ( castTo<VoxelAutoPlayControllerComponent>( pController ) != nullptr )
                    outValue = 1.0;
                else if ( castTo<PlayerControllerComponent>( pController ) != nullptr )
                    outValue = 0.0;
                else
                    outValue = 2.0;
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_COMPONENT_WATCH( voxelWalkedDistance, "Voxel.WalkedDistance", "Horizontal distance of the player body from where it was first placed", VoxelPlayerComponent, &VoxelPlayerComponentDevInternal::readWalkedDistance );
    SW_AUTOMATION_COMPONENT_WATCH( voxelHotbarSlot, "Voxel.HotbarSlot", "Selected hotbar slot (0-based)", VoxelPlayerComponent, &VoxelPlayerComponentDevInternal::readHotbarSlot );
    SW_AUTOMATION_GETTER_WATCH( voxelBrokenCount, "Voxel.BrokenCount", "Blocks the player has broken", VoxelPlayerComponent, &VoxelPlayerComponent::getBrokenCount );
    SW_AUTOMATION_COMPONENT_WATCH( voxelControlPitch, "Voxel.ControlPitch", "Control pitch of the player pawn (radians, up is +)", VoxelPlayerComponent, &VoxelPlayerComponentDevInternal::readControlPitch );
    SW_AUTOMATION_COMPONENT_WATCH( voxelOnGround, "Voxel.OnGround", "1 while the player body stands on ground (it has landed after being placed)", VoxelPlayerComponent, &VoxelPlayerComponentDevInternal::readOnGround );
    SW_AUTOMATION_COMPONENT_WATCH( voxelControlYaw, "Voxel.ControlYaw", "Control yaw of the player pawn (radians)", VoxelPlayerComponent, &VoxelPlayerComponentDevInternal::readControlYaw );
    SW_AUTOMATION_COMPONENT_WATCH( voxelControllerKind, "Voxel.ControllerKind", "Who possesses the player pawn: 0 player controller, 1 auto play AI, -1 nobody", VoxelPlayerComponent, &VoxelPlayerComponentDevInternal::readControllerKind );
} // namespace sw
