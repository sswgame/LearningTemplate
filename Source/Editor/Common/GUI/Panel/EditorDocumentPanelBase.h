/**
 * @file EditorDocumentPanelBase.h
 * @brief 문서를 가진 에디터 패널의 기반입니다 — 미저장 비트, 저장 · 되돌리기, 닫기 확인을 여기만 가집니다.
 * @details 언리얼 `SDockTab`(패널 UI)과 `FAssetEditorToolkit`(문서) 을 나눈 자리다. 예전에는 `EditorPanel` 이 문서 함수 여섯을 기본 구현과 함께 가져,
 *          패널이 `markDocumentDirty` 만 부르고 `saveDocument` 를 빠뜨려도 컴파일됐다(Ctrl+S 가 조용히 실패하고 미저장이 영영 남는다).
 *          이제 문서 없는 패널은 미저장을 표시할 수 없고(`EditorPanel` 에 없다), 문서 패널은 저장 · 되돌리기를 반드시 구현한다(순수 가상).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"

#include "Editor/Common/GUI/Panel/EditorPanel.h"

namespace sw::editor
{
    /** @brief 문서 자동 저장 사본을 꺼낸 결과입니다(`EditorDocumentPanelBase::takeAutosaveCopy`). */
    enum class DocumentAutosaveResult : uint8
    {
        Nothing,  ///< 쓸 것이 없다(깨끗하다 · 지난 사본과 같다 · 사본을 두지 않는 문서)
        Deferred, ///< 끌기 · 글 칸 같은 상호작용 구간 중이다 — 구간 중간 상태를 사본으로 두지 않게 다음 프레임으로 미룬다
        Ready     ///< 사본으로 쓸 문서 id 와 글을 꺼냈다
    };

    /**
     * @class EditorDocumentPanelBase
     * @brief 저장할 문서를 가진 패널입니다. 파생은 `saveDocument` · `revertDocument` 를 구현하고 편집을 `markDocumentDirty` 로 알립니다.
     */
    class SW_EDITOR_API EditorDocumentPanelBase : public EditorPanel
    {
    public:
        EditorDocumentPanelBase*       asDocumentPanel() final { return this; }
        const EditorDocumentPanelBase* asDocumentPanel() const final { return this; }

        /**
         * @brief 저장되지 않은 편집이 있으면 true 입니다.
         * @details 가상 함수가 아니다. 비트를 기반 클래스가 보관하므로, 패널이 "* Unsaved changes" 를 화면에 띄우면서 Ctrl+S 와
         *          종료 확인에는 나타나지 않는 반쪽짜리 구현이 불가능하다.
         */
        bool isDocumentDirty() const { return _bDocumentDirty; }
        /**
         * @brief 마지막 편집 알림이 문서 전체 글을 만든 시간(us)입니다(감시 값 `Editor.DocumentCaptureUs`). 만든 적이 없으면 0 입니다.
         * @details 되돌리기의 차이 저장(편집당 1 ms 초과 또는 기록 16 MB 초과 때)을 판정할 재는 장치다 — 단계는 이미 바뀐 구간만 들고 글은 한 벌만 든다.
         */
        uint64 getLastCaptureMicroseconds() const { return _lastCaptureMicroseconds; }
        /**
         * @brief `saveDocument()` 를 호출하고 성공했을 때만 dirty 를 지웁니다.
         * @details 모든 저장 경로가 이 함수를 거친다. 파생 클래스가 각자 구현하면 실패해도 dirty 를 지워 편집이 조용히 사라지거나, 성공해도 계속 미저장으로 남는다.
         *          저장이 쓸 파일(`collectFilesToSave`) 중 하나라도 읽기 전용(버전 관리가 잠그지 않음)이면 `saveDocument` 를 부르지 않고 알린다 — 씬 저장 · 옮기기 · 삭제와 같은 확인(`EditorContentWrite::warnIfReadOnly`)이다.
         * @return 저장에 성공했으면 true 입니다. false 이면 dirty 는 그대로 남습니다.
         */
        [[nodiscard]] bool saveDocumentAndClearDirty();
        /** @brief dirty 이면 저장합니다. 이미 깨끗하거나 저장에 실패하면 false 입니다. */
        [[nodiscard]] bool trySaveDirtyDocument()
        {
            if ( _bDocumentDirty == false )
                return false;
            return saveDocumentAndClearDirty();
        }
        /**
         * @brief dirty 와 무관하게 디스크(또는 기본값)에서 문서를 다시 읽고 dirty 를 지웁니다(`revertDocument`).
         * @details 저장 왕복 확인(`panel.reloadDocument` — 저장 → 다시 읽기 → 저장이 같은 바이트인지)이 쓴다. 편집을 버리는 사용자 동작은 `discardDirtyDocument` 다.
         */
        void reloadDocument()
        {
            revertDocument();
            clearDocumentDirty();
        }
        /** @brief dirty 이면 revertDocument() 를 호출하고 dirty 를 지웁니다. */
        void discardDirtyDocument()
        {
            if ( _bDocumentDirty == false )
                return;
            revertDocument();
            clearDocumentDirty();
        }
        /**
         * @brief 연 에셋 파일의 리소스 id 입니다. 연 경로가 없는 문서(`EditorStateDocumentPanel`)와 아직 연 문서가 없는 패널은 빈 글입니다.
         * @details 두 기반이 각각 정한다(순수 가상) — 옮기기 · 삭제 계획(`IEditorPanelService::collectOpenDocuments`)이 연 문서를 묻는 길이다(감사 P0-4).
         */
        [[nodiscard]] virtual string_view getDocumentAssetPath() const = 0;
        /**
         * @brief 지금 `saveDocument` 가 쓸 파일(디스크 절대 경로)을 @p outListPath 에 더합니다. `saveDocumentAndClearDirty` 가 쓰기 전에 하나씩 읽기 전용을 확인하고, 거절 시나리오(`panel.lockSaveFiles`)가 같은 목록을 잠근다.
         * @details 두 기반이 정한다(순수 가상) — 에셋 문서(`EditorDocumentPanel`)는 연 파일이 기본이고 다른 파일을 쓰는 패널이 재정의하며, 상태 문서(`EditorStateDocumentPanel`)는 패널마다 반드시 적는다.
         *          빠뜨리면 그 파일은 확인 없이 Core 원자적 쓰기에서 알림 없이 진다. 유니티 `AssetModificationProcessor.OnWillSaveAssets( paths )` 가 저장할 경로 목록으로 체크아웃을 묻는 자리다.
         */
        virtual void collectFilesToSave( vector<string>& outListPath ) const = 0;
        /** @brief 닫기 확인(Save / Don't Save / Cancel)을 기다리는 중이면 true 입니다. */
        bool isCloseConfirmPending() const { return _bConfirmClose; }
        /**
         * @brief 닫기 확인에서 고른 항목을 적용합니다. 선택이 없으면 아무 일도 하지 않습니다.
         * @details Save 는 저장에 성공해야 닫는다(실패하면 열어 두어 편집을 지키고 확인도 끝낸다). Don't Save 는 `revertDocument` 로 편집을 버린 뒤 닫고, Cancel 은 열어 둔다.
         */
        void applyCloseChoice( EditorUnsavedChoice choice );
        /**
         * @brief 되돌리기가 이 문서를 그대로 돌리는지 봅니다 — 지금 문서 글을 되돌리기와 같은 길로 다시 적용하고, 다시 직렬화한 글이 같아야 한다.
         * @details 되돌리기 전수 시나리오(`editor/undocoverage` 의 문서 패널 단계 — `panel.expectUndoRoundTrip`)가 부른다(감사 E-16). 문서 되돌리기는 구간 전 글을
         *          적용해 돌리므로, 적용 → 직렬화가 같은 글이 아니면 Ctrl+Z 가 문서를 다른 상태로 돌린다. 두 기반이 정한다(순수 가상) — 상태 문서는 기록을
         *          자기 모델에 두어 글 왕복이 없다고 답한다. 언리얼 트랜잭션 테스트 · 유니티 Undo 테스트의 실행 → 되돌리기 → 상태 비교와 같은 자리다.
         */
        [[nodiscard]] virtual expected<void> validateUndoRoundTrip() = 0;
        /**
         * @brief 자동 저장 사본으로 쓸 문서 id 와 글을 꺼냅니다(감사 E-1 — 문서 패널 자동 저장). 꺼냈으면 같은 글은 다시 내지 않는다.
         * @details 두 기반이 정한다(순수 가상) — 에셋 문서는 연 문서의 글(문서 되돌리기와 같은 `captureDocumentText`), 상태 문서는 기록을 자기 모델에 두어 사본이 없다고 답한다.
         *          언리얼 `FPackageAutoSaver` 가 dirty 패키지를 모두 사본으로 두고, 끌기 같은 상호작용 중이면 미루는 것과 같은 자리다.
         */
        [[nodiscard]] virtual DocumentAutosaveResult takeAutosaveCopy( string& outDocumentPath, string& outText ) = 0;

    private:
        friend class EditorPanel; // 셸(`draw` · `requestClose`)이 문서 상태를 묻고 확인을 띄운다
        // 문서 패널의 기반은 이 둘뿐이다 — 생성자를 둘에만 연다. 에셋 파일 하나를 고치는 패널이 직접 상속해 자체 열기 길과 문서 경로를 따로 들던 모양을 형식으로 막는다.
        friend class EditorDocumentPanel;      // 에셋 파일 하나(워크스페이스 포커스로 연다)
        friend class EditorStateDocumentPanel; // 연 문서 경로가 없는 상태 · 설정 묶음

        /** @brief 기본 열림 상태로 문서 패널을 만듭니다. 두 기반(`EditorDocumentPanel` · `EditorStateDocumentPanel`)만 부른다. */
        explicit EditorDocumentPanelBase( bool bOpenByDefault )
            : EditorPanel{ bOpenByDefault }
            , _bDocumentDirty{ SW_FALSE }
            , _bConfirmClose{ SW_FALSE }
            , _lastCaptureMicroseconds{ 0 }
        {
        }

    protected:
        /**
         * @brief 닫을 때 미저장 편집을 확인해야 하면 true 입니다. 기본은 true 입니다.
         * @details 편집이 패널 밖(전역 변수 관리자처럼 바로 적용되는 곳)에 살아 있어서 패널을 닫아도 아무것도 잃지 않는 패널만 false 로 재정의한다.
         */
        virtual bool shouldAskBeforeClose() const { return true; }
        /**
         * @brief 닫기 확인과 제목의 미저장 표시보다 먼저 부릅니다. 패널이 접혀 있어도 `draw()` 마다 부릅니다.
         * @details 미리보기 씬의 Undo 스택처럼 패널 바깥의 기록에서 편집을 읽는 패널은 여기서 `markDocumentDirty` 를 부른다. 기본은 아무 일도 하지 않는다.
         */
        virtual void syncDocumentDirty() {}
        /**
         * @brief 패널을 그린 뒤 부릅니다(`EditorPanel::draw`). 끌기 · 슬라이더 같은 상호작용 구간이 끝났으면 그 동안의 편집을 되돌리기 한 단계로 기록한다.
         * @details 기본은 아무 일도 하지 않는다(`EditorDocumentPanel` 이 구간을 쥔다).
         */
        virtual void flushDocumentInteraction() {}
        /** @brief 편집이 생겼음을 알립니다. 제목의 미저장 표시와 종료 확인이 이 값을 봅니다. */
        void markDocumentDirty() { _bDocumentDirty = SW_TRUE; }
        /** @brief 저장이나 되돌리기 직후 dirty 를 지웁니다. */
        void clearDocumentDirty() { _bDocumentDirty = SW_FALSE; }
        /** @brief 문서를 디스크에 씁니다. 성공하면 true 입니다. */
        [[nodiscard]] virtual bool saveDocument() = 0;
        /**
         * @brief 지금 저장해도 되면 true 입니다. false 이면 `saveDocumentAndClearDirty` 가 `saveDocument` 를 호출하지 않고 false 를 반환합니다.
         * @details 문서를 읽지 못한 패널(깨졌거나 새 형식)은 이전 문서의 데이터를 가지고 있다. 그대로 쓰면 읽지 못한 파일을 이전 문서로 덮어쓴다.
         */
        virtual bool canSaveDocument() const { return true; }
        /**
         * @brief 저장에 성공한 직후 부릅니다. 기본은 아무 일도 하지 않습니다.
         * @details 에셋 문서 패널(`EditorDocumentPanel`)이 저장한 에셋을 감시를 기다리지 않고 핫 리로드 · 연쇄 리로드에 넘긴다(`AssetHotReload::applySavedAsset`).
         */
        virtual void onDocumentSaved() {}
        /** @brief 저장하지 않고 디스크나 기본값 상태로 되돌립니다. */
        virtual void revertDocument() = 0;

    private:
        /** @brief 미저장이면서 닫기 확인을 거쳐야 하면 확인을 걸고 true 입니다(셸의 `requestClose`). */
        bool beginCloseConfirmIfNeeded();
        /** @brief 닫기 확인 모달을 그리고 고른 항목을 적용합니다(창 밖, 접힌 창도). */
        void drawCloseConfirm();

    private:
        uint8  _bDocumentDirty : 1;
        uint8  _bConfirmClose  : 1;      ///< 닫으려다 미저장 확인을 기다리는 중
        uint64 _lastCaptureMicroseconds; ///< `getLastCaptureMicroseconds` — 문서 패널 기반(`EditorDocumentPanel`)이 편집 알림의 글 만들기를 잰다
    };
} // namespace sw::editor
