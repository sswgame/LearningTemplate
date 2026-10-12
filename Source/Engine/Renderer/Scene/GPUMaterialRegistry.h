/**
 * @file GPUMaterialRegistry.h
 * @brief 머티리얼 그룹(셰이더 타입), 머티리얼 원소의 영속 인덱스, 셰이더 퍼뮤테이션 표를 관리합니다(언리얼 GPUScene 의 영속 ID 에 해당).
 * @details `GPUSceneBuilder` 가 값 멤버로 두고 직접 부른다(가상 호출 없음). 렌더 스레드가 읽는 것(그룹의 경로와 원소, 퍼뮤테이션 목록)은
 *          스냅샷(`GPUSceneSnapshot::_listMaterialGroup`, `_pListShaderPermutation`)에 두고, 찾는 맵, 회수 시계, 프리리스트는 여기에 둔다.
 *          스냅샷은 빌더가 가지고 매 프레임 복사해 내보내므로 함수마다 인자로 받는다(포인터로 붙들면 빌더를 옮길 때 끊어진다).
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Renderer/Scene/GPUSceneSnapshot.h"

namespace sw
{
    class Material;
    class MaterialInstance;

    /**
     * @class GPUMaterialRegistry
     * @brief 머티리얼 원소 레지스트리입니다. 게임 스레드 전용이며 GPU 를 알지 못합니다.
     */
    class SW_API GPUMaterialRegistry
    {
    public:
        /**
         * @brief (머티리얼, 인스턴스)의 퍼뮤테이션 해시를 반환합니다. 셰이더 경로와 정적 define 을 함께 봅니다.
         * @details 인스턴스는 키워드를 덮어쓸 수 있으므로 인스턴스가 있으면 그 해시를 쓴다(부모의 해시를 이미 포함한다).
         */
        static uint64 permutationHashFor( const Material* pMaterial, const MaterialInstance* pInstance );

        /**
         * @brief 그룹 목록, 원소 테이블, 퍼뮤테이션 표를 통째로 비웁니다. 그룹 기준이 바뀌었을 때 부릅니다.
         * @details 그룹 목록(스냅샷)과 셰이더 경로에서 인덱스를 찾는 맵은 한 몸이라 함께 비운다. 회수 시계(빌드 번호)는 그대로 둔다.
         */
        void reset( GPUSceneSnapshot& inoutSnapshot );
        /** @brief 머티리얼의 셰이더 타입 그룹 인덱스를 찾거나 만듭니다(배치를 방출할 때 부릅니다). 머티리얼이 없으면 kInvalidMaterialGroup 입니다. */
        uint32 materialGroupFor( const Material* pMaterial, GPUSceneSnapshot& inoutSnapshot );
        /** @brief 퍼뮤테이션 인덱스를 찾거나 만듭니다(배치를 방출할 때 부릅니다). 머티리얼이 없으면 kInvalidShaderPermutation 입니다. */
        uint32 shaderPermutationFor( const Material* pMaterial, const MaterialInstance* pInstance, GPUSceneSnapshot& inoutSnapshot );
        /**
         * @brief 전체 재구축을 시작합니다. 빌드 번호를 올리고 그룹마다의 직전 조회 결과를 버립니다.
         * @details 원소 인덱스는 지우지 않는다. 처음 본 쌍에만 슬롯을 주고 쓰이지 않으면 `freeUnusedMaterialElements` 가 지연 회수한다.
         */
        void beginBuild();
        /**
         * @brief (머티리얼, 인스턴스) 쌍을 그룹에 넣고 원소 인덱스(materialIndex)를 반환합니다(배치를 방출할 때 인스턴스마다 부릅니다).
         * @details 같은 쌍은 같은 원소를 공유한다. 배치를 합치면 한 배치 안에 원소가 여러 개 들어간다. 이번 빌드 번호로 스탬프를 찍는다.
         */
        uint32 assignMaterialElement( const shared_ptr<Material>& material, const shared_ptr<MaterialInstance>& instance, uint32 groupIndex,
                                      GPUSceneSnapshot& inoutSnapshot );
        /**
         * @brief 배치 구성을 그대로 둔 프레임(제자리 갱신)에 회수 시계를 돌립니다. 빌드 번호를 올리고 배치가 쓰는 원소에 스탬프를 찍습니다.
         * @details 스탬프는 원소에 찍는다. 인스턴스를 돌 필요 없이 배치가 기록해 둔 (배치, 원소) 쌍만 돈다.
         *          listBatchElementRange 는 배치마다 listBatchElementIndex 의 구간이다.
         */
        void stampBatchElements( const vector<GPUMeshBatch>& listBatch, const vector<GPUInstanceRun>& listBatchElementRange,
                                 const vector<uint32>& listBatchElementIndex );
        /** @brief 오래 쓰이지 않은 머티리얼 원소를 회수해 프리리스트로 돌립니다. 인덱스는 옮기지 않습니다. */
        void freeUnusedMaterialElements( GPUSceneSnapshot& inoutSnapshot );

    private:
        /**
         * @brief 머티리얼 그룹의 원소 인덱스 테이블입니다. `GPUSceneSnapshot::_listMaterialGroup` 과 같은 인덱스로 나란히 둡니다.
         * @details 스냅샷의 그룹에는 RT 가 읽는 것(경로, 원소)만 남기고 테이블은 여기 둔다. 프레임마다 패킷으로 복사되지 않는다.
         */
        struct MaterialGroupState
        {
            unordered_map<GPUMaterialElementKey, uint32, GPUMaterialElementKeyHash> _mapEntryToIndex;
            /// @brief 원소별로 마지막으로 쓰인 빌드 번호입니다. 오래 쓰이지 않은 원소를 회수하는 기준입니다.
            vector<uint64> _listEntryLastUsedBuild;
            /// @brief 회수된 원소 슬롯입니다. **인덱스를 옮기지 않고** 재사용합니다. 옮기면 영속 ID 가 아니게 됩니다.
            vector<uint32> _listFreeEntry;
            /// @brief 직전 조회 결과입니다. 배치 안의 인스턴스는 정렬되어 있어 대부분 같은 원소를 연속으로 조회합니다.
            GPUMaterialElementKey _lastKey{};
            uint32                _lastIndex{ 0 };
            uint8                 _bHasLast{ SW_FALSE };
        };

        /// @brief 셰이더 경로에서 `GPUSceneSnapshot::_listMaterialGroup` 인덱스를 찾는 맵입니다. 배치마다 그룹 목록을 string 비교로 훑지 않기 위한 것입니다.
        unordered_map<string, uint32> _mapShaderPathToGroup;
        vector<MaterialGroupState>    _listMaterialGroupState;
        /// @brief 퍼뮤테이션 해시에서 `GPUSceneSnapshot::_pListShaderPermutation` 의 인덱스를 찾는 맵입니다.
        unordered_map<uint64, uint32> _mapPermutationToIndex;
        /// @brief 빌드 번호입니다. 원소가 마지막으로 쓰인 시점을 재는 데만 씁니다(회수 판정).
        uint64 _buildCounter{ 0 };
    };
} // namespace sw
