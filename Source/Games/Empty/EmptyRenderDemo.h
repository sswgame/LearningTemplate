/**
 * @file EmptyRenderDemo.h
 * @brief 모듈 렌더 확장 시연(ModuleRenderPass M3 · M4) — 게임 모듈이 렌더 스레드 확장 객체와 씬 프록시를 등록하는 모양을 보입니다. 클라이언트 빌드에만 있습니다.
 * @details - `Empty.DrawDemo`(M4): 게임 스레드가 큐브 몇 개의 값(월드 행렬 · 색)을 프록시 갱신으로 올리고, 렌더 스레드가 프록시의 `collectDrawElements` 로 요소를 받아 그린다.
 *          - `Empty.TintDemo`(M3): 그려진 장면 색 위에 틴트를 블렌드하는 풀스크린 패스. 색은 매 프레임 확장 안에서 값으로 정한다.
 *          두 패스 모두 파이프라인 `game/empty/pipeline/modulepasspipeline.xml` 이 셰이더를 `_listModuleShader` 로 선언한 위에서 돈다. 그 파이프라인을 고르지 않은 실행에서는
 *          셰이더가 선언되지 않았으므로 두 패스는 오류를 한 번 남기고 넣어지지 않는다(대체 셰이더 없음).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/Renderer/Module/ModuleRenderPassRegistry.h"

namespace sw
{
    /** @brief 큐브 요소를 내는 렌더 스레드 프록시입니다. 값은 `setElements`(프레임 시작에 렌더 스레드가 적용하는 갱신)로만 바뀝니다. */
    class EmptyDrawDemoProxy final : public IModuleSceneProxy
    {
    public:
        void setElements( const vector<ModuleDrawElement>& listElement ) { _listElement = listElement; }
        void collectDrawElements( ModuleDrawCollector& out, const ModuleFrameInfo& frame ) const override;

    private:
        vector<ModuleDrawElement> _listElement;
    };

    /** @brief 확장 객체입니다. 프레임마다 두 패스를 선언합니다. */
    class EmptyRenderExtension final : public IModuleRenderExtension
    {
    public:
        void addPasses( ModulePassBuilder& builder, const ModuleFrameInfo& frame ) override;
    };

    /**
     * @class EmptyRenderDemo
     * @brief 시연 한 벌의 수명입니다. 확장 · 프록시를 올리고, 소멸하면 flush 한 뒤 풉니다(핫 리로드로 모듈이 내려가도 렌더 스레드에 코드가 남지 않는다).
     * @details 멤버 선언 순서가 수명 순서입니다 — 확장 객체가 먼저 만들어지고 등록이 풀린 뒤에 소멸합니다.
     */
    class EmptyRenderDemo
    {
    public:
        explicit EmptyRenderDemo( ModuleRenderPassRegistry& registry );

    private:
        EmptyRenderExtension              _extension;
        ModuleRenderExtensionRegistration _extensionRegistration;
        ModuleSceneProxyRegistration      _proxyRegistration;
    };
} // namespace sw
