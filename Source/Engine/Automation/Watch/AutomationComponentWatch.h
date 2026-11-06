/**
 * @file AutomationComponentWatch.h
 * @brief 씬의 첫 컴포넌트 하나에서 수를 읽는 감시 값입니다(게임 디렉터의 탐침). "첫 컴포넌트 찾기 + 없으면 false + 값 읽기" 감싸기를 매크로 하나로 둡니다.
 * @details 게임마다 `findProbeDirector` 와 그것을 부르는 `read*` 함수 대여섯을 손으로 적었다(다섯 게임). 찾기 규칙(첫 것 · 없으면 값 없음)을 한 곳에 둔다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    /** @brief 컴포넌트 감시 값의 몸통입니다. 매크로(`SW_AUTOMATION_COMPONENT_WATCH`)가 씁니다. */
    struct AutomationComponentWatch
    {
        /** @brief 씬의 첫 @p TComponent(파생 포함)입니다. 없거나 씬이 없으면 nullptr 입니다. 단언 단계에서만 불리므로 프레임 경로가 아닙니다. */
        template <typename TComponent>
        static const TComponent* findFirst( const GameObjectManager* pManager )
        {
            const TComponent* pFound = nullptr;
            if ( pManager == nullptr )
                return nullptr;
            pManager->forEachComponentOfType<TComponent>( [&pFound]( const TComponent* pComponent )
            {
                if ( pFound == nullptr )
                    pFound = pComponent;
            } );
            return pFound;
        }

        /** @brief 첫 @p TComponent 에서 @p TRead 로 읽습니다. 컴포넌트가 없거나 @p TRead 가 false 면 값이 없습니다. */
        template <typename TComponent, bool ( *TRead )( const TComponent&, float64& )>
        [[nodiscard]] static bool read( const GameObjectManager* pManager, float64& outValue )
        {
            const TComponent* pComponent = findFirst<TComponent>( pManager );
            return pComponent != nullptr && TRead( *pComponent, outValue );
        }
    };
} // namespace sw

/**
 * @brief 씬의 첫 @p TComponent 에서 수를 읽는 감시 값을 등록합니다(.cpp 의 파일 범위, `namespace sw` 안). @p pRead 는 `bool ( const TComponent&, float64& )` 입니다.
 * @code
 *     static bool readKillCount( const ArenaGameModeComponent& director, float64& outValue ) { outValue = director.getKillCount(); return true; }
 *     SW_AUTOMATION_COMPONENT_WATCH( arenaKillCount, "Arena.KillCount", "Enemies felled this game", ArenaGameModeComponent, &readKillCount );
 * @endcode
 */
#define SW_AUTOMATION_COMPONENT_WATCH( id, pName, pTooltip, TComponent, pRead ) SW_AUTOMATION_WATCH( id, pName, pTooltip, (&::sw::AutomationComponentWatch::read<TComponent, pRead>))
