/**
 * @file MeshModifiersPanelModel.h
 * @brief Mesh Modifiers 패널의 모델 — 하위 스택 편집(연 묶음 · 번호 경로 · 고른 하위 번호 · 들어가기/나오기 요청), 곡선 편집기 팝업을 연 자리, 추가 메뉴 설명 캐시,
 *        워프 정의 내보내기 경로, 끌기 한 번을 되돌리기 한 줄로 묶는 편집 상태. ImGui 를 모릅니다.
 * @details 스택 · 되돌리기 · 미리보기는 세션(`MeshModifierSession`, 에디터 서브시스템)이 들고, 패널(`MeshModifiersPanel`)은 그리기와 곡선 편집기 화면 상태(`EditorCurveEditorState`)만 든다 —
 *          카메라 프리셋 `Model/` 과 같은 선이다(언리얼 모디파이어 스택 디테일 커스터마이즈의 상태와 Slate 행이 나뉜 것과 같다).
 *          프레임을 넘어 "그 모디파이어" 를 기억하는 자리는 주소가 아니라 `MeshModifier::getInstanceID` 로 비교한다 — 되돌리기는 스택을 새 인스턴스로 다시 짓는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

namespace sw::editor
{
    struct MeshModifierRegistration;

    class DeformableModifier;
    class MeshModifier;
    class MeshModifierSession;
    class MeshModifierStack;

    /**
     * @class MeshModifiersPanelModel
     * @brief Mesh Modifiers 패널의 상태와 명령입니다. 파일 머리말 참고.
     */
    class MeshModifiersPanelModel
    {
    public:
        MeshModifiersPanelModel();

        // ------------------------------------------------------------------------------
        // 1) 하위 스택
        // ------------------------------------------------------------------------------
        /**
         * @brief 묶음 @p root 아래에서 지금 보일 하위 스택을 찾습니다. 다른 묶음이면 경로 · 고른 번호를 처음으로 되돌리고,
         *        경로가 무효면(되돌리기 · 지우기) 처음으로 돌립니다. 그리기 앞에 한 번 부르고, 그 프레임의 들어가기 · 나오기는 `applyNestedRequest` 까지 미룬다.
         */
        MeshModifierStack& resolveNestedStack( DeformableModifier& root );
        /** @brief 들어간 깊이(고른 묶음 자신의 하위 스택이 1)입니다. */
        uint32 getNestedDepth() const { return static_cast<uint32>( _listNestedPath.size() ) + 1u; }
        bool   canLeaveNested() const { return _listNestedPath.empty() == false; }
        int32  getNestedSelectedIndex() const { return _nestedSelectedIndex; }
        void   setNestedSelectedIndex( int32 index ) { _nestedSelectedIndex = index; }
        /** @brief 이번 프레임 끝(`applyNestedRequest`)에 한 단계 나오게 합니다. */
        void requestLeaveNested() { _bLeaveRequested = true; }
        /** @brief 이번 프레임 끝에 보이는 스택의 @p index 번 묶음으로 들어가게 합니다. */
        void requestEnterNested( int32 index ) { _enterRequest = index; }
        /** @brief 이번 프레임에 요청이 있었으면 true 입니다(고른 하위 모디파이어를 그리지 않는다 — 그 번호는 곧 다른 스택의 것이다). */
        bool hasNestedRequest() const { return _bLeaveRequested || _enterRequest >= 0; }
        /**
         * @brief 미룬 들어가기 · 나오기를 적용합니다. 보이는 스택을 다 그린 뒤 부릅니다.
         * @details 예전에는 "위로" 단추가 그리기 앞에서 경로를 바로 줄여, 같은 프레임의 목록 · 고른 하위 모디파이어가 나오기 전 스택을 바깥 스택의 번호로 그렸다.
         */
        void applyNestedRequest();

        // ------------------------------------------------------------------------------
        // 2) 곡선 편집기 팝업
        // ------------------------------------------------------------------------------
        /** @brief @p modifier 의 매개변수 @p paramIndex (목록 항목 @p itemIndex, 단일 곡선이면 -1)에 팝업을 열었다고 기억합니다. */
        void openCurvePopup( const MeshModifier& modifier, uint32 paramIndex, int32 itemIndex );
        /** @brief 열린 팝업이 그 자리의 것이면 true 입니다. 되돌리기로 다시 지은 같은 주소의 다른 모디파이어는 아니다. */
        bool isCurvePopupFor( const MeshModifier& modifier, uint32 paramIndex, int32 itemIndex ) const;
        /** @brief 항목 번호가 바뀌었다 — 열린 팝업은 다음 그리기에 닫힌다. */
        void closeCurvePopup() { _curveModifierID = 0; }

        // ------------------------------------------------------------------------------
        // 3) 추가 메뉴 · 워프 내보내기 · 편집 묶기
        // ------------------------------------------------------------------------------
        /** @brief 모디파이어 종류 @p registration 의 설명입니다. 마우스가 새 항목에 올 때 한 번만 인스턴스를 만들어 묻는다. */
        const string& findDescription( const MeshModifierRegistration& registration );

        /** @brief 입력칸이 바로 고치는 워프 정의 파일 경로(`.mfwarp`)입니다. */
        string& editWarpExportPath() { return _warpExportPath; }
        /** @brief 경로 확장자가 맞고 @p shown 에 모디파이어가 있으면 true 입니다(단추를 켠다). */
        bool canExportWarp( const MeshModifierStack& shown ) const;
        /** @brief 보이는 하위 스택 @p shown 을 워프 정의 파일로 씁니다. 결과는 로그로 알린다. */
        void exportWarp( const MeshModifierStack& shown ) const;

        /** @brief 입력칸이 값을 바꿨다 — 끌기의 첫 변경이면 되돌리기 기록을 남기고, 미리보기를 다시 걸게 합니다. @p bItemActive 는 그 칸이 아직 끌리는지입니다. */
        void noteEdit( MeshModifierSession& session, bool bItemActive );
        /** @brief 입력칸을 놓았다 — 다음 편집이 새 기록을 남기게 합니다. */
        void finishEdit() { _bEditing = false; }

    private:
        vector<int32> _listNestedPath;      ///< 묶음의 하위 스택에서 들어간 묶음들의 번호 경로(비면 고른 묶음 자신의 하위 스택)
        uint64        _nestedRootID;        ///< 하위 스택 편집을 연 묶음의 인스턴스 번호(0 이면 없음)
        int32         _nestedSelectedIndex; ///< 보이는 하위 스택에서 고른 번호(-1 이면 없음)
        int32         _enterRequest;        ///< 이번 프레임 끝에 들어갈 묶음 번호(-1 이면 없음)
        bool          _bLeaveRequested;     ///< 이번 프레임 끝에 한 단계 나온다
        uint64        _curveModifierID;     ///< 곡선 팝업을 연 모디파이어의 인스턴스 번호(0 이면 없음)
        uint32        _curveParamIndex;     ///< 곡선 팝업을 연 매개변수 번호
        int32         _curveItemIndex;      ///< 곡선 팝업을 연 목록 항목 번호(단일 곡선이면 -1)
        string        _descriptionID;       ///< 설명을 만들어 둔 등록 id
        string        _description;         ///< `_descriptionID` 모디파이어의 설명
        string        _warpExportPath;      ///< 보이는 하위 스택을 내보낼 워프 정의 파일 경로(`.mfwarp`, `WorldWarpModifier::writeWarpFile`)
        bool          _bEditing;            ///< 끌기 중인 입력칸이 이미 되돌리기 기록을 남겼다
    };
} // namespace sw::editor
