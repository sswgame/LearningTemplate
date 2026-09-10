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

        /**
         * @brief 게임이 시작할 때 여는 씬(리소스 경로). 비면 씬 없이 뜬다.
         * @details 예전엔 에디터 밖의 실행은 씬 없이 떴다 — 배포본이 씬 로드 경로(SCN1 · 프리팹 GUID 해석)를 한 번도
         *          태우지 않았다. 지금은 테스트 씬을 걸어 둔다("일단은"). 벤치(`-gv_benchMeshes`)가 켜지면 벤치 씬이
         *          우선이고, 에디터의 `-gv_editorStartupScene` 은 뒤에 큐잉되어 이긴다.
         */
        PROPERTY()
        string _startupScene{};

        /** @brief App/EngineLoop가 로드한 활성 값을 GameInstance 부트스트랩에 전달 */
        static void              setActive( const GameConfig& config );
        static const GameConfig& getActive();
    };
} // namespace sw
