/**
 * @file ContentBrowserFileActions.h
 * @brief 콘텐츠 브라우저의 파일 작업입니다. 새로 만들기, 복제, 이름 바꾸기, 옮기기, 삭제와 그 사이에 기다리는 상태를 가집니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/Commands/Asset/EditorAssetMove.h"

namespace sw::editor
{
    struct EditorNewAssetTemplate;

    class EditorReferenceIndex;
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class ContentBrowserMoveQueue
     * @brief 참조 역색인이 따라오기를 기다리는 옮기기들입니다. 옮기기 하나가 끝나면 역색인을 다시 읽으므로, 그동안 온 옮기기는 차례로 줄을 선다.
     * @details 이름 바꾸기는 지금 입력란의 것 하나뿐이다(다시 Enter 하면 새 이름으로 바뀌고, 취소하거나 다른 에셋을 시작하면 버린다 — 남겨 두면 다음 에셋에 그 이름이 붙는다).
     *          한 에셋을 또 옮기면 나중 목적지만 남는다(먼저 옮기면 나중 옮기기의 원본이 사라진다).
     */
    class ContentBrowserMoveQueue
    {
    public:
        /** @brief 기다리는 옮기기 하나입니다. 이름 바꾸기면 `_newName`, 폴더로 옮기기면 `_sourceAbs` · `_folderAbs` 입니다. */
        struct Move
        {
            bool   _bRename{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
            string _newName{};
            string _sourceAbs{};
            string _folderAbs{};
        };

        ContentBrowserMoveQueue();

        void addRename( string_view newName );
        void addMove( string_view sourceAbs, string_view folderAbs );
        /** @brief 기다리는 이름 바꾸기를 버립니다. */
        void discardRename();
        /** @brief 가장 먼저 온 옮기기를 꺼냅니다. 없으면 false 입니다. */
        [[nodiscard]] bool takeNext( Move& outMove );
        bool               isEmpty() const { return _listMove.empty(); }

    private:
        vector<Move> _listMove;
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class ContentBrowserFileActions
     * @brief 콘텐츠 브라우저가 디스크의 에셋을 바꾸는 작업입니다. 실제 파일 처리는 `EditorAssetFileCommands` 가 하고, 이 클래스는 입력을 검사하고
     *        이름 바꾸기와 삭제 확인처럼 두 단계로 나뉜 작업의 대기 상태를 가집니다.
     * @details 성공한 작업은 결과 경로를 돌려준다. 그 경로를 선택하고 목록을 다시 읽게 하는 일은 패널이 한다.
     *          ImGui 에 의존하지 않는다. 확인 모달과 입력란을 여는 시점은 패널이 정한다.
     */
    class ContentBrowserFileActions
    {
    public:
        /** @brief 기다리는 작업이 없는 상태로 만듭니다. */
        ContentBrowserFileActions();

        // ------------------------------------------------------------------------------
        // 1) 한 번에 끝나는 작업. 성공하면 결과 경로를 돌려준다. 실패는 `EditorAssetFileCommands` 가 알린다
        // ------------------------------------------------------------------------------
        /** @brief @p parentAbs 에 새 폴더를 만듭니다. @p parentAbs 가 비어 있으면 false 입니다. */
        [[nodiscard]] static bool createFolder( string_view parentAbs, string& outFolderAbs );
        /** @brief @p folderAbs 에 새 에셋을 만듭니다. @p folderAbs 가 비어 있으면 false 입니다. */
        [[nodiscard]] static bool createAsset( string_view folderAbs, const EditorNewAssetTemplate& assetTemplate, string& outAssetAbs );
        /** @brief 에셋을 같은 폴더에 복제합니다. @p sourceAbs 가 비어 있으면 false 입니다. */
        static bool duplicateAsset( string_view sourceAbs, string& outAssetAbs );
        /**
         * @brief 에셋(폴더면 그 안 전부)을 @p folderAbs 로 옮기고 참조를 고칩니다 — 트랜잭션이다(`EditorAssetFileCommands::applyAssetMoves`). 폴더는 다 옮긴 뒤 빈 원본을 지운다.
         * @return 이미 그 폴더에 있거나, 자기 안으로 옮기거나, 고칠 수 없는 참조가 있거나, 쓰기가 졌으면(되돌린다) false 입니다.
         */
        [[nodiscard]] static bool moveAssetToFolder( string_view sourceAbs, string_view folderAbs, EditorReferenceIndex& inoutIndex, string& outDestinationAbs );
        /** @brief `moveAssetToFolder` 가 할 일을 디스크를 건드리지 않고 계획합니다(확인 대화상자의 미리보기). 옮길 수 없으면 false 이고 `_failure` 에 이유입니다. */
        [[nodiscard]] static bool planMoveToFolder( string_view sourceAbs, string_view folderAbs, const EditorReferenceIndex& index, EditorAssetMovePlan& outPlan );

        // ------------------------------------------------------------------------------
        // 2) 이름 바꾸기. beginRename 으로 시작하고 commitRename 이나 cancelRename 으로 끝낸다
        // ------------------------------------------------------------------------------
        /**
         * @brief 에셋 이름 바꾸기를 시작합니다. 폴더와 빈 경로는 받지 않습니다.
         * @param outStem 입력란에 미리 넣을, 확장자를 뺀 이름입니다.
         * @return 시작했으면 true 입니다.
         */
        bool beginRename( string_view absolutePath, string& outStem );
        /** @brief 이름을 바꾸는 중인 에셋의 경로를 반환합니다. 없으면 빈 문자열입니다. */
        const string& getRenamingPath() const { return _renamingAbs; }
        /** @brief 이름 바꾸기를 끝냅니다. 바꾸지 않습니다. */
        void cancelRename() { _renamingAbs.clear(); }
        /**
         * @brief 이름 바꾸기를 @p newName 으로 끝냅니다. 성공하든 실패하든 이름 바꾸기 상태는 끝납니다.
         * @return 옮겼으면 true 이고 @p outDestinationAbs 에 새 경로가 들어갑니다.
         */
        bool commitRename( string_view newName, EditorReferenceIndex& inoutIndex, string& outDestinationAbs );
        /**
         * @brief 이름 바꾸기의 대상 경로를 만듭니다. @p newName 에 확장자가 없으면 원래 확장자를 붙입니다.
         * @return 이름이 비었거나 대상이 원래 경로와 같으면 false 입니다.
         */
        static bool makeRenameDestination( string_view sourceAbs, string_view newName, string& outDestinationAbs );

        // ------------------------------------------------------------------------------
        // 3) 삭제. requestDelete 로 대상을 정하고, 사용자가 확인하면 confirmDelete 로 휴지통에 보낸다
        // ------------------------------------------------------------------------------
        /** @brief 삭제 확인을 기다릴 에셋을 정합니다. */
        void requestDelete( string_view absolutePath ) { _pendingDeleteAbs = string{ absolutePath }; }
        /** @brief 삭제 확인을 기다리는 에셋을 반환합니다. 없으면 빈 문자열입니다. */
        const string& getPendingDeletePath() const { return _pendingDeleteAbs; }
        /** @brief 기다리던 삭제를 취소합니다. */
        void cancelDelete() { _pendingDeleteAbs.clear(); }
        /**
         * @brief 기다리던 에셋을 OS 휴지통으로 보냅니다. 성공하든 실패하든 기다리던 삭제는 끝납니다.
         * @return 휴지통으로 보냈으면 true 입니다. 기다리던 에셋이 없으면 false 입니다.
         */
        bool confirmDelete();

    private:
        /** @brief 폴더(그 안 파일 전부) 또는 파일 하나를 @p folderAbs 로 옮기는 목록입니다. 옮길 수 없으면 그 이유입니다. */
        [[nodiscard]] static expected<vector<EditorAssetMove>> collectMovesToFolder( string_view sourceAbs, string_view folderAbs );

    private:
        string _renamingAbs;      ///< 이름을 바꾸는 중인 에셋. 비어 있으면 없음
        string _pendingDeleteAbs; ///< 삭제 확인을 기다리는 에셋. 비어 있으면 없음
    };
} // namespace sw::editor
