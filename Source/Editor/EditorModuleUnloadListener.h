/**
 * @file EditorModuleUnloadListener.h
 * @brief 확장 모듈 라이브러리가 언로드되기 전에 그 이미지의 등록 줄로 만든 에디터 인스턴스를 지웁니다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Module/ModuleLibraryUtil.h"

#include "Editor/Common/EditorSubsystem.h"

namespace sw::editor
{
    class EditorContext;

    /**
     * @class EditorModuleUnloadListener
     * @brief 확장 모듈 라이브러리가 언로드되기 전에(핫 리로드 · 종료) 그 이미지의 등록 줄로 만든 패널 · 팝업 · 인스펙터 · 커맨드를 지웁니다.
     * @details 리스너 객체는 EditorModule 코드라 EditorModule 이 언로드되면 vtable 도 사라집니다. 그래서 EditorContext 가 소유하고 에디터를 종료할 때 함께 지웁니다
     *          (`ModuleLibraryUtil::registerUnloadHandler` 의 주의 — 모듈보다 오래 살면 안 된다). EditorModule 자신이 언로드될 때는 에디터 인스턴스가 먼저 통째로 사라지므로 할 일이 없습니다.
     *          훑는 매니저들을 의존으로 선언하므로 그들보다 먼저 종료 · 소멸합니다(언로드 훑기가 지워진 매니저를 밟지 않는다).
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
        EditorModuleUnloadListener( const EditorModuleUnloadListener& )            = delete;
        EditorModuleUnloadListener& operator=( const EditorModuleUnloadListener& ) = delete;

        uint32 onModuleUnloading( const void* pBegin, const void* pEnd, bool& outKeepLibraryMapped );

    private:
        EditorContext& _context;

    private:
        /** @brief 모듈 언로드 처리기 구독(`ModuleLibraryUtil::registerUnloadHandler`)입니다. 맨 끝 멤버라 먼저 사라져, 다른 멤버가 사라지는 동안 불리지 않는다. */
        Subscription _moduleUnloadSubscription{ ModuleLibraryUtil::registerUnloadHandler( { "editor extension instances", true }, ModuleUnloadFunction::create<&EditorModuleUnloadListener::onModuleUnloading>( this ) ) };
    };
} // namespace sw::editor
