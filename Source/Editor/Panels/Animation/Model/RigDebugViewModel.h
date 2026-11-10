/**
 * @file RigDebugViewModel.h
 * @brief 리그 디버그 창의 ViewModel — 고른 오브젝트의 `PoseModifierComponent` 노드 · 대상 줄을 값으로 모으고(`gather`), 노드 고르기 · 가중치 · 켜짐 · 리그 저장 · 대상 기즈모 ·
 *        본 표시 명령을 받습니다. ImGui 를 모릅니다(`docs/agents/EditorUI.md` MVVM 계약).
 * @details 고친 가중치 · 켜짐은 실행 중인 인스턴스의 노드 복제본에 들어가고 되돌리기는 문서 명령(`EditorTransaction::push`)으로 기록한다. "Save to rig" 는 리그 에셋
 *          (`*.rig.xml`)의 노드 `weight` · `enabled` 에 되써 넣는다(`RigSaveParams::writeToRigFile`). 대상 기즈모 상태는 뷰포트 시각화와 함께 보는 `RigTargetEdit` 다.
 *          감시 값(`Editor.RigDebug*`)도 여기(.cpp)서 읽는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

namespace sw::editor
{
    /** @brief 창이 그릴 내용의 종류입니다. */
    enum class RigDebugContent : uint8
    {
        NoRig = 0,   ///< 고른 오브젝트에 리그 컴포넌트가 없다
        RigNotReady, ///< 리그가 아직 묶이지 않았다
        Rig          ///< 노드 · 대상 줄이 있다
    };

    /** @brief 노드 한 줄입니다(값만). */
    struct RigDebugNodeRow
    {
        string         _name{};
        string         _typeName{};
        string         _bones{};           ///< 쓰는 본을 "첫 본 (+n)" 으로 줄인 글
        string         _weightSlot{};      ///< 가중치 슬롯 이름(없으면 빈 글)
        vector<uint32> _listBone{};        ///< 쓰는 본 번호
        float32        _weight{ 0.0f };    ///< 노드 가중치
        float32        _effective{ 0.0f }; ///< 커브 · 슬롯 · LOD 를 모두 곱한 이번 프레임 값
        uint8          _bEnabled{ SW_FALSE };
    };

    /** @brief 대상 한 줄입니다(값만). */
    struct RigDebugTargetRow
    {
        string _name{};
        float3 _translation{};        ///< 보이는 오프셋(기즈모로 끄는 중이면 남겨 둔 값)
        uint8  _bOwnUnit{ SW_FALSE }; ///< 자기 유닛 대상(본 · 소켓)이라 기즈모로 옮길 수 있다
        uint8  _bGizmoOn{ SW_FALSE }; ///< 이 대상에 기즈모를 놓았다
    };

    /** @brief 리그 디버그 창이 한 프레임 그릴 값입니다. 포인터 · 참조를 두지 않는다(MVVM 계약). */
    struct RigDebugViewState
    {
        RigDebugContent           _content{ RigDebugContent::NoRig };
        vector<RigDebugNodeRow>   _listNode{};
        vector<RigDebugTargetRow> _listTarget{};
        string                    _status{};             ///< "Save to rig" 결과 줄
        string                    _targetStatus{};       ///< 대상 기즈모 저장 결과 줄
        optional<uint32>          _selectedNode;         ///< 고른 노드 번호(없으면 비어 있다)
        uint8                     _bCanSave{ SW_FALSE }; ///< 리그 파일 경로가 있다
    };

    /** @brief 리그 디버그 창의 ViewModel 입니다. */
    class RigDebugViewModel
    {
    public:
        RigDebugViewModel();

        /** @brief 고른 오브젝트의 줄을 @p outState 에 채웁니다. 고른 오브젝트가 바뀌었으면 노드 선택과 대상 기즈모를 풉니다. */
        void gather( RigDebugViewState& outState );
        /** @brief 노드 @p nodeIndex 를 고르거나(이미 골랐으면 풉니다). */
        void toggleNodeSelection( uint32 nodeIndex );
        /** @brief 노드 가중치를 끄는 동안 인스턴스 값을 바꿉니다(되돌리기는 `commitNodeWeight` 가 남긴다). */
        void setNodeWeight( uint32 nodeIndex, float32 weight );
        /** @brief 가중치 끌기를 마칩니다 — @p weightBefore 에서 지금 값으로의 되돌리기 한 단계를 남깁니다. */
        void commitNodeWeight( uint32 nodeIndex, float32 weightBefore );
        /** @brief 노드를 켜거나 끄고 되돌리기 한 단계를 남깁니다. */
        void setNodeEnabled( uint32 nodeIndex, bool bEnabled );
        /** @brief 인스턴스 노드의 가중치 · 켜짐을 리그 파일에 되써 넣습니다("Save to rig"). */
        void saveToRig();
        /** @brief 대상 @p targetIndex 에 뷰포트 기즈모를 놓거나 거둡니다. */
        void setTargetGizmo( uint32 targetIndex, bool bOn );
        /** @brief 고른 노드가 쓰는 본마다 구와 이름을 이번 프레임 디버그 드로우로 냅니다(`debugdraw.category RigDebug off` 로 끈다). */
        void submitBoneMarkers( const float4& color );

    private:
        string           _status;           ///< 저장 결과 줄
        uint64           _selectedObjectID; ///< 고른 노드가 속한 오브젝트다(오브젝트가 바뀌면 노드 선택을 푼다)
        optional<uint32> _selectedNode;     ///< 고른 노드 번호다(없으면 비어 있다)
    };
} // namespace sw::editor
