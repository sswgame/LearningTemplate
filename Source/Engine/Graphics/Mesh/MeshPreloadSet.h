/**
 * @file MeshPreloadSet.h
 * @brief 플레이 중에 처음 쓸 메시를 워커에서 미리 읽어 보관하는 집합입니다(언리얼 `FStreamableManager::RequestAsyncLoad` 의 핸들에 해당합니다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Task/TaskTypes.h"

namespace sw
{
    class Mesh;

    /**
     * @class MeshPreloadSet
     * @brief 경로 목록의 메시를 백그라운드 태스크(`TaskPriority::Low`)에서 `MeshCache::acquire` 로 읽고, 이 집합이 살아 있는 동안 보관합니다.
     * @details 메시 캐시는 약한 참조이므로 아무도 보관하지 않으면 읽은 메시가 바로 사라집니다. 그래서 이 집합이 보관합니다. 플레이 중에 처음 스폰하는 종류의
     *          메시를 게임 스레드가 동기로 읽으면 느린 디스크에서 그 프레임이 읽기 한 번만큼 늘어납니다(히치). 플레이를 시작할 때 `request` 하면
     *          그 읽기가 워커로 갑니다. 다 읽기 전에 게임 스레드가 같은 경로를 `acquire` 하면 그 자리에서 읽고, 먼저 넣은 쪽을 둘이 공유합니다.
     * @warning 태스크가 이 객체를 가리킵니다. 없애기 전에 `clear` 하거나(소멸자가 기다린다) 태스크가 끝나야 합니다.
     */
    class SW_API MeshPreloadSet
    {
    public:
        MeshPreloadSet() = default;
        ~MeshPreloadSet();

        MeshPreloadSet( const MeshPreloadSet& )            = delete;
        MeshPreloadSet& operator=( const MeshPreloadSet& ) = delete;

        /**
         * @brief @p listPath 의 메시를 미리 읽기 시작합니다. 이미 보관 중인 메시는 먼저 놓습니다(`clear`).
         * @details 엔진 서비스가 없으면(테스트와 도구) 그 자리에서 읽습니다.
         */
        void request( vector<string> listPath );

        /** @brief 읽기가 끝날 때까지 기다리고 보관 중인 메시를 모두 놓습니다. */
        void clear();

        /** @brief 요청한 읽기가 끝났으면 true 입니다(요청이 없어도 true). */
        [[nodiscard]] bool isComplete() const;

        /** @brief 지금 보관 중인 메시 수입니다(읽기가 끝난 뒤에 읽으십시오). 읽지 못한 경로는 세지 않습니다. */
        [[nodiscard]] size_t getLoadedCount() const;

    private:
        /** @brief 태스크 본문입니다. 경로마다 `MeshCache::acquire` 로 읽어 보관합니다. 이 태스크만 `_listMesh` 를 씁니다. */
        void loadAll( const TaskArgs& args );

    private:
        vector<string>           _listPath;
        vector<shared_ptr<Mesh>> _listMesh;
        TaskStageHandle          _stage;
    };
} // namespace sw
