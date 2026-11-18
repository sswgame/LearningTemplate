/**
 * @file EditorAutosaveCommands.h
 * @brief 씬 자동 저장과 비정상 종료 뒤 복구입니다(ImGui 없음). 원본을 덮지 않는 세대별 사본을 에디터 상태 폴더에 두고, 다음 기동에서 복구를 묻는다.
 * @details 언리얼 Auto Save(`Saved/Autosaves` 의 사본 · 다음 기동의 Restore 대화상자) · 유니티의 `Temp/__Backupscenes` 와 같은 자리다.
 *          - 쓰는 곳: `<에디터 상태 폴더>/Autosave/<pid>-<기동 시각>/<씬 칸>/<세대>.scene.xml` 과 옆의 `<세대>.meta`(프로젝트 · 원본 id · 원본 수정 시각 · 저장 시각).
 *            세션(에디터 프로세스 하나)마다 폴더 하나와 잠금 `session.lock`(pid · 기동 시각)이다 — 두 에디터가 서로의 사본 · 잠금을 지우지 않는다(유니티 프로세스별 `Temp/__Backupscenes`).
 *          - 쓰는 때: 씬이 dirty 이고 지난 사본 뒤 편집이 있었고 주기(환경설정 Editor/Autosave)가 지났을 때. Play 중에는 쓰지 않는다. 원본이 없는 새 씬은 `untitled-<씬 세대>` 칸에 쓰고, 복원하면 소스 없는 새 씬으로 열린다(저장은 다른 이름 저장).
 *          - 정리: 그 씬을 정상 저장하면 그 씬의 사본을, 편집기를 끝내면 자기 세션 폴더(넘겨받은 것 포함)를 지운다. 세대는 환경설정 수만 남긴다.
 *            모듈 리로드 · 백엔드 교체는 세션 id 를 호스트 맡김 칸(`IModuleStateStore`)에 맡겨 같은 폴더에 이어 쓴다 — 리로드는 가장 죽기 쉬운 순간이라 사본을 지우지 않는다.
 *          - 비정상 종료: 기동 때 잠금의 pid 가 죽은 세션 폴더만 지난 세션이 끝나지 않은 것이다 — 이 세션이 넘겨받고(잠금을 고쳐 쓴다) 남은 사본을 복구 후보로 둔다.
 *          - 복구: 다른 프로젝트의 사본, 사본 뒤에 바뀐 원본에는 적용하지 않는다(이유를 보인다). 복원은 사본을 열고 소스를 원래 id 로 돌린 뒤 씬을 dirty 로 둔다 —
 *            원본은 사용자가 저장할 때만 바뀌고, 되돌리기 기록은 새 씬과 같이 빈 채로 시작한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

namespace sw::editor
{
    struct EditorDocumentAutosaveEntry;

    /** @brief 복구 후보 하나(씬 하나의 가장 새 사본)입니다. */
    struct EditorAutosaveEntry
    {
        string _snapshotAbs;            ///< 가장 새 사본의 디스크 경로
        string _snapshotID;             ///< 그 사본의 리소스 id(`autosave/…` — 붙인 도메인)
        string _sourceID;               ///< 원래 씬의 리소스 id. 비면 저장한 적 없는 새 씬이다
        string _projectRoot;            ///< 사본을 쓴 프로젝트 루트
        int64  _savedTicks{ 0 };        ///< 사본을 쓴 시각(`FileUtil::getCurrentFileWriteTime` 눈금)
        uint64 _sourceContentHash{ 0 }; ///< 그때 원본 파일의 내용 해시(`ContentHash`). 0 은 메타에 없음 — 바뀐 원본으로 거절한다
        uint32 _generationCount{ 0 };   ///< 남은 세대 수
        string _refusal;                ///< 복원할 수 없는 이유(비면 복원할 수 있다)
    };

    /** @brief 세션을 내리는 까닭입니다. 편집기 종료만 폴더를 지운다. */
    enum class EditorAutosaveSessionEnd : uint8
    {
        ModuleReload, ///< 모듈 리로드 · 백엔드 교체 — 같은 프로세스의 다음 인스턴스가 이어 쓴다
        EditorExit,   ///< 편집기 종료(호스트가 `requestBackgroundStop` 으로 알렸다)
    };

    /**
     * @struct EditorAutosaveSession
     * @brief 자동 저장 세션 하나(에디터 프로세스 pid 와 세션을 연 시각)와 그 폴더의 디스크 규칙입니다. 폴더 이름 `<pid>-<시각>` 이 된다.
     * @details 씬 · 에디터 상태 없이 루트 폴더만 받는다 — 단위 테스트가 스크래치 폴더로 부른다.
     */
    struct EditorAutosaveSession
    {
        /** @brief 세션 잠금 파일 이름입니다(세션 폴더 안). */
        static constexpr const utf8* kLockFileName = "session.lock";

        int32 _processID{ 0 };  ///< 세션을 연 에디터 프로세스
        int64 _startTicks{ 0 }; ///< 세션을 연 시각(`FileUtil::getCurrentFileWriteTime` 눈금) — 같은 pid 를 OS 가 다시 쓴 세션과 가른다

        /** @brief 이 세션의 폴더 `<root>/<pid>-<시각>` 입니다. */
        string makeFolder( string_view root ) const;
        /**
         * @brief 이 세션을 엽니다 — 자기 폴더와 잠금을 쓰고, 잠금 pid 가 죽은 세션 폴더를 넘겨받는다(잠금을 이 세션으로 고쳐 쓴다).
         * @param outListRecoveredFolder 넘겨받은 폴더(앞서 이 세션이 넘겨받은 것 포함 — 리로드 뒤에도 복구 후보를 다시 보인다). 살아 있는 다른 세션은 넣지 않는다.
         * @return 자기 폴더 · 잠금을 쓰지 못하면 false 입니다.
         */
        [[nodiscard]] bool open( string_view root, vector<string>& outListRecoveredFolder ) const;
        /** @brief 편집기 종료입니다 — 잠금 pid 가 @p processID 인 폴더(자기 것 + 넘겨받은 것)만 지운다. 다른 에디터의 폴더는 그대로다. */
        static void removeOwned( string_view root, int32 processID );
        /** @brief 세션 폴더 @p sessionFolder 의 잠금을 읽습니다. 없거나 깨졌으면 false 입니다. */
        [[nodiscard]] static bool readLock( string_view sessionFolder, EditorAutosaveSession& outSession );
    };

    /**
     * @struct EditorAutosaveCommands
     * @brief 자동 저장 · 복구의 입구입니다. 상태는 에디터 상태 저장소가 들고, 디스크 정리는 상태 없이도 된다(`endSession` — 잠금의 pid 로 자기 폴더를 찾는다).
     */
    struct EditorAutosaveCommands
    {
        /** @brief 사본을 붙이는 리소스 도메인 이름입니다(`autosave/<씬 칸>/<세대>.scene.xml`). */
        static constexpr const utf8* kDomain = "autosave";

        /** @brief 게임 단계가 프레임마다 부릅니다 — 첫 프레임에 세션을 열고(잠금 · 복구 후보), 복원이 끝난 씬의 소스를 돌리고, 요청된 복원을 열고, 주기가 되면 사본을 쓴다. */
        static void tick();
        /** @brief 세션을 내립니다 — 도메인을 떼고, 편집기 종료일 때만 자기 세션 폴더를 지운다. 에디터 데이터가 내려가기 직전에 부른다(에디터 상태 저장소는 이미 없다). */
        static void endSession( EditorAutosaveSessionEnd reason );
        /** @brief 지금 씬의 사본을 바로 씁니다(주기 무시 — 개발 명령 `autosave.now`). 썼으면 true 입니다. */
        static bool writeSnapshotNow();
        /** @brief 씬 @p sourceID 를 정상 저장했습니다 — 그 씬의 사본을 지운다(남겨 두면 다음 기동이 이미 저장한 일을 복구하라고 묻는다). */
        static void onSceneSaved( string_view sourceID );

        /** @brief 복구 후보입니다. 지난 세션이 비정상 종료했을 때만 찬다. */
        static const vector<EditorAutosaveEntry>& getRecoverable();
        /** @brief 지금 사본 폴더를 복구 후보로 다시 읽습니다(개발 명령 `autosave.rescan` — 비정상 종료 시나리오). 후보 수입니다. */
        static uint32 refreshRecoverable();
        /**
         * @brief UI 쪽 — 후보 @p index 의 복원을 요청합니다. 실행은 게임 단계(`tick`)가 한다: 씬 열기 입구(`tryOpenScene`: Play 중 거절 · 미저장 확인)로 사본을 열고,
         *        열리면 소스를 원래 id 로 돌리고 dirty 로 두고 후보에서 뺀다. 확인에서 취소하면 후보는 남는다. 거절 이유는 그때 알린다.
         * @details 요청은 번호가 아니라 사본 id 로 붙잡는다 — 실행 전에 다른 줄을 지우면 번호가 당겨진다. 번호가 범위 밖이면 false 입니다.
         */
        static bool requestRestore( uint32 index );
        /** @brief @p listEntry 에서 사본 id(`normalizeAssetID` 로 견준다)가 @p snapshotID 인 후보의 번호를 찾습니다. 없으면 false 입니다(그 사이 지웠거나 복원했다). */
        [[nodiscard]] static bool findRecoverableIndex( const vector<EditorAutosaveEntry>& listEntry, string_view snapshotID, uint32& outIndex );
        /** @brief 후보 @p index 의 사본을 지웁니다. */
        static void discard( uint32 index );
        /** @brief 이번 세션에 쓴 사본 수입니다(감시 값 `Editor.AutosaveSnapshotCount`). */
        static uint32 getWrittenSnapshotCount();
        /** @brief 이번 세션에 복원을 끝낸 수입니다(감시 값 `Editor.AutosaveRestoredCount`). */
        static uint32 getRestoredCount();
        /** @brief 이 세션의 사본 폴더입니다. 세션을 아직 열지 않았거나 열지 못했으면(또는 에디터 상태가 없는 호스트면) 빈 글입니다 — 문서 사본(`EditorDocumentAutosaveEntry`)도 이 안에 쓴다. */
        static string getSessionFolder();
        /** @brief 문서 패널의 복구 후보입니다(감사 E-1). 씬 후보와 같이 지난 세션이 비정상 종료했을 때(또는 `autosave.rescan`)만 찬다. */
        static const vector<EditorDocumentAutosaveEntry>& getRecoverableDocuments();
        /**
         * @brief 문서 후보 @p index 를 복원합니다 — 문서를 워크스페이스 포커스로 열게 하고, 그 문서 패널이 읽으면(`takePendingDocumentRestore`) 사본 글을 되돌리기와 같은 길로 적용해 dirty 로 둔다.
         * @details 거절 이유가 있거나 사본 글을 읽지 못하면 false 입니다. 다른 문서를 고치고 있었으면 미저장 확인은 문서 열기 길이 한다.
         */
        static bool restoreDocument( uint32 index );
        /** @brief 문서 후보 @p index 의 사본을 지웁니다. */
        static void discardDocument( uint32 index );
        /**
         * @brief 문서 패널이 @p documentPath 를 읽은 직후 부릅니다 — 그 문서의 복원 청이 있으면 사본 글을 @p outText 에 꺼내고 true 입니다(청과 사본은 지운다).
         * @details 에디터 상태가 없는 호스트(문서 패널 테스트)면 false 다.
         */
        [[nodiscard]] static bool takePendingDocumentRestore( string_view documentPath, string& outText );
        /** @brief 이번 세션에 문서 복원을 끝낸 수입니다(감시 값 `Editor.AutosaveDocumentRestoredCount`). */
        static uint32 getRestoredDocumentCount();
        /** @brief 씬 리소스 id 를 사본 폴더 이름(`[a-z0-9_.-]`)으로 바꿉니다. 같은 id 는 같은 이름입니다. */
        static string makeSlotName( string_view sourceID );
        /** @brief 사본 옆 메타 글(`key=value` 줄들)에서 @p key 의 값을 찾습니다. 없으면 빈 글입니다. 씬 사본 · 문서 사본 · 세션 잠금 파일이 같은 모양이다. */
        [[nodiscard]] static string findMetaValue( string_view metaText, string_view key );
        /** @brief 리소스 id @p sourceID 의 원본 파일 내용 해시입니다. 풀지 못하거나 아직 없으면(새 문서) 0 입니다 — 사본을 쓸 때와 복구할 때 같은 규칙으로 견준다. */
        [[nodiscard]] static uint64 computeSourceHash( string_view sourceID );
    };
} // namespace sw::editor
