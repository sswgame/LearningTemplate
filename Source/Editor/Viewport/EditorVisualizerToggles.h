/**
 * @file EditorVisualizerToggles.h
 * @brief 뷰포트 시각화의 켬/끔을 id 로 보관합니다(ImGui 없음 — EditorTest 가 시험합니다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Common/EditorExports.h"

namespace sw::editor
{
    struct EditorVisualizerRegistration;

    /** @brief 시각화 하나를 사용자가 바꾼 기록입니다(기본값과 다를 때만 남습니다). */
    struct EditorVisualizerToggle
    {
        string _id;
        bool   _bOn{ false };
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorVisualizerToggles
     * @brief 뷰포트 시각화의 켬/끔입니다. 등록 줄의 기본값 위에 사용자가 바꾼 것만 id 로 보관합니다.
     * @details 등록 순서의 비트 위치로 보관하면 확장 모듈이 줄을 끼우거나 뺄 때 다른 시각화가 켜집니다. id 로 보관하면 줄이 로드되고 언로드되어도 각자의 상태가 남습니다.
     */
    class SW_EDITOR_API EditorVisualizerToggles
    {
    public:
        /** @brief @p registration 이 켜져 있으면 true 입니다(바꾼 기록이 없으면 등록 줄의 기본값). */
        bool isOn( const EditorVisualizerRegistration& registration ) const;
        /** @brief 켜거나 끕니다. 기본값과 같아지면 기록을 지웁니다. */
        void setOn( const EditorVisualizerRegistration& registration, bool bOn );
        /** @brief 바꾼 기록 수입니다(시험). */
        uint32 getOverrideCount() const { return static_cast<uint32>( _listToggle.size() ); }

    private:
        vector<EditorVisualizerToggle> _listToggle;
    };
} // namespace sw::editor
