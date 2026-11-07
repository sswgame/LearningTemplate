/**
 * @file WesternGameModeComponent.h
 * @brief 서부극 판을 여는 디렉터의 기반입니다. 키트 컴포넌트(말)가 판의 서부극 카탈로그를 디렉터에게 묻는 자리입니다.
 */
#pragma once
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Framework/GameModeComponent.h"

namespace sw
{
    class WesternCatalog;
    class WesternOutlaw;

    /**
     * @class WesternGameModeComponent
     * @brief 판을 연 디렉터가 읽은 서부극 카탈로그(`<Horse>` 품종 · 유대 · 생존)를 답합니다. 게임의 디렉터가 이것을 상속합니다.
     * @details 키트의 말 컴포넌트(`WesternHorseComponent`)가 게임 디렉터 형식을 모르고 카탈로그를 찾게 한다(RTTI 를 끄고 빌드해 인터페이스 캐스트 대신 기반 형식).
     */
    REFLECT( Abstract, Category = "Western", DisplayName = "Western Director", Tooltip = "Base of the game directors that open a western catalog" )
    class SW_KIT_API WesternGameModeComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();

        /** @brief 판이 열렸으면 읽은 서부극 카탈로그이고, 아니면 nullptr 입니다. */
        virtual const WesternCatalog* findWesternCatalog() const = 0;
        /** @brief 판이 열렸으면 플레이어의 명예 · 법 · 생존 · 사냥이고, 아니면 nullptr 입니다(감시 값이 읽는다). */
        virtual const WesternOutlaw* findOutlaw() const = 0;
    };
} // namespace sw
