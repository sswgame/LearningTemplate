/**
 * @file DevTools/ModuleReload/LiveReload/ShadowCopyName.h
 * @brief 섀도 복사본의 자리 · 이름과, 이 프로세스가 남긴 복사본 치우기를 다룹니다.
 *
 * @note 핫 리로드(`LiveReloadManager`)와 그 테스트만 쓰고, Shipping 빌드에서 함께 빠집니다(`Source/ModuleManager/CMakeLists.txt` 의 제외 목록).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @struct ShadowCopyName
     * @brief 섀도 복사본은 실행 뿌리 아래 이 프로세스의 폴더(`getDirectory`)에 `<모듈>_<번호>_<원본 시각>` 이름으로 둡니다.
     * @details 리눅스는 이 이름의 파일, Windows 는 이 이름의 **폴더** 안에 세대 이름 파일입니다(로더가 import 를 파일 이름으로 맞추므로 파일 이름이 곧 세대 이름이다 —
     *          `ModuleShadowCopy::writeShadowCopy`). 의존 DLL 은 `Bin` · `Bin/Modules` 를 사용자 검색 폴더로 더해 찾으므로(`ModuleLibraryUtil::loadLibraryAt`) 원본 옆이 아니어도 된다.
     *          예전에는 원본 옆(`Bin/Modules`)에 `<모듈>_temp_p<pid>_…` 로 두고 시작 · 종료마다 죽은 프로세스의 것을 이름으로 골라 거뒀다. 이제 남은 복사본(Windows 에서 아직 매핑돼
     *          지우지 못한 것, 죽은 프로세스의 것)은 실행 뿌리째 주인(`SavedRetention` 의 `Automation` 규칙)이 거둔다 — 언리얼이 핫 리로드 DLL 을 새 이름으로 빌드 폴더에 남기는 것보다 거두는 자리가 분명하다.
     */
    struct ShadowCopyName
    {
        /** @brief 확장자 없는 복사본 이름을 만듭니다(예: `SWGame_3_13435508261`). */
        static string make( string_view moduleName, uint32 serial, uint64 sourceMtime );

        /**
         * @brief 이 프로세스의 섀도 복사본 폴더 `<실행 뿌리>/ModuleShadow/<pid>` 입니다. 쓰는 쪽이 처음 쓸 때 만듭니다.
         * @details pid 칸은 실행 뿌리를 물려받은 손자(`kRunRootVariable`)와 같은 이름의 복사본이 겹치지 않게 할 뿐이다 — 이름을 읽어 거두는 코드는 없다.
         */
        static string getDirectory();

        /**
         * @brief @p directoryPath 아래의 복사본(파일 · Windows 의 복사본 폴더 · 디버그 심볼 · 쓰다 만 임시 파일)을 지우고, 지운 수를 반환합니다.
         * @details 아직 올라와 있는 복사본은 OS 가 지우기를 거절하므로(Windows) 남는다 — 실행 뿌리째 주인이 거둔다.
         */
        static uint32 removeCopies( string_view directoryPath );
    };
} // namespace sw
