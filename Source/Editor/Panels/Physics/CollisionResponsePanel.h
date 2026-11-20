/**
 * @file CollisionResponsePanel.h
 * @brief 충돌 응답 창(물리 설정 `physicssettings.xml`)입니다 — 프로필 × 채널 응답 격자를 보고 칸을 눌러 막음 · 겹침 · 무시를 돌리며, 그 결과인 프로필 쌍 응답 표를 봅니다.
 * @details 언리얼 Project Settings > Collision(프리셋 · 채널 표) 자리다. 상태와 명령은 `CollisionResponseEditModel`(ImGui 없음)이 들고, 되돌리기는 문서 패널의 원문 단계다.
 *          연 문서가 없으면 프로젝트의 물리 설정(`PhysicsSystem::kSettingsPath`)을 연다 — 이 표는 프로젝트에 하나다. 채널 · 프로필을 더하고 지우는 일은 카탈로그 편집기다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Editor/Common/GUI/Panel/EditorDocumentPanel.h"
#include "Editor/Panels/Physics/Model/CollisionResponseEditModel.h"

namespace sw::editor
{
    /** @class CollisionResponsePanel @brief 파일 머리말 참고. */
    class CollisionResponsePanel : public EditorDocumentPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "collision_responses";

        CollisionResponsePanel();

        const utf8*        getPanelTitle() const override { return "Collision Responses"; }
        void               drawDocumentContent() override;
        [[nodiscard]] bool saveDocument() override;
        bool               canAcceptDocumentPath( string_view assetPath ) const override;

        const CollisionResponseEditModel& getModel() const { return _model; }

    private:
        ToolAssetLoadResult loadDocument() override;
        string              captureDocumentText() const override { return _model.captureText(); }
        void                applyDocumentText( string_view text ) override { _model.applyText( text ); }

        /** @brief 프로필 × 채널 격자입니다. 칸을 누르면 다음 응답으로 돌린다. 채널 기본을 쓰는 칸은 흐리게 보인다. */
        void drawResponseGrid();
        /** @brief 프로필 쌍의 결과 응답(약한 쪽)입니다 — 읽기만 한다. */
        void drawPairTable();

    private:
        CollisionResponseEditModel _model;
        uint8                      _bOpenRequested : 1; ///< 연 문서가 없어 프로젝트 물리 설정을 한 번 열었다
        [[maybe_unused]] uint8     _reserved       : 7;
    };
} // namespace sw::editor
