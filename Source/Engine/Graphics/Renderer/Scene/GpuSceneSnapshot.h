/**
 * @file GpuSceneSnapshot.h
 * @brief 게임 스레드가 만들고 렌더 스레드가 받는 씬 스냅샷 — 두 스레드가 공유하는 **유일한** 타입.
 * @details 만드는 쪽은 `GpuSceneBuilder`(GT), 받아서 GPU 에 올리는 쪽은 `GpuScene`(RT) 이다. 두 클래스는 서로를 모르고
 *          이 구조체만 안다. 그래서 "GT 가 RT 의 값을 덮어쓴다" · "RT 가 씬을 읽는다" 같은 실수는 컴파일되지 않는다.
 *          소유 규칙(shared_ptr 로 소유를 함께 싣는다)은 `GpuSceneSnapshot` 주석과 Scripts/lint/gate/CheckRenderOwnership.py 참고.
 */
#pragma once
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

namespace sw
{
    class Material;
    class MaterialInstance;
    class Mesh;

    /// @brief 배치에 머티리얼 데이터 그룹이 없다 (GpuMeshBatch::_materialGroup).
    inline constexpr uint32 kInvalidMaterialGroup = 0xFFFFFFFFu;
    /// @brief 배치에 셰이더 퍼뮤테이션이 없다 — 패스가 자기 PSO 로 그린다 (GpuMeshBatch::_shaderPermutation).
    inline constexpr uint32 kInvalidShaderPermutation = 0xFFFFFFFFu;

    /// @brief GPU 인스턴스 (월드 행렬 + 메시/머티리얼 인덱스)
    struct GpuInstance
    {
        float4x4 _world{};
        float3   _boundsCenter{};
        float32  _boundsRadius{ 1.0f };
        uint32   _meshBatchIndex{ 0 };
        uint32   _materialIndex{ 0 };
        uint32   _blendMode{ 0 }; ///< RHIBlendMode
        /**
         * @brief GPU 인스턴스 애니메이션 시드 — 0 이면 애니메이션 없음.
         * @details instanceanim.hlsl 이 이 값을 해시해 **인스턴스마다 다른 각속도**를 만든다. 예전엔 여기가
         *          정렬용 `_pad` 였다 — 자리를 새로 만들지 않고 그 빈칸을 쓴다(셰이더 구조체 레이아웃 불변).
         *          CPU 가 매 프레임 회전을 계산해 올리던 것을 GPU 로 옮기는 통로다.
         */
        uint32 _spinSeed{ 0 };
    };

    /// @brief 같은 메시/머티리얼의 인스턴스 배치
    struct GpuMeshBatch
    {
        shared_ptr<Mesh> _mesh; ///< **소유를 싣는다** — RT 가 upload() 에서 이 메시를 역참조한다
        RHIBufferHandle  _vertexBuffer{ 0 };
        uint32           _vertexCount{ 0 };
        /**
         * @brief 정점 풀 안에서 이 메시의 시작(정점 단위) — 간접 인자의 startVertex. 풀 밖 메시는 0 (자기 정점 버퍼).
         * @details RT 가 upload() 에서 채운다. 같은 풀 버퍼를 쓰는 배치들은 정점 버퍼를 다시 걸지 않고 멀티 드로우로 묶인다.
         */
        uint32 _firstVertex{ 0 };
        uint32 _instanceBase{ 0 };
        uint32 _instanceCount{ 0 };
        uint32 _materialIndex{ 0 };
        /**
         * @brief 모프 풀에서 이 배치 메시의 시작 오프셋(정점 단위). 0xFFFFFFFF = 모프 안 함.
         * @details RT 가 `upload()` 에서 채운다 — GT 는 GPU 풀을 모른다(스냅샷 소유 규칙). 드로우는 이 값을
         *          루트 상수로 실어 정점 셰이더가 `g_SwMorphVertices[base + SV_VertexID]` 를 읽게 한다.
         */
        uint32             _morphVertexBase{ 0xFFFFFFFFu };
        RHIBlendMode       _blendMode  = RHIBlendMode::Opaque;
        RHIDescriptorIndex _materialCb = kInvalidDescriptorIndex;
        /**
         * @brief 배치가 쓰는 부모 머티리얼 — 셰이더 타입(머티리얼 데이터 그룹)과 텍스처 슬롯의 소유자.
         * @details **소유를 함께 싣는다.** 렌더 스레드는 이 배치가 든 패킷을 다 쓸 때까지 이 머티리얼을 역참조하므로,
         *          게임 스레드가 먼저 놓아도 살아 있어야 한다. 예전에는 생포인터였고 그 사이를 pin/retire 큐가
         *          지킨다고 돼 있었지만 그 큐의 답을 읽는 곳이 없었다 — 소유가 패킷을 따라가면 큐가 필요 없다.
         */
        shared_ptr<Material> _material;
        /** @brief 머티리얼 데이터 그룹(셰이더 타입) 인덱스 — _snapshot._listMaterialGroup. 없으면 kInvalidMaterialGroup. */
        uint32 _materialGroup{ 0xFFFFFFFFu };
        /**
         * @brief 이 배치를 그릴 셰이더 퍼뮤테이션 — `GpuScene::getShaderPermutations()` 인덱스.
         * @details 없으면 kInvalidShaderPermutation 이고, 그때는 패스가 자기 PSO 로 그린다. 배치 키에 이 퍼뮤테이션
         *          해시가 들어가므로, 한 배치의 인스턴스는 반드시 모두 같은 셰이더다.
         */
        uint32 _shaderPermutation{ 0xFFFFFFFFu };
        /**
         * @brief 그룹의 GPU 구조버퍼(g_SwMaterials, t9)와 SRV 인덱스 — upload 가 채운다.
         * @details 인스턴스의 _materialIndex 가 이 버퍼의 원소를 고른다 (언리얼 GPUScene 방식). 드로우마다 CB 를 갈아
         *          끼우지 않고, 배치마다 이 버퍼를 리플렉션 슬롯에 한 번 건다.
         */
        RHIBufferHandle    _materialBuffer{ 0 };
        RHIDescriptorIndex _materialSrv = kInvalidDescriptorIndex;
        /** @brief 그룹 버퍼의 원소 수 — PassCB g_SwMaterialCount 로 넘겨 셰이더가 인덱스를 클램프한다. */
        uint32 _materialCount{ 0 };
        /**
         * @brief 머티리얼 텍스처의 백엔드 SRV 인덱스(서수 순). 비네이티브 bindless 백엔드에서만 쓴다.
         * @details DX11/GL 은 셰이더가 전역 인덱스를 못 풀어서 엔진이 t5..t8 에 직접 바인딩해야 한다.
         *          렌더 스레드는 씬을 못 보므로(Material* 를 따라갈 수 없다) 값으로 실어 나른다.
         */
        RHIDescriptorIndex _arrMaterialTexSrv[shaderslot::kMaterialTextureCount] = {
            kInvalidDescriptorIndex, kInvalidDescriptorIndex, kInvalidDescriptorIndex, kInvalidDescriptorIndex };
        /** @brief RT 가 draw 직전에 updateRhi 한다. 수명은 이 shared_ptr 이 쥔다 — 패킷이 살아 있는 동안 산다. */
        shared_ptr<MaterialInstance> _materialInstance;
    };

    /**
     * @struct GpuMaterialElementKey
     * @brief 머티리얼 데이터 원소 하나를 가리키는 키 — (머티리얼, 인스턴스) 쌍.
     * @details 인스턴스가 없으면 머티리얼 자신이 원소다. 인스턴스는 CB 값만 덮어쓰므로 부모 머티리얼과 함께 봐야 한다.
     */
    // SW_OWNERSHIP_RAW_OK: 정체성 키다 — 비교만 하고 **역참조하지 않는다**. 소유를 실으면 키가 수명을 붙들어,
    //                      회수돼야 할 머티리얼이 스냅샷이 사는 동안 살아남는다(그것이 원소 표가 따로 있는 이유다).
    struct GpuMaterialElementKey
    {
        Material*         _pMaterial{ nullptr };
        MaterialInstance* _pInstance{ nullptr };
        /** @brief 같으면 true를 반환합니다. */
        bool operator==( const GpuMaterialElementKey& other ) const
        {
            return _pMaterial == other._pMaterial && _pInstance == other._pInstance;
        }
    };

    /**
     * @struct GpuMaterialElement
     * @brief 머티리얼 데이터 원소 하나 — (머티리얼, 인스턴스) 쌍의 **소유**.
     * @details 키(`GpuMaterialElementKey`)는 정체성이라 생포인터고, 원소는 렌더 스레드가 `getBuffer()` 로 읽으므로
     *          소유를 든다. 인스턴스가 없으면 머티리얼 자신이 원소다.
     */
    struct GpuMaterialElement
    {
        shared_ptr<Material>         _material;
        shared_ptr<MaterialInstance> _instance;
    };

    /// @brief GpuMaterialElementKey 해시 — 포인터 둘을 섞는다.
    struct GpuMaterialElementKeyHash
    {
        /** @brief 호출 연산자입니다. */
        size_t operator()( const GpuMaterialElementKey& key ) const
        {
            size_t hash = reinterpret_cast<size_t>( key._pMaterial ) * 1315423911u;
            hash ^= reinterpret_cast<size_t>( key._pInstance ) + 0x9e3779b9u + ( hash << 6 ) + ( hash >> 2 );
            return hash;
        }
    };

    /**
     * @struct GpuShaderPermutation
     * @brief 머티리얼이 요구하는 셰이더 변형 하나 — 경로 + 정적 define.
     * @details 언리얼의 `FMaterialShaderMap` 이 있는 자리다. 같은 .hlsl 이라도 정적 스위치가 다르면 **다른 셰이더**라서
     *          다른 PSO 로 그려야 한다. 예전엔 셰이더 **경로**만 봐서, 유리 머티리얼처럼 always-define 을 가진 것이
     *          불투명 머티리얼과 한 배치로 접혔다 — 배치는 PSO 하나로 그리므로 그 정보가 그냥 버려졌다.
     *
     *          렌더 스레드는 씬을 못 보므로(Material* 을 따라갈 수 없다) 값으로 실어 나른다.
     */
    struct GpuShaderPermutation
    {
        /// @brief 머티리얼이 선언한 셰이더. 패스가 자기 셰이더를 쓰는 경우(그림자·뎁스)엔 무시된다.
        string _shaderPath;
        /// @brief always-define + 정적 스위치 + 멀티컴파일 선택. 그대로 PSO 의 셰이더 define 이 된다.
        vector<string> _listDefine;
        /// @brief (경로, define) 해시 — PSO 캐시 키이자 **배치 병합 키**다.
        uint64 _hash{ 0 };
    };

    /**
     * @struct GpuMaterialGroup
     * @brief 셰이더 타입(머티리얼 셰이더 경로)별 머티리얼 데이터 원소 목록 — CPU 스냅샷의 일부.
     * @details 원소 순서가 곧 materialIndex 다. 같은 셰이더를 쓰는 머티리얼/인스턴스는 구조체 레이아웃이 같아 한 버퍼에 쌓인다.
     */
    struct GpuMaterialGroup
    {
        string                     _shaderPath;
        vector<GpuMaterialElement> _listEntry;
        // 원소 인덱스 표(키 → 인덱스 · 마지막 사용 빌드 · 프리리스트 · 직전 조회)는 **빌더의 것**이다
        // (`GpuSceneBuilder::MaterialGroupState`). 스냅샷에는 RT 가 읽는 것만 싣는다 — 예전에는 표까지 이 안에 있어
        // 프레임마다 패킷으로 복사됐다(맵 하나 + 벡터 둘, 그룹마다).
    };

    /** @brief 인스턴스 배열에서 바뀐 구간 `[_start, _start + _count)`. */
    struct GpuInstanceRun
    {
        uint32 _start{ 0 };
        uint32 _count{ 0 };
    };

    /**
     * @struct GpuSceneSnapshot
     * @brief 게임 스레드가 만들고 렌더 패킷에 실어 렌더 스레드로 **옮기는 전부**.
     *
     * @details 소유 규칙이 이 타입에 적혀 있다:
     *          - 렌더 스레드가 역참조하는 CPU 객체(메시·머티리얼·인스턴스)는 **shared_ptr 로 소유를 함께 싣는다.**
     *            패킷이 살아 있는 동안(렌더 큐 깊이만큼) 게임 스레드가 놓아도 산다. 생포인터는 정렬 키 같은
     *            **정체성**에만 쓰고 스레드를 넘어 역참조하지 않는다.
     *          - RT 가 upload() 에서 만드는 값(GPU 핸들·간접 개수·컬 뷰)은 여기 없다 — 그래서 옮겨질 수 없다.
     *            "한쪽만 만드는 값을 스냅샷이 매 프레임 덮어쓴다" 는 버그가 타입으로 막힌다.
     *          - 모듈(게임 DLL)이 만든 객체를 엔진이 마지막까지 들 수 있으므로, 실리는 객체는 Engine 의
     *            create() 팩토리로 만든다(제어 블록이 Engine.dll 에 산다).
     *          검사는 Scripts/lint/gate/CheckRenderOwnership.py 가 한다.
     */
    struct GpuSceneSnapshot
    {
        /**
         * @brief 인스턴스 배열 — **값이 아니라 공유한다.** RT 는 읽기만 하므로 프레임마다 복사할 이유가 없다.
         *
         * @details 예전에는 값이었고 `exportCpuSnapshot` 이 매 프레임 통째로 복사했다. 아무것도 움직이지
         *          않는 정적 씬에서도 인스턴스당 55 ns 가 들었고(엔티티 8000 개면 441 us), 그때 그것이
         *          **게임 스레드의 유일한 실제 작업**이었다(build 0 · flushTransforms 0 · update 0).
         *
         *          배치·머티리얼 목록은 그대로 값이다 — **RT 가 GPU 핸들을 거기에 덧칠하므로**
         *          공유하면 안 된다(`GpuMeshBatch::_firstVertex` · `_materialCb` 주석 참고). 대신 개수가
         *          적어 복사가 싸다. 큰 것만 공유하고 작은 것은 복사하는 것이 이 타입의 규칙이다.
         */
        shared_ptr<const vector<GpuInstance>> _pListInstance;
        vector<GpuMeshBatch>                  _listOpaqueBatch;
        vector<GpuMeshBatch>                  _listTransparentBatch;
        vector<GpuMeshBatch>                  _listAllBatch;      ///< 불투명 다음 투명. 간접 슬롯과 일치
        vector<GpuMaterialGroup>              _listMaterialGroup; ///< 셰이더 타입별 머티리얼 원소
        /**
         * @brief 퍼뮤테이션 목록 — 배치의 `_shaderPermutation` 이 가리킨다. **불변 목록을 공유한다.**
         * @details 문자열 경로와 define 목록이 든 값이라 프레임마다 복사하면 그 문자열들이 전부 힙이었다(프레임당 ~30 회).
         *          빌더는 새 퍼뮤테이션이 나타날 때만 목록을 새로 만들어 바꿔 끼운다(copy-on-write). RT 는 읽기만 한다.
         */
        shared_ptr<const vector<GpuShaderPermutation>> _pListShaderPermutation;
        /// @brief GPU 회전을 요청한 인스턴스 수 (0 이면 애니메이션 디스패치를 건너뛴다).
        uint32 _spinInstanceCount{ 0 };
        /// @brief 마지막 buildFromScene 이 내용을 바꿨는가. RT 는 0 이면 인스턴스 재업로드를 생략한다.
        uint8 _bCpuDirty{ SW_TRUE };
        /**
         * @brief 1 이면 인스턴스 배열 **전체**가 바뀌었다. 0 이면 `_listDirtyInstanceRun` 만 바뀌었다.
         * @details 전체 재구축은 1, 물체만 움직인 프레임은 0 이다. 버퍼를 새로 만든 프레임도 받는 쪽이
         *          전체로 취급해야 한다 — 새 버퍼에는 아직 아무것도 없다.
         */
        uint8 _bAllInstancesDirty{ SW_TRUE };
        /**
         * @brief 바뀐 인스턴스 구간들 (`_bAllInstancesDirty` 가 0 일 때만 뜻이 있다).
         * @details 인스턴스 버퍼는 하나만 움직여도 **전체**를 다시 올리고 있었다 — 8000 개 중 10 개만
         *          움직여도 800 개를 움직일 때와 같은 100 us 였다. 구간이 너무 잘게 흩어지면
         *          작은 업로드가 도리어 비싸므로, 빌더가 개수 상한을 넘기면 전체로 돌린다.
         */
        vector<GpuInstanceRun> _listDirtyInstanceRun;

        /** @brief 인스턴스 배열. 아직 발행 전이면 빈 배열을 돌려준다. */
        const vector<GpuInstance>& getInstances() const
        {
            static const vector<GpuInstance> s_listEmpty{};
            return ( _pListInstance != nullptr ) ? *_pListInstance : s_listEmpty;
        }
    };
} // namespace sw
