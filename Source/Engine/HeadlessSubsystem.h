/**
 * @file Engine/HeadlessSubsystem.h
 * @brief 기동 순서의 Headless 자리입니다 — 명령줄의 헤드리스 작업(셰이더 · 씬 쿠킹, 리플렉션 문서, 리타깃 베이크, 립싱크, 임포트 · 로컬라이제이션 준비)을 돌립니다.
 * @details 작업이 있었으면 `SkipDependents` 라 이것에 기대는 단계(창 · RHI · 렌더러 · 플레이어 설정 · 글꼴)가 서지 않는다. 결과는 호스트가 준 상태
 *          (`HeadlessTaskState`)에 적는다 — 엔진 루프가 `isHeadless` · `didHeadlessTaskFail` 로 내보낸다. 작업은 `SW_HEADLESS_TASK` 줄로 오르고(`Engine/Foundation/HeadlessTask.h`), 이 단계는 고르고 돌리기만 한다.
 *          모든 타입 공급자가 등록을 끝낸 뒤(ModuleLoadStage)라 쿠킹이 게임 · 킷의 컴포넌트 타입을 안다. 글꼴 · 사용자 설정 · RHI 는 낮은 층이라 이 이름을 모르므로
 *          역방향 의존으로 적는다.
 */
#pragma once
#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Foundation/Macros.h"
#include "Core/Subsystem/ISubsystem.h"

#include "Engine/Foundation/HeadlessTask.h"

namespace sw
{
    /**
     * @brief 에디터 모듈이 하는 헤드리스 작업(현지화 도구 · 원본 임포트 · 대조)의 인자입니다. 엔진은 이것이 하나라도 적혔으면 창 · RHI 없이 세우고,
     *        App 의 작업 표(`AppHeadlessTasks`)가 모듈을 올려 부른다. 새 에디터 도구 작업은 인자를 둘 중 맞는 목록과 그 표에 함께 더한다.
     * @details 글 도구는 어느 호스트에서나 선다. 원본 임포트 · 대조는 App 의 일이라 서버에 주면 기동이 실패한다(`HeadlessTaskHost::Client`).
     */
    inline constexpr CommandLineArgument kArrEditorTextToolArgument[] = {
        CommandLineArgument::GATHER_TEXT,
        CommandLineArgument::CHECK_TEXT,
        CommandLineArgument::EXPORT_PO,
        CommandLineArgument::IMPORT_PO,
        CommandLineArgument::CHECK_ASSET_REFS,
    };
    /** @brief 원본 임포트 · 대조 인자입니다(`kArrEditorTextToolArgument` 설명). */
    inline constexpr CommandLineArgument kArrEditorAssetToolArgument[] = {
        CommandLineArgument::IMPORT_TEXTURES,
        CommandLineArgument::CHECK_TEXTURES,
        CommandLineArgument::COOK_TEXTURE_VARIANTS,
        CommandLineArgument::CHECK_TEXTURE_VARIANTS,
        CommandLineArgument::IMPORT_MODELS,
        CommandLineArgument::CHECK_MODELS,
        CommandLineArgument::IMPORT_HEIGHTFIELDS,
        CommandLineArgument::CHECK_HEIGHTFIELDS,
    };
} // namespace sw

namespace sw
{
    /**
     * @class HeadlessSubsystem
     * @brief 명령줄의 헤드리스 작업을 돌리는 서브시스템입니다.
     */
    class SW_API HeadlessSubsystem final : public ISubsystem
    {
    public:
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "Headless";

        /** @brief @p outState 에 결과를 적습니다(호스트 소유 — 이것보다 오래 산다). @p bDedicatedServer 면 클라이언트 작업을 거절합니다. */
        HeadlessSubsystem( bool bDedicatedServer, HeadlessTaskState& outState );

        SubsystemInitResult     initializeSubsystem() override;
        void                    shutdownSubsystem() override {}
        const utf8*             getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const> getSubsystemDependencies() const override;
        span<const utf8* const> getSubsystemDependents() const override;

    private:
        HeadlessTaskState& _state;
        uint8              _bDedicatedServer : 1;
    };
} // namespace sw
