/**
 * @file EditorModuleUnloadListener.h
 * @brief 확장 모듈 이미지가 언로드되기 전에 그 이미지의 등록 줄로 만든 에디터 인스턴스를 지웁니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Module/ModuleUnloadListener.h"

namespace sw::editor
{
    class EditorContext;

    /**
     * @class EditorModuleUnloadListener
     * @brief 확장 모듈 이미지가 언로드되기 전에(핫 리로드 · 종료) 그 이미지의 등록 줄로 만든 패널 · 팝업 · 인스펙터를 지웁니다.
     * @details 리스너 객체는 EditorModule 코드라 EditorModule 이 언로드되면 vtable 도 사라집니다. 그래서 EditorContext 가 소유하고 에디터를 종료할 때 함께 지웁니다
     *          (`IModuleUnloadListener` 의 주의 — 모듈보다 오래 살면 안 된다). EditorModule 자신이 언로드될 때는 에디터 인스턴스가 먼저 통째로 사라지므로 할 일이 없습니다.
     */
    class EditorModuleUnloadListener final : public IModuleUnloadListener
    {
    public:
        explicit EditorModuleUnloadListener( EditorContext& context );
        ~EditorModuleUnloadListener() override;
        EditorModuleUnloadListener( const EditorModuleUnloadListener& )            = delete;
        EditorModuleUnloadListener& operator=( const EditorModuleUnloadListener& ) = delete;

        const utf8* getModuleUnloadListenerName() const override { return "editor extension instances"; }
        uint32      onModuleUnloading( const void* pBegin, const void* pEnd, bool& outKeepImageMapped ) override;
        bool        isReleaseExpected() const override { return true; }

    private:
        EditorContext& _context;
    };
} // namespace sw::editor
