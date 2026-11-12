/**
 * @file EditorDocumentOpenUtil.h
 * @brief 문서 패널이 어떤 문서를 열지 정합니다 — "Open..." 후보 · 최근 목록 · 포커스 교체 판정(ImGui 없음).
 */
#pragma once
#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/Commands/Asset/EditorAssetIndex.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"

namespace sw::editor
{
    class EditorWorkspace;
    struct EditorAssetMoveNotice;

    /**
     * @struct EditorDocumentOpenUtil
     * @brief `EditorDocumentPanel` 의 판정만 모았습니다. 팝업과 콤보를 그리는 일은 패널이 합니다.
     */
    struct EditorDocumentOpenUtil
    {
        /**
         * @brief 색인 항목에서 종류가 @p kind 이고 @p accept 가 받는 경로만 골라, 이름순 · 중복 없이 @p outListPath 에 씁니다(이전 내용은 지운다).
         * @details 후보의 경로가 ImGui ID 이기도 해서 같은 경로가 둘이면 한 줄만 눌린다. @p accept 는 패널의 `canAcceptDocumentPath` 이고 포커스 매칭과 같은 판정이다.
         */
        template <typename TAccept>
        static void collectCandidates( const vector<EditorAssetIndexEntry>& listEntry, const EditorAssetType& kind, TAccept&& accept, vector<string>& outListPath )
        {
            outListPath.clear();
            for ( const EditorAssetIndexEntry& entry : listEntry )
            {
                if ( entry._kind == kind && accept( string_view{ entry._path } ) )
                    outListPath.push_back( entry._path );
            }
            VectorUtil::sortUnique( outListPath );
        }

        /** @brief 최근 문서 목록 맨 앞에 @p assetPath 를 둡니다. 중복은 빼고 @p maxCount 개까지 남깁니다. 빈 경로는 무시합니다. */
        static void rememberRecent( vector<string>& inoutListPath, string_view assetPath, size_t maxCount );

        /** @brief 포커스(@p focused, 이 패널이 받는 경로 또는 빈 글)가 지금 문서(@p loaded)와 다른 새 문서이면 true 입니다. */
        [[nodiscard]] static bool isNewFocus( string_view focused, string_view loaded );

        /** @brief 받아들일 문서입니다. 미저장 확인을 기다린 경로(@p pending)가 지금 포커스보다 앞섭니다. 확인하는 동안 포커스가 바뀌어도 사용자가 답한 문서를 연다. */
        [[nodiscard]] static string_view chooseDocumentToAccept( string_view pending, string_view focused );

        /**
         * @brief 문서 교체를 취소한 뒤 포커스를 지금 문서(@p loaded)로 되돌립니다. 열린 문서가 없으면 포커스를 비웁니다.
         * @details 포커스를 그대로 두면 다음 그리기가 또 새 문서로 보고 같은 확인 창을 띄워, 취소가 듣지 않는다.
         */
        static void restoreFocusAfterCancel( EditorWorkspace& workspace, const string& loaded );

        /**
         * @brief 에셋 옮김 @p notice 로 연 문서 경로 @p inoutLoaded 와 최근 목록 @p inoutListRecent 를 새 id 로 고칩니다(감사 P0-4).
         * @return 고친 뒤의 연 문서가 글을 고친 참조 파일이면 true 입니다 — 패널은 깨끗하면 디스크에서 다시 읽는다(미저장 참조자는 계획이 거절했다, `findDirtyReferrerDocument`).
         */
        [[nodiscard]] static bool applyAssetMove( const EditorAssetMoveNotice& notice, string& inoutLoaded, vector<string>& inoutListRecent );
    };
} // namespace sw::editor
