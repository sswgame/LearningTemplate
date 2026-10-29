/**
 * @file EditorModuleUnloadListener.h
 * @brief 모듈을 내리기 전에 그 모듈의 등록 줄로 만든 에디터 인스턴스와 그 모듈 타입의 기본값 인스턴스를 거둡니다.
 */
#pragma once
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/EditorSubsystem.h"

namespace sw::editor
{
    class EditorContext;

    /**
     * @class EditorModuleUnloadListener
     * @brief 모듈을 내리기 전에(핫 리로드) 그 모듈의 등록 줄로 만든 패널 · 팝업 · 인스펙터 · 명령 · 확장 서브시스템과, 그 모듈 타입의 기본값 인스턴스를 거둡니다.
     * @details 등록 줄은 모듈을 내리기 전에 떨어지고(`ModuleObjectRegistrar::detach`) 목록이 알린다(`subscribeEditorExtensionRemoved`) — 그 자리에서 매니저들을
     *          목록과 맞춰 빠진 줄의 인스턴스를 종료한다. 기본값 인스턴스는 어느 모듈의 컴포넌트든 될 수 있어 모듈을 내린다는 알림(`TypeRegistry::subscribeModuleUnloading`)에 모두 지운다.
     *          주소로 고르지 않는다. 맞추는 매니저들을 의존으로 선언하므로 그들보다 먼저 종료 · 소멸한다(구독이 지워진 매니저를 밟지 않는다).
     */
    class EditorModuleUnloadListener final : public EditorSubsystem
    {
    public:
        explicit EditorModuleUnloadListener( EditorContext& context );
        ~EditorModuleUnloadListener() override;

        // EditorSubsystem — EditorContext 가 의존 순서로 초기화하고 역순으로 종료 · 소멸한다.
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "EditorModuleUnloadListener";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const>      getSubsystemDependencies() const override;
        SubsystemInitResult          initializeSubsystem() override;
        void                         shutdownSubsystem() override;
        EditorModuleUnloadListener( const EditorModuleUnloadListener& )            = delete;
        EditorModuleUnloadListener& operator=( const EditorModuleUnloadListener& ) = delete;

    private:
        /** @brief 등록 줄이 빠졌다 — 매니저들을 목록과 맞춘다. */
        void onRegistrationRemoved();
        /** @brief 모듈을 내린다 — 기본값 인스턴스를 지운다(다음에 물을 때 다시 만든다). */
        void onModuleUnloading( string_view moduleName );

        EditorContext& _context;
        Subscription   _registrationRemovedSubscription;
        Subscription   _moduleUnloadingSubscription;
    };
} // namespace sw::editor
