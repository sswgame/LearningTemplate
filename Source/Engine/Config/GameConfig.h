#pragma once
#include "Core/Container/string.h"

#include "Engine/Config/IConfig.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @brief Dev 호스트: 활성 게임 팩 선택 (배포 콘텐츠는 Resource/.../data)
     * @details Shipping은 베이크된 JSON 기본값을 사용하며 디스크 Config/Game 을 요구하지 않는다.
     */
    REFLECT()
    struct SW_API GameConfig : IConfig
    {
        REFLECT_BODY();

        PROPERTY()
        string _packRoot{};

        PROPERTY()
        string _gameDataFile{ "data/gamedata.xml" };

        /** @brief App/EngineLoop가 로드한 활성 값을 GameInstance 부트스트랩에 전달 */
        static void              setActive( const GameConfig& config );
        static const GameConfig& getActive();
    };
} // namespace sw
