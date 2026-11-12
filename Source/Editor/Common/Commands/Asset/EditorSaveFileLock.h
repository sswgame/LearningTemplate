/**
 * @file EditorSaveFileLock.h
 * @brief 거절 시나리오(`panel.lockSaveFiles` · `panel.unlockSaveFiles`)가 읽기 전용으로 잠근 파일을 기억하고 반드시 되돌리는 자리입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"

namespace sw::editor
{
    /**
     * @struct EditorSaveFileLock
     * @brief 잠금은 에디터 범위의 상태가 쥐고, 그 상태가 사라질 때(시나리오 통과 · 실패 · 시간 초과 · 창 닫힘의 종료, 체인 단계 초기화의 `EditorStateStore::resetAll`) 쓰기를 되돌린다.
     * @details 잠그기 전에 목록을 장부 파일(자동화 출력 폴더)에 먼저 적는다 — 프로세스가 죽어 소멸자가 돌지 못해도 다음 잠금 · 체인 단계 초기화(`releaseStaleLocks`)가 푼다.
     *          예전에는 명령이 잠금을 상태에만 두어, 시나리오가 `panel.unlockSaveFiles` 전에 끊기면 저장소의 설정 파일이 읽기 전용으로 남았다.
     *          유니티 테스트의 `TearDown` · 언리얼 Automation 의 정리 단계처럼 단계가 어떻게 끝나든 부작용을 되돌리는 자리다.
     */
    struct EditorSaveFileLock
    {
        /** @brief @p listPath 의 지금 글을 붙잡고 있는 파일을 모두 읽기 전용으로 둡니다. 앞 잠금이 남아 있으면 먼저 풉니다. 하나라도 못 읽거나 못 잠그면 이유이고 잠근 것은 되돌립니다. */
        [[nodiscard]] static expected<void> lock( const vector<string>& listPath );
        /** @brief 잠근 파일의 쓰기를 모두 되돌리고 장부를 지웁니다. 잠긴 동안 글이 바뀐(없던 파일이 생긴) 첫 경로를 돌려주고, 모두 그대로면 빈 값입니다. */
        [[nodiscard]] static optional<string> unlock();
        /** @brief 앞 프로세스가 장부에 남긴 잠금을 풉니다(장부가 없으면 할 일이 없다). 체인 단계 초기화와 `lock` 이 부른다. */
        static void releaseStaleLocks();
        /** @brief 지금 잠근 파일 수입니다(테스트 확인용). */
        [[nodiscard]] static uint32 getLockedCount();
    };
} // namespace sw::editor
