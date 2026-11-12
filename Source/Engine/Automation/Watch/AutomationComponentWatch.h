/**
 * @file AutomationComponentWatch.h
 * @brief 씬의 첫 컴포넌트 하나에서 수를 읽는 감시 값입니다(게임 디렉터의 탐침). "첫 컴포넌트 찾기 + 없으면 false + 값 읽기" 감싸기를 매크로 하나로 둡니다.
 * @details 게임마다 `findProbeDirector` 와 그것을 부르는 `read*` 함수 대여섯을 손으로 적었다(다섯 게임). 찾기 규칙(첫 것 · 없으면 값 없음)을 한 곳에 둔다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"

namespace sw
{
    /** @brief 컴포넌트 감시 값의 몸통입니다. 매크로(`SW_AUTOMATION_COMPONENT_WATCH`)가 씁니다. */
    struct AutomationComponentWatch
    {
        /** @brief 씬의 첫 @p TComponent(파생 포함)입니다. 없거나 씬이 없으면 nullptr 입니다. 단언 단계에서만 불리므로 프레임 경로가 아닙니다 — 열쇠는 감시 값 레지스트리가 넘긴다. */
        template <typename TComponent>
        static const TComponent* findFirst( SceneWalkKey key, const GameObjectManager* pManager )
        {
            const TComponent* pFound = nullptr;
            if ( pManager == nullptr )
                return nullptr;
            pManager->forEachComponentOfType<TComponent>( key, [&pFound]( const TComponent* pComponent )
            {
                if ( pFound == nullptr )
                    pFound = pComponent;
            } );
            return pFound;
        }

        /** @brief 첫 @p TComponent 에서 @p TRead 로 읽습니다. 컴포넌트가 없거나 @p TRead 가 false 면 값이 없습니다. */
        template <typename TComponent, bool ( *TRead )( const TComponent&, float64& )>
        [[nodiscard]] static bool read( SceneWalkKey key, const GameObjectManager* pManager, float64& outValue )
        {
            const TComponent* pComponent = findFirst<TComponent>( key, pManager );
            return pComponent != nullptr && TRead( *pComponent, outValue );
        }

        /** @brief 첫 @p TComponent 의 인자 없는 const 게터(수 · bool · 열거) @p TGetter 값입니다. 컴포넌트가 없으면 값이 없습니다. */
        template <typename TComponent, auto TGetter>
        [[nodiscard]] static bool readGetter( SceneWalkKey key, const GameObjectManager* pManager, float64& outValue )
        {
            const TComponent* pComponent = findFirst<TComponent>( key, pManager );
            if ( pComponent == nullptr )
                return false;
            outValue = static_cast<float64>( ( pComponent->*TGetter )() );
            return true;
        }
    };
} // namespace sw

/**
 * @brief 씬의 첫 @p TComponent 에서 수를 읽는 감시 값을 등록합니다(.cpp 의 파일 범위, `namespace sw` 안). @p pRead 는 `bool ( const TComponent&, float64& )` 입니다.
 * @code
 *     static bool readStrideScale( const ShooterAvatarComponent& avatar, float64& outValue ) { if ( avatar.getStrideAppliedCount() == 0 ) return false; outValue = avatar.getLastStrideScale(); return true; }
 *     SW_AUTOMATION_COMPONENT_WATCH( shooterStrideScale, "Shooter3D.StrideScale", "Stride scale; absent until the rig is bound", ShooterAvatarComponent, &readStrideScale );
 * @endcode
 */
#define SW_AUTOMATION_COMPONENT_WATCH( id, pName, pTooltip, TComponent, pRead ) SW_AUTOMATION_WALK_WATCH( id, pName, pTooltip, (&::sw::AutomationComponentWatch::read<TComponent, pRead>))

/**
 * @brief 씬의 첫 @p TComponent 의 const 게터 하나를 그대로 읽는 감시 값을 등록합니다. 값을 가공하거나 "값 없음" 을 고르면 `SW_AUTOMATION_COMPONENT_WATCH` 를 씁니다.
 * @code
 *     SW_AUTOMATION_GETTER_WATCH( arenaKillCount, "Arena.KillCount", "Enemies felled this game", ArenaGameModeComponent, &ArenaGameModeComponent::getKillCount );
 * @endcode
 */
#define SW_AUTOMATION_GETTER_WATCH( id, pName, pTooltip, TComponent, pGetter ) SW_AUTOMATION_WALK_WATCH( id, pName, pTooltip, (&::sw::AutomationComponentWatch::readGetter<TComponent, pGetter>))
