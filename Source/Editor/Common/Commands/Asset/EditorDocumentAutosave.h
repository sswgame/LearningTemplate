/**
 * @file EditorDocumentAutosave.h
 * @brief 문서 패널(머티리얼 · 타일맵 · 시퀀서 …)의 자동 저장 사본과 그 복구 후보입니다(ImGui 없음). 씬 자동 저장(`EditorAutosaveCommands`)의 세션 폴더 안 `documents/` 에 쓴다.
 * @details 감사 E-1 남은 몫 — 예전에는 씬만 사본을 두어 문서 패널의 미저장 편집은 비정상 종료와 함께 사라졌다. 언리얼 Auto Save 는 dirty 패키지(맵 · 에셋)를 모두 사본으로 둔다.
 *          - 쓰는 곳: `<세션 폴더>/documents/<문서 칸>.autosave.txt`(문서 글 — 문서 되돌리기와 같은 `captureDocumentText`)와 옆의 `<문서 칸>.meta`(문서 id · 프로젝트 · 원본 해시 · 저장 시각).
 *            문서마다 가장 새 사본 하나만 둔다(세대 없음 — 문서 글은 되돌리기 기록이 세대를 대신한다).
 *          - 쓰는 일은 워커가 한다(`EditorDocumentAutosaveWriteJob`) — 글을 모으는 것만 UI 스레드다(문서 패널의 글은 UI 스레드만 읽는다).
 *          - 정리: 문서를 정상 저장하면 그 사본을 지우고(`remove`), 편집기를 끝내면 세션 폴더째 지운다(씬 자동 저장과 같다).
 *          - 복구: 다른 프로젝트의 사본, 사본 뒤에 바뀐 원본에는 적용하지 않는다(이유를 단다). 복원은 문서를 연 뒤 사본 글을 되돌리기와 같은 길로 적용해 dirty 로 둔다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Path/AbsolutePath.h"

#include "Editor/Common/Task/EditorBackgroundTask.h"

namespace sw
{
    class TaskArgs;
} // namespace sw

namespace sw::editor
{
    /** @brief 문서 사본 하나(쓸 것 또는 복구 후보)와 그 디스크 규칙입니다. 세션 폴더만 받는다 — 단위 테스트가 스크래치 폴더로 부른다. */
    struct EditorDocumentAutosaveEntry
    {
        string       _documentReference;      ///< 메타에 적힌 문서의 참조 글(복구 후보만) — GUID 참조라 훑은 뒤 문서를 옮겨도 복원이 지금 자리를 찾는다
        string       _documentPath;           ///< 문서의 리소스 id(후보면 훑은 때의 자리). 상태 문서(`_panelID` 가 있다)면 그 문서가 저장할 파일(`makeFileID`)이다
        string       _panelID;                ///< 상태 문서(프로젝트 설정 · 전역 변수 · 메시 모디파이어)의 패널 id. 비면 에셋 문서다 — 복원이 이 패널을 연다
        string       _text;                   ///< 문서 글(쓸 때만 — 복구 후보는 `_textAbs` 에서 읽는다)
        AbsolutePath _textAbs;                ///< 사본 글 파일의 디스크 경로(복구 후보)
        string       _projectRoot;            ///< 사본을 쓴 프로젝트 루트
        uint64       _sourceContentHash{ 0 }; ///< 사본을 쓸 때 원본 파일의 내용 해시(`ContentHash`). 0 은 원본이 아직 없는 새 문서다
        int64        _savedTicks{ 0 };        ///< 사본을 쓴 시각(`FileUtil::getCurrentFileWriteTime` 눈금 — 복구 후보만, 메타에서 읽는다)
        string       _refusal;                ///< 복원할 수 없는 이유(비면 복원할 수 있다)

        /** @brief 세션 폴더 안 문서 사본 폴더 이름입니다(씬 칸과 겹치지 않는다 — 씬 칸은 `.scene.xml` 사본을 든다). */
        static constexpr const utf8* kFolderName = "documents";

        /** @brief 이 사본의 글과 메타를 `<sessionFolder>/documents/` 에 씁니다(앞 사본을 덮는다). 메타를 쓰지 못하면 글도 지운다 — 어디 것인지 모르는 사본은 복구하지 않는다. */
        [[nodiscard]] bool write( const AbsolutePath& sessionFolder ) const;
        /** @brief 이 복구 후보의 사본 글을 읽습니다. 읽지 못하면 false 입니다. */
        [[nodiscard]] bool readText( string& outText ) const;
        /** @brief 이 복구 후보가 문서 @p documentPath 의 사본인지 봅니다 — 사본 파일 이름이 문서의 정체(GUID)다. */
        [[nodiscard]] bool isCopyOf( string_view documentPath ) const;
        /** @brief 문서 @p documentPath 의 사본을 지웁니다(정상 저장 · 다시 읽기 · 복원). 없으면 아무것도 하지 않는다. */
        static void remove( const AbsolutePath& sessionFolder, string_view documentPath );
        /**
         * @brief 세션 폴더들의 문서 사본을 복구 후보로 모읍니다. @p projectRoot 가 아닌 프로젝트, @p computeSourceHash 가 사본 때와 다른 원본은 거절 이유를 단다.
         * @param computeSourceHash 후보가 가리키는 원본의 내용 해시(없으면 0). 테스트는 고정 값을 준다.
         */
        static void scan( const vector<AbsolutePath>& listSessionFolder, string_view projectRoot, uint64 ( *computeSourceHash )( const EditorDocumentAutosaveEntry& entry ),
                          vector<EditorDocumentAutosaveEntry>& outListEntry );
        /** @brief 상태 문서가 저장할 파일 @p absolutePath 의 사본 id 입니다 — 프로젝트 아래면 프로젝트 기준 경로, 밖이면 그 경로 그대로입니다. */
        [[nodiscard]] static string makeFileID( string_view absolutePath );
        /** @brief 사본 id @p fileID(`makeFileID`)가 가리키는 디스크 파일입니다. 풀 수 없으면 없음입니다. */
        [[nodiscard]] static optional<AbsolutePath> resolveFileID( string_view fileID );
    };

    /** @brief 워커가 쓸 사본 묶음입니다 — 세션 폴더와 문서 사본들. */
    struct EditorDocumentAutosaveBatch
    {
        AbsolutePath                        _sessionFolder;
        vector<EditorDocumentAutosaveEntry> _listEntry;
    };

    /**
     * @class EditorDocumentAutosaveWriteJob
     * @brief 문서 사본 묶음을 워커에서 쓰고 쓴 수를 게임 스레드에 돌려줍니다(감사 E-1 — 쓰기를 UI 스레드에서 뗀다).
     * @details 낸 잡이 끝나기 전에는 새로 내지 않는다(`take` 가 Running 이면 다음 주기로 미룬다) — 새 세대가 앞 묶음을 버려 사본이 빠지지 않게.
     */
    class EditorDocumentAutosaveWriteJob final : public EditorBackgroundTask<EditorDocumentAutosaveBatch, uint32>
    {
    public:
        /** @brief 묶음 @p batch 를 쓰게 냅니다. */
        void request( EditorDocumentAutosaveBatch batch );

    private:
        static void runJob( const TaskArgs& args );
    };
} // namespace sw::editor
