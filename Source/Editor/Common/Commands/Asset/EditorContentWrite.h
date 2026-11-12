/**
 * @file EditorContentWrite.h
 * @brief 에디터가 콘텐츠 파일을 쓰거나 옮기거나 지우기 전에 지나는 확인 하나입니다(읽기 전용 = 버전 관리가 잠그지 않은 파일).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"

namespace sw::editor
{
    /** @brief 확인을 거치는 동작입니다 — 알림 제목과 로그가 고른다. */
    enum class EditorContentWriteKind : uint8
    {
        Save,   ///< 씬 · 문서 패널 저장
        Move,   ///< 옮기기 · 이름 바꾸기(옮기는 파일과 고칠 참조 파일)
        Delete, ///< 휴지통 · 삭제
    };

    /**
     * @struct EditorContentWrite
     * @brief 씬 저장 · 문서 패널 저장(`IEditorDocumentPanel::saveDocumentAndClearDirty`) · 에셋 옮기기 계획 · 삭제가 모두 이 확인을 지난다.
     * @details 예전에는 씬 저장 두 곳만 읽기 전용을 물었다. 문서 저장은 묻지 않고 원자적 쓰기로 가서 POSIX `rename` 이 읽기 전용 대상을 바꿔 끼웠다(git LFS lockable 잠금이 뚫린다).
     *          언리얼은 저장 전 `PromptForCheckoutAndSave` 가 모든 패키지에 같은 길이고, 유니티 VCS 도 저장 · 옮기기 · 삭제 앞에서 체크아웃을 묻는다 — 동작마다 따로 묻지 않는 것이 그 자리다.
     *          남의 잠금은 따로 보지 않는다: LFS 는 잠근 사람 사본만 쓰기 가능으로 두므로 남이 잠근 파일은 이 사본에서 읽기 전용이다.
     *          자동 체크아웃은 두지 않는다(`EditorSourceControl` 머리말 — 잠금 서버가 없는 저장소에서 저장마다 실패가 쌓인다). 마지막 그물은 `FileUtil` 원자적 쓰기의 읽기 전용 거절이다.
     */
    struct EditorContentWrite
    {
        /** @brief @p absolutePath(파일, 또는 폴더면 그 아래 모든 파일)에서 처음 만난 읽기 전용 파일의 경로입니다. 없거나 모두 쓸 수 있으면 빈 값입니다. */
        [[nodiscard]] static optional<string> findReadOnlyAtOrUnder( string_view absolutePath );
        /**
         * @brief @p absolutePath 에 읽기 전용 파일이 있으면 알림 · 경고 로그를 남기고 true 입니다(부르는 쪽은 아무것도 쓰지 않고 실패한다).
         * @details 쓰기 실패 로그만으로는 "먼저 잠가야 한다" 는 사실이 드러나지 않는다 — 그래서 실패 대신 이유를 알린다.
         */
        [[nodiscard]] static bool refuseReadOnly( string_view absolutePath, EditorContentWriteKind kind );
    };
} // namespace sw::editor
