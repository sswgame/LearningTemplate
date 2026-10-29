/**
 * @file EditorToolDefaultsReload.h
 * @brief 편집기 도구 기본값(`editortooldefaults.json`)이 바뀌면 다시 읽습니다 — 엔진의 설정 리로드(`ConfigReloadSubsystem`)에 처리기 하나를 건다.
 * @details `Config/` 감시와 호스트 설정 리로드는 엔진 서브시스템이 한다(편집기 없는 개발 실행도 같다). 편집기는 자기가 들고 있는 시드만 맡는다.
 */
#pragma once
#include "Core/Delegate/Subscription.h"

#include "Editor/Common/EditorSubsystem.h"

#include "Engine/Serialization/Serializer/ReloadOutcome.h"

namespace sw::editor
{
    /** @class EditorToolDefaultsReload @brief 도구 기본값 파일의 리로드 처리기를 엔진 설정 리로드에 겁니다. */
    class EditorToolDefaultsReload : public EditorSubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "EditorToolDefaultsReload";

        SubsystemInitResult initializeSubsystem() override;
        void                shutdownSubsystem() override { _handler.reset(); }
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /** @brief @p fullPath 가 도구 기본값 파일이면 다시 읽습니다. 아니면 `NotTracked`, 깨졌으면(이전 값을 지켰다) `Failed` 입니다. */
        [[nodiscard]] static ReloadOutcome reloadIfToolDefaults( string_view fullPath );

    private:
        Subscription _handler; ///< 엔진 설정 리로드에 건 처리기
    };
} // namespace sw::editor
