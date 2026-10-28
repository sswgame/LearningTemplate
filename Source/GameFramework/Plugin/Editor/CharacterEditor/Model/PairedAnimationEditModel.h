/**
 * @file PairedAnimationEditModel.h
 * @brief 페어드 애니메이션 에셋(`*.pairedanim.xml`) 편집 모델입니다 — 타임라인 길이, 역할의 시작 · 블렌드 · 정렬 오프셋 · 워프 끝, 공유 알림의 시각 · 역할 ·
 *        추가 · 삭제, 되돌리기, 파일 읽기 · 쓰기와 한 시계 미리보기(스크럽)의 계산. ImGui 와 씬을 모릅니다.
 * @details 편집 대상은 파일 한 장의 내용(`PairedAnimationContentDef`)이다. 쓰기는 `PairedAnimationDef::makeXMLText` 하나로 하고, 규칙 검사는 그 글을
 *          `PairedAnimationDef::loadFromXMLText` 로 다시 읽어 본다 — 런타임과 같은 검사 하나이며 에디터 쪽 사본 규칙을 두지 않는다. 규칙에 어긋난 내용은 저장하지 않는다.
 *          편집 하나가 값 하나의 범위(유한, 0 이상)를 먼저 보고, 역할 사이 규칙(앵커 하나, 시작 < 길이, 알림 역할)은 저장 · `isValid` 가 본다.
 *          미리보기 계산은 세션 규칙과 같다: 역할은 공유 시각이 `start` 에 닿으면 들어오고, 클립 시각은 `공유 시각 - start`, 클립이 끝나면 종료 상태로 나간다.
 *          정렬 목표는 `PairedAnimationDef::computeAlignTarget`(세션과 같은 식)이다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "Engine/Character/PairedAnimation/PairedAnimationDef.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/Model/EditSnapshotHistory.h"

namespace sw::editor
{
    /** @brief 미리보기 시각에서 역할 하나의 단계입니다. */
    enum class PairedAnimationPreviewStage : uint8
    {
        Waiting = 0, ///< 공유 시각이 아직 `start` 앞이다
        Playing,     ///< 역할 클립을 재생 중이다
        Exited,      ///< 클립이 끝나 종료 상태로 나갔다
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 미리보기 시각에서 역할 하나의 상태입니다. */
    struct PairedAnimationRolePreview
    {
        float32                     _clipSeconds{ 0.0f }; ///< `Playing` 일 때 역할 클립 시각
        PairedAnimationPreviewStage _stage{ PairedAnimationPreviewStage::Waiting };
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class PairedAnimationEditModel
     * @brief 페어드 애니메이션 파일 하나와 그 편집 기록 · 미리보기 시각입니다. 파일 머리말 참고.
     */
    class PairedAnimationEditModel
    {
    public:
        PairedAnimationEditModel();

        // --- 읽기 · 쓰기 ---------------------------------------------------------------------------------------------

        /** @brief XML 텍스트에서 읽어 현재 내용을 바꿉니다. 규칙에 어긋나면 현재 내용을 그대로 두고 false 입니다(이유는 로그). 성공하면 기록을 지우고 "저장된 상태" 로 맞춥니다. */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName );
        /** @brief 파일을 읽습니다. 파일이 없거나 읽지 못하면 false 입니다. */
        [[nodiscard]] bool loadFromFile( string_view absolutePath );
        /** @brief 현재 내용을 파일 형식으로 씁니다. */
        string saveToXMLText() const { return PairedAnimationDef::makeXMLText( _content ); }
        /** @brief 규칙을 본 뒤 파일에 씁니다. 규칙에 어긋나거나 쓰지 못하면 쓰지 않고 false 이고 이유를 @p outError 에 담습니다. 성공하면 "저장된 상태" 로 맞춥니다. */
        [[nodiscard]] bool saveToFile( string_view absolutePath, string& outError );

        // --- 조회 ----------------------------------------------------------------------------------------------------

        const PairedAnimationContentDef& getContent() const { return _content; }
        /** @brief 저장된 상태와 내용이 다르면 true 입니다. */
        bool isDirty() const { return saveToXMLText() != _savedText; }
        /** @brief 지금 내용을 다시 읽어 규칙(런타임 검사)에 맞으면 true 입니다. 틀린 이유는 로그로 나갑니다. */
        bool isValid() const;

        // --- 편집 ----------------------------------------------------------------------------------------------------
        // 실패하면 내용을 바꾸지 않고 되돌리기 기록도 남기지 않습니다. 이유는 @p outError 에 영어 한 문장으로 담습니다.
        // bContinuous 가 true 이면 같은 대상 · 같은 필드의 연속 편집(끌기)을 되돌리기 한 단계로 합칩니다. 끌기가 끝나면 `endContinuousEdit` 를 부릅니다.

        [[nodiscard]] bool setDuration( float32 seconds, bool bContinuous, string& outError );
        /** @brief 역할이 공유 타임라인에 들어오는 시각입니다. 앵커는 0 이어야 하므로 고칠 수 없습니다. */
        [[nodiscard]] bool setRoleStart( uint32 roleIndex, float32 seconds, bool bContinuous, string& outError );
        [[nodiscard]] bool setRoleBlend( uint32 roleIndex, float32 enterSeconds, float32 exitSeconds, bool bContinuous, string& outError );
        /** @brief 정렬(`snap` · `warp`) 역할의 앵커 기준 자리(m)와 요(도)입니다. 정렬하지 않는 역할은 실패합니다. */
        [[nodiscard]] bool setRoleOffset( uint32 roleIndex, const float3& offset, float32 yawDegrees, bool bContinuous, string& outError );
        /**
         * @brief 미리보기에서 옮긴 역할 루트(@p rolePosition · @p roleYaw 라디안)를 앵커 루트(@p anchorPosition · @p anchorYaw) 기준 오프셋 · 요로 바꿔 넣습니다(오프셋 기즈모).
         * @details `computeRoleTarget` 의 역이다 — 넣은 뒤 같은 앵커로 목표를 구하면 그 자리가 나온다.
         */
        [[nodiscard]] bool setRoleOffsetFromWorld( uint32 roleIndex, const float3& anchorPosition, float32 anchorYaw, const float3& rolePosition, float32 roleYaw, bool bContinuous,
                                                   string& outError );
        /** @brief `warp` 역할의 워프 창 끝(역할 클립 시각)입니다. */
        [[nodiscard]] bool setRoleWarpEnd( uint32 roleIndex, float32 seconds, bool bContinuous, string& outError );
        /** @brief 공유 알림을 끝에 더합니다. 시각은 0 이상 · 길이 이하, 역할은 있는 역할이어야 합니다. */
        [[nodiscard]] bool addNotify( const hashed_string& name, const hashed_string& role, float32 time, string& outError );
        [[nodiscard]] bool removeNotify( uint32 notifyIndex, string& outError );
        [[nodiscard]] bool setNotifyTime( uint32 notifyIndex, float32 time, bool bContinuous, string& outError );
        [[nodiscard]] bool setNotifyRole( uint32 notifyIndex, const hashed_string& role, string& outError );
        /** @brief 연속 편집을 끝냅니다. 다음 편집은 새 되돌리기 단계를 만듭니다. */
        void endContinuousEdit() { _history.endCoalescing(); }

        // --- 되돌리기 ------------------------------------------------------------------------------------------------

        [[nodiscard]] bool undo();
        [[nodiscard]] bool redo();
        bool               canUndo() const { return _history.canUndo(); }
        bool               canRedo() const { return _history.canRedo(); }

        // --- 미리보기(한 시계) ---------------------------------------------------------------------------------------

        /** @brief 공유 시각을 정합니다(0 ~ 길이로 자릅니다 — 스크럽 막대의 범위다). */
        void    setScrubTime( float32 seconds );
        float32 getScrubTime() const { return _scrubTime; }
        /**
         * @brief 지금 공유 시각에서 역할의 단계와 클립 시각입니다. @p clipSeconds 는 그 역할 클립의 길이이고, 모르면 0 을 넘깁니다(그러면 나감을 판정하지 않는다).
         * @details 앵커는 늘 들어와 있고 길이에 닿아도 마지막 자세를 보인다(세션은 그 다음 갱신에 끝난다).
         */
        PairedAnimationRolePreview computeRolePreview( uint32 roleIndex, float32 clipSeconds ) const;
        /** @brief 공유 시각이 (@p fromSeconds, @p toSeconds] 를 지날 때 울리는 알림의 인덱스를 모읍니다(스크럽으로 앞으로 갈 때). */
        void collectNotifiesBetween( float32 fromSeconds, float32 toSeconds, vector<uint32>& outListNotifyIndex ) const;
        /** @brief 앵커 루트가 @p anchorPosition · @p anchorYaw 일 때 역할 루트의 정렬 목표입니다. 정렬하지 않는 역할은 앵커 루트 그대로입니다. */
        void computeRoleTarget( uint32 roleIndex, const float3& anchorPosition, float32 anchorYaw, float3& outPosition, float32& outYaw ) const;

    private:
        /** @brief 편집 직전에 부릅니다. */
        void recordUndo( string_view coalesceKey );
        /** @brief 역할 인덱스가 범위 안이면 true, 아니면 오류를 쓰고 false 입니다. */
        bool isRoleIndexValid( uint32 roleIndex, string& outError ) const;
        bool isNotifyIndexValid( uint32 notifyIndex, string& outError ) const;

        PairedAnimationContentDef                      _content;
        string                                         _savedText;
        EditSnapshotHistory<PairedAnimationContentDef> _history;
        float32                                        _scrubTime;
    };
} // namespace sw::editor
