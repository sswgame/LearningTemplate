/**
 * @file ShadowCopyName.h
 * @brief 섀도 복사본 파일 이름과, 남은 복사본 가운데 지워도 되는 것을 다룹니다.
 *
 * @note 핫 리로드(`LiveReloadManager`)와 그 시험만 쓰고, Shipping 빌드에서 함께 빠집니다(`Source/AppHost/CMakeLists.txt` 의 제외 목록).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
    /**
     * @struct ShadowCopyName
     * @brief 섀도 복사본 파일 이름 `<모듈>_temp_p<프로세스 ID>_<번호>_<원본 시각>` 을 만들고 읽으며, 폴더에 남은 복사본을 치웁니다.
     * @details 복사본은 실행 파일 폴더에 둡니다. 그 폴더는 같은 빌드의 프로세스 여럿(App · 시험 실행 파일)이 동시에 쓰므로, 이름에 만든
     *          프로세스의 ID 를 넣어 서로 겹치지 않게 하고, 치울 때는 **다른 살아 있는 프로세스의 것**을 건드리지 않습니다 — 그 프로세스가
     *          막 써 두고 아직 올리지 않은 복사본일 수 있습니다.
     */
    struct ShadowCopyName
    {
        /** @brief 모듈 이름과 나머지를 가르는 표식입니다. 이것이 든 파일 이름만 섀도 복사본 후보입니다. */
        static constexpr const utf8* kMarker = "_temp_";
        /** @brief 표식 뒤 프로세스 ID 앞에 붙는 글자입니다. 이것이 없으면 프로세스 ID 를 넣기 전 형식입니다. */
        static constexpr utf8 kProcessPrefix = 'p';

        /** @brief 확장자 없는 복사본 이름을 만듭니다(예: `SWGame_temp_p4120_3_13435508261`). */
        static string make( string_view moduleName, int32 processId, uint32 serial, uint64 sourceMtime );

        /**
         * @brief 파일 이름(경로 가능)이 섀도 복사본인지 보고, 그렇다면 만든 프로세스 ID 를 꺼냅니다.
         * @param outProcessId 만든 프로세스 ID 입니다. 프로세스 ID 를 넣기 전 형식(`<모듈>_temp_<번호>_<시각>`)이면 0 입니다.
         * @return 두 형식 가운데 하나면 true 입니다. 표식이 없거나 뒤가 형식에 맞지 않으면 false 입니다.
         */
        [[nodiscard]] static bool parse( string_view filePath, int32& outProcessId );

        /**
         * @brief @p directoryPath 에 남은 섀도 복사본(디버그 심볼 · 쓰다 만 임시 파일 포함) 가운데 이 프로세스 · 살아 있지 않은 프로세스 ·
         *        프로세스 ID 가 없는 이름의 것을 지우고, 지운 수를 반환합니다. 다른 살아 있는 프로세스의 것은 남깁니다.
         * @details 아직 올라와 있는 복사본은 OS 가 지우기를 거절하므로(Windows) 다음 정리 때 다시 지웁니다.
         */
        static uint32 removeStaleCopies( string_view directoryPath );
    };
} // namespace sw
