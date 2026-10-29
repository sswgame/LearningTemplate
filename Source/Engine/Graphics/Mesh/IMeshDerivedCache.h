/**
 * @file Engine/Graphics/Mesh/IMeshDerivedCache.h
 * @brief 메시에서 기하를 만들어 따로 들고 있는 위 층 캐시가 구현하는 인터페이스입니다(잎 헤더 — 물리 헤더가 메시 캐시 헤더를 끌지 않게 따로 둔다).
 */
#pragma once

namespace sw
{
    /**
     * @class IMeshDerivedCache
     * @brief 메시에서 기하를 만들어 따로 들고 있는 위 층 캐시(물리 셰이프)의 인터페이스입니다. `MeshCache` 가 다시 읽기(`reload`)와 비우기(`clear`) 뒤에 부른다.
     * @details 구현은 엔진 범위에 이 타입으로 걸리고(`EngineSubsystemSet` — 물리 셰이프 표), 메시 캐시가 `engine::findSubsystem<IMeshDerivedCache>()` 로 찾는다.
     *          위 층이 아래 층의 인터페이스를 구현하는 방향이라 그래픽스가 물리를 include 하지 않는다(`IRenderTargetDeclarationSink` 와 같은 모양). 예전에는 정적 초기화로 거는
     *          고리 목록(`SW_MESH_INVALIDATION_HOOK`)이었다. 받는 쪽은 하나다 — 둘째가 생기면 같은 타입을 두 번 걸 수 없어(`SubsystemScope::expose`) 기동에서 드러난다.
     *          언리얼 `UStaticMesh::OnMeshChanged` 를 바디 셋업이 받는 자리.
     */
    class IMeshDerivedCache
    {
    public:
        virtual ~IMeshDerivedCache() = default;

        /** @brief 메시가 다시 읽히거나 비워졌습니다 — 메시에서 만든 기하를 버린다(이미 만든 바디는 이전 기하 그대로). */
        virtual void onMeshesInvalidated() = 0;
    };
} // namespace sw
