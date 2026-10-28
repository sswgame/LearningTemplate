/**
 * @file IntentPlayback.h
 * @brief 의도 기록(`ControlIntentHistory`, `.swintent`)을 씬의 폰들에게 한 번에 재생하는 엔진 쪽 진입점입니다.
 * @details 기록의 트랙마다 같은 이름의 폰을 찾아 기록 조종자(`IntentTrackControllerComponent`)를 생성하고 `play` 합니다. 재생 UI, 카메라, 되감기와
 *          기록을 켜고 파일로 쓰는 명령줄과 에디터 버튼은 이 진입점을 부르는 쪽(게임, 에디터)이 맡습니다. 재생은 기록과 같은 씬을 처음부터 같은 고정 프레임 시간으로
 *          돌릴 때만 같은 궤적입니다(`ControlIntentHistory`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class ControlIntentHistory;
    class GameObjectManager;

    /**
     * @struct IntentPlayback
     * @brief 기록 재생을 시작하는 함수 모음입니다. 상태가 없습니다.
     */
    struct SW_GF_API IntentPlayback
    {
        /** @brief 폰마다 생성하는 기록 조종자 오브젝트 이름의 접두사입니다(뒤에 폰 이름). 시나리오 `<Intent>` 와 같은 조종자를 재사용합니다. */
        static constexpr string_view kControllerPrefix = "IntentTrack.";

        /**
         * @brief @p history 의 트랙마다 같은 이름의 폰에 기록 조종자를 생성하고 다음 틱부터 재생합니다.
         * @param bReturnWhenDone 참이면 트랙이 끝난 뒤 폰을 원래 조종자에게 돌려줍니다. 거짓이면 멈춘 채 빙의를 유지합니다.
         * @param firstIndex 트랙의 이 번째 틱부터 냅니다(0 = 처음부터). 되감기가 키 틱에서 이어 돌 때 씁니다. 트랙 길이 이상이면 그 트랙은 이미 끝난 것이라 건드리지 않습니다.
         * @param pOutSkippedCount 씬에 폰이 없거나 트랙에 빈 틱이 있어 재생하지 못한 트랙 수를 받습니다(nullptr 이면 안 받음).
         * @return 재생을 시작한 트랙 수입니다.
         */
        static uint32 start( GameObjectManager& manager, const ControlIntentHistory& history, bool bReturnWhenDone, uint32 firstIndex, uint32* pOutSkippedCount = nullptr );
        /**
         * @brief `.swintent` 파일을 읽어 `start` 합니다. 파일을 읽지 못하면 @p outError 를 채우고 false 입니다(재생한 트랙이 0 이어도 읽었으면 true).
         * @param pOutStartedCount 시작한 트랙 수를 받습니다(nullptr 이면 안 받음).
         */
        [[nodiscard]] static bool startFromFile( GameObjectManager& manager, string_view path, bool bReturnWhenDone, string& outError, uint32* pOutStartedCount = nullptr );
    };
} // namespace sw
