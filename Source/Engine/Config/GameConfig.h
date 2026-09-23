#pragma once
#include "Core/Container/string.h"

#include "Engine/Config/IConfig.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @brief Dev 호스트의 활성 게임 팩 선택입니다(배포 콘텐츠는 Resource/.../data).
     * @details Shipping 은 베이크된 JSON 기본값을 쓰며 디스크의 Config/Game 을 요구하지 않습니다.
     */
    REFLECT()
    struct SW_API GameConfig : IConfig
    {
        REFLECT_BODY();

        PROPERTY()
        string _packRoot{};

        PROPERTY()
        string _gameDataFile{ "data/gamedata.xml" };

        /**
         * @brief 게임이 시작할 때 여는 씬(리소스 경로)입니다. 비면 씬 없이 뜹니다.
         * @details 예전에는 에디터 밖의 실행이 씬 없이 떴습니다. 배포본이 씬 로드 경로(SCN1 · 프리팹 GUID 해석)를 한 번도
         *          거치지 않았습니다. 지금은 테스트 씬을 걸어 둡니다(임시). 벤치(`-gv_benchMeshes`)가 켜지면 벤치 씬이
         *          우선이고, 에디터의 `-gv_editorStartupScene` 은 나중에 큐에 들어가므로 결국 그것이 열립니다.
         */
        PROPERTY()
        string _startupScene{};

        /** @brief App/EngineLoop 가 읽은 활성 값을 GameInstance 부트스트랩에 전달합니다. */
        static void              setActive( const GameConfig& config );
        static const GameConfig& getActive();
    };
} // namespace sw
