/**
 * @file EditorAssetMove.h
 * @brief 에셋 옮기기 하나와 옮기기 묶음의 계획 · 결과 값입니다(`EditorAssetFileCommands` 가 채우고, 콘텐츠 브라우저 확인 대화상자가 그린다).
 * @details 명령 헤더에서 값만 떼어 냈다 — MVVM View 가 명령 헤더를 보지 않고 계획을 그리게(`docs/agents/EditorUI.md` 'Model headers stay light').
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

namespace sw::editor
{
    /** @brief 옮기기 하나입니다(디스크 경로). 이름 바꾸기도 같은 폴더 안의 옮기기다. */
    struct EditorAssetMove
    {
        string _sourceAbs;
        string _destinationAbs;
    };

    /**
     * @brief 옮기기 묶음의 계획과 결과입니다. 미리보기(`planAssetMoves`)와 적용(`applyAssetMoves`)이 같은 형식을 채운다.
     * @details 바깥 도메인 목록은 옮기는 묶음 밖에서 이 묶음을 적은 파일의 도메인(`game/<팩>` · `common` …)이다 — 게임 전용 에셋을 `common` 으로 옮기기 전에
     *          다른 팩이 이미 쓰는지, 공통 에셋을 한 팩 아래로 옮기면 다른 팩이 깨지는지를 확인 대화상자가 보인다.
     */
    struct EditorAssetMovePlan
    {
        vector<EditorAssetMove> _listMove;            ///< 정규화한 옮기기(폴더는 그 안의 파일마다 하나)
        vector<string>          _listOldID;           ///< 옮기기마다 지금 리소스 id
        vector<string>          _listNewID;           ///< 옮기기마다 새 리소스 id
        vector<string>          _listReferrerID;      ///< 글을 고칠 참조 파일(지금 id)
        vector<string>          _listUnfixedReferrer; ///< 경로 칸 밖에서 옮기는 에셋을 적은 파일 — 하나라도 있으면 옮기지 않는다
        vector<string>          _listOutsideDomain;   ///< 묶음 밖 참조 파일의 도메인
        string                  _failure;             ///< 옮길 수 없거나 지면 그 이유. 비었으면 계획대로 된다
        uint32                  _fixedFileCount{ 0 }; ///< 적용한 뒤 글을 고친 파일 수
    };
} // namespace sw::editor
