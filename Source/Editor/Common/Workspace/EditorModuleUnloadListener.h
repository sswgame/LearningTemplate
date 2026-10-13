/**
 * @file EditorModuleUnloadListener.h
 * @brief 확장 모듈 이미지가 언로드되기 전에 그 이미지의 등록 줄로 만든 에디터 인스턴스를 지웁니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Module/ModuleUnloadListener.h"

#include "Editor/Common/EditorSubsystem.h"

namespace sw::editor
{
    class EditorContext;

    /**
     * @class EditorModuleUnloadListener
     * @brief 확장 모듈 이미지가 언로드되기 전에(핫 리로드 · 종료) 그 이미지의 등록 줄로 만든 패널 · 팝업 · 인스펙터 · 커맨드를 지웁니다.
     * @details 리스너 객체는 EditorModule 코드라 EditorModule 이 언로드되면 vtable 도 사라집니다. 그래서 EditorContext 가 소유하고 에디터를 종료할 때 함께 지웁니다
     *          (`IModuleUnloadListener` 의 주의 — 모듈보다 오래 살면 안 된다). EditorModule 자신이 언로드될 때는 에디터 인스턴스가 먼저 통째로 사라지므로 할 일이 없습니다.
     *          훑는 매니저들을 의존으로 선언하므로 그들보다 먼저 종료 · 소멸합니다(언로드 훑기가 지워진 매니저를 밟지 않는다).
     */
    class EditorModuleUnloadListener final : public EditorSubsystem, public IModuleUnloadListener
    {
    public:
        explicit EditorModuleUnloadListener( EditorContext& context );
        ~EditorModuleUnloadListener() override;

        // EditorSubsystem — EditorContext 가 의존 순서로 초기화하고 역순으로 종료 · 소멸한다.
        const utf8* getSubsystemName() const override { return "EditorModuleUnloadListener"; }
        const utf8* getSubsystemDependencies() const override { return "EditorPanelManager EditorPopupManager InspectorComponentManager InspectorPropertyManager EditorDefaultObjects"; }
        EditorModuleUnloadListener( const EditorModuleUnloadListener& )            = delete;
        EditorModuleUnloadListener& operator=( const EditorModuleUnloadListener& ) = delete;

        const utf8* getModuleUnloadListenerName() const override { return "editor extension instances"; }
        uint32      onModuleUnloading( const void* pBegin, const void* pEnd, bool& outKeepImageMapped ) override;
        bool        isReleaseExpected() const override { return true; }

    private:
        EditorContext& _context;
    };
} // namespace sw::editor
