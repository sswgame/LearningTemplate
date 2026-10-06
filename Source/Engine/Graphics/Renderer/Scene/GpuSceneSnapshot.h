/**
 * @file GpuSceneSnapshot.h
 * @brief 게임 스레드가 만들고 렌더 스레드가 받는 씬 스냅샷입니다. 두 스레드가 공유하는 **유일한** 타입입니다.
 * @details 만드는 쪽은 `GpuSceneBuilder`(GT), 받아서 GPU 에 올리는 쪽은 `GpuScene`(RT) 입니다. 두 클래스는 서로를 모르고
 *          이 구조체만 압니다. 그래서 "GT 가 RT 의 값을 덮어쓴다" · "RT 가 씬을 읽는다" 같은 실수는 컴파일되지 않습니다.
 *          소유 규칙(shared_ptr 로 소유를 함께 싣는다)은 `GpuSceneSnapshot` 주석과 Scripts/lint/gate/CheckRenderOwnership.py 참고.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/HashUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Graphics/Shader/Binding/GpuSpriteInstanceData.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

namespace sw
{
    class Material;
    class MaterialInstance;
    class Mesh;

    /// @brief 배치에 머티리얼 데이터 그룹이 없음을 뜻합니다(GpuMeshBatch::_materialGroup).
    inline constexpr uint32 kInvalidMaterialGroup = invalid_index::kUint32;
    /// @brief 배치에 셰이더 퍼뮤테이션이 없음을 뜻합니다. 그러면 패스가 자기 PSO 로 그립니다(GpuMeshBatch::_shaderPermutation).
    inline constexpr uint32 kInvalidShaderPermutation = invalid_index::kUint32;

    /**
     * @brief GPU 인스턴스 하나입니다(월드 행렬 · 바운드 · 배치 인덱스 · 머티리얼 원소 인덱스 · 블렌드 · 회전 시드 · 스프라이트 프레임과 색).
     * @details HLSL 쪽은 `Resource/engine/shaders/instancedata.hlsli` 의 `SwInstanceData` 하나이고 그래픽스 · 컴퓨트가 함께 씁니다. 필드를
     *          고치면 그 파일과 ShaderBindingValidatorTest.InstanceElementLayoutMatchesCpuStruct 의 표를 함께 고칩니다(쿠킹된 바이너리로 대조합니다).
     */
    struct GpuInstance
    {
        float4x4 _world{};
        float3   _boundsCenter{};
        float32  _boundsRadius{ 1.0f };
        uint32   _meshBatchIndex{ 0 };
        uint32   _materialIndex{ 0 };
        uint32   _blendMode{ 0 }; ///< RHIBlendMode
        /**
         * @brief GPU 인스턴스 애니메이션 시드입니다. 0 이면 애니메이션이 없습니다.
         * @details instanceanim.hlsl 이 이 값을 해시해 **인스턴스마다 다른 각속도**를 만듭니다. 회전은 CPU 가 매 프레임
         *          계산해 올리지 않고 GPU 가 계산합니다.
         */
        uint32 _spinSeed{ 0 };
        /**
         * @brief 스프라이트 프레임(UV 사각형)과 색입니다. 지금 읽는 셰이더는 sprite2d.hlsl 하나입니다.
         * @details 머티리얼 인스턴스가 아니라 여기 싣는 이유는 `GpuSpriteInstanceData` 주석에 있습니다. 배치 키(머티리얼 인스턴스)를 건드리지
         *          않아 같은 텍스처의 스프라이트가 프레임 · 색이 달라도 한 배치이고, 바뀌면 그 인스턴스만 더티 구간으로 올라갑니다.
         */
        GpuSpriteInstanceData _sprite{};
        /**
         * @brief 정점 애니메이션(VAT) 시각 오프셋(초)입니다. VAT 가 걸린 메시만 읽습니다(binding.hlsli `swLoadAnimatedVertex`).
         * @details 셰이더는 VAT 시계(`GpuSceneSnapshot::_vertexAnimationTime`)에 이것을 더한 시각의 프레임을 그립니다 — 군중 시스템이 VAT 로 넘길 때
         *          그 캐릭터의 클립 시각에 맞춰 한 번 적고, 그 뒤로는 바뀌지 않아 인스턴스를 다시 올리지 않습니다. 셰이더의 `vertexAnimationPhase` 입니다.
         */
        float32 _vertexAnimationPhase{ 0.0f };
        uint32  _arrReserved[3]{}; ///< 원소를 128 바이트(16 의 배수)로 맞춘다 — 셰이더의 `reserved0..2`
    };
} // namespace sw

namespace sw
{
    static_assert( sizeof( GpuInstance ) == 128, "GpuInstance must match SwInstanceData (instancedata.hlsli) byte for byte" );

    /// @brief 같은 메시 · 머티리얼 · 퍼뮤테이션으로 그리는 인스턴스 배치입니다.
    struct GpuMeshBatch
    {
        shared_ptr<Mesh> _mesh; ///< **소유를 싣습니다.** RT 가 upload() 에서 이 메시를 역참조합니다
        RHIBufferHandle  _vertexBuffer{ 0 };
        uint32           _vertexCount{ 0 };
        /**
         * @brief 정점 풀 안에서 이 메시의 시작(정점 단위)입니다. 간접 인자의 startVertex 가 됩니다. 풀 밖 메시는 0 입니다(자기 정점 버퍼).
         * @details RT 가 upload() 에서 채웁니다. 같은 풀 버퍼를 쓰는 배치들은 정점 버퍼를 다시 걸지 않고 멀티 드로우로 묶입니다.
         */
        uint32 _firstVertex{ 0 };
        uint32 _instanceBase{ 0 };
        uint32 _instanceCount{ 0 };
        uint32 _materialIndex{ 0 };
        /**
         * @brief 모프 풀에서 이 배치 메시의 시작 오프셋(정점 단위)입니다. 0xFFFFFFFF = 모프 안 함.
         * @details RT 가 `GpuScene::assignMorphBases` 로 채웁니다. GT 는 GPU 풀을 모릅니다(스냅샷 소유 규칙). upload 가 이 값을
         *          배치 표(`GpuBatchInfo`, g_SwBatches t13)에 옮겨 적고, 정점 셰이더가 자기 배치 번호로 읽습니다(binding.hlsli swComputeMorphElement).
         */
        uint32 _morphVertexBase{ 0xFFFFFFFFu };
        /**
         * @brief 정점 애니메이션(VAT) 표에서 이 배치 메시의 머리 원소입니다. 0xFFFFFFFF = VAT 없음.
         * @details RT 가 `GpuScene::assignVertexAnimationBases` 로 채웁니다(모프 시작과 같은 길 — 배치 표 `GpuBatchInfo` 에 실린다).
         */
        uint32       _vertexAnimationBase{ 0xFFFFFFFFu };
        RHIBlendMode _blendMode = RHIBlendMode::Opaque;
        /**
         * @brief 인스턴스의 월드 행렬식이 음수(거울 변환)인 배치입니다. 이 배치는 컬 모드를 뒤집은 PSO 로 그립니다(언리얼 `bReverseCulling`).
         * @details 거울 변환은 삼각형 감김을 뒤집습니다. 컬 모드를 그대로 두면 카메라 쪽 면이 잘리고 안쪽 면이 보입니다. 컬 모드는 PSO 의 상태라
         *          배치 키에 들어갑니다 — 한 배치의 인스턴스는 모두 같은 부호입니다(`FrameRenderer::psoForBatch`).
         */
        uint8              _bReverseCulling{ SW_FALSE };
        RHIDescriptorIndex _materialCb = kInvalidDescriptorIndex;
        /**
         * @brief 배치가 쓰는 부모 머티리얼입니다. 셰이더 타입(머티리얼 데이터 그룹)과 텍스처 슬롯의 소유자입니다.
         * @details **소유를 함께 싣습니다.** 렌더 스레드는 이 배치가 든 패킷을 다 쓸 때까지 이 머티리얼을 역참조하므로,
         *          게임 스레드가 먼저 놓아도 살아 있어야 합니다. 소유가 패킷을 따라가므로 수명을 따로 지키는 큐가 필요 없습니다.
         */
        shared_ptr<Material> _material;
        /** @brief 머티리얼 데이터 그룹(셰이더 타입) 인덱스입니다(`_listMaterialGroup`). 없으면 kInvalidMaterialGroup 입니다. */
        uint32 _materialGroup{ 0xFFFFFFFFu };
        /**
         * @brief 이 배치를 그릴 셰이더 퍼뮤테이션입니다. `_pListShaderPermutation` 의 인덱스이고 `GpuScene::findShaderPermutation` 으로 찾습니다.
         * @details 없으면 kInvalidShaderPermutation 이고, 그때는 패스가 자기 PSO 로 그립니다. 배치 키에 이 퍼뮤테이션
         *          해시가 들어가므로, 한 배치의 인스턴스는 반드시 모두 같은 셰이더입니다.
         */
        uint32 _shaderPermutation{ 0xFFFFFFFFu };
        /**
         * @brief 그룹의 GPU 구조버퍼(g_SwMaterials, t9)와 SRV 인덱스입니다. upload 가 채웁니다.
         * @details 인스턴스의 _materialIndex 가 이 버퍼의 원소를 고릅니다(언리얼 GPUScene 방식). 드로우마다 CB 를 갈아
         *          끼우지 않고, 배치마다 이 버퍼를 리플렉션 슬롯에 한 번 겁니다.
         */
        RHIBufferHandle    _materialBuffer{ 0 };
        RHIDescriptorIndex _materialSrv = kInvalidDescriptorIndex;
        /** @brief 그룹 버퍼의 원소 수입니다. 드로우 루트 상수 g_SwMaterialCount 로 넘겨 셰이더가 인덱스를 클램프합니다. */
        uint32 _materialCount{ 0 };
        /**
         * @brief 머티리얼 텍스처의 백엔드 SRV 인덱스(서수 순)입니다. 비네이티브 bindless 백엔드에서만 씁니다.
         * @details DX11 · GL 은 셰이더가 전역 인덱스를 못 풀어서 엔진이 t5..t8 에 직접 바인딩해야 합니다.
         *          렌더 스레드는 씬을 못 보므로(Material* 를 따라갈 수 없습니다) 값으로 실어 나릅니다.
         */
        RHIDescriptorIndex _arrMaterialTexSrv[shaderslot::kMaterialTextureCount] = {
            kInvalidDescriptorIndex, kInvalidDescriptorIndex, kInvalidDescriptorIndex, kInvalidDescriptorIndex };
        /** @brief 배치의 머티리얼 인스턴스입니다. RT 가 upload() 에서 updateRhi 합니다. 수명은 이 shared_ptr 이 쥐어, 패킷이 살아 있는 동안 삽니다. */
        shared_ptr<MaterialInstance> _materialInstance;

        /**
         * @brief 두 배치가 머티리얼 쪽에서 한 멀티 드로우로 묶일 수 있는가입니다 — 버퍼 · SRV · 원소 수 · 텍스처가 같아야 하고, 머티리얼 CB 는
         *        셰이더가 그 슬롯을 걸 때만(@p bShaderBindsMaterialCb) 본다.
         * @details GPUScene 경로의 셰이더는 머티리얼을 구조버퍼(`g_SwMaterials`, t9)에서 인스턴스의 `_materialIndex` 로 읽어 CB 슬롯이 없다 —
         *          그런 셰이더에서 `_materialCb` 를 키에 넣으면 깊이순으로 섞인 투명 배치들이 그림에 영향 없는 값 때문에 하나씩 따로 그려진다
         *          (큐브 8000 · 도형 8 종: 배치 1795 개가 드로우 1206 회, 이 값을 빼면 3 회이고 화면은 같다).
         */
        static bool canShareMaterialBinding( const GpuMeshBatch& head, const GpuMeshBatch& other, bool bShaderBindsMaterialCb )
        {
            if ( other._materialBuffer != head._materialBuffer || other._materialSrv != head._materialSrv || other._materialCount != head._materialCount )
                return false;
            if ( bShaderBindsMaterialCb && other._materialCb != head._materialCb )
                return false;
            for ( uint32 texIndex = 0; texIndex < shaderslot::kMaterialTextureCount; ++texIndex )
            {
                if ( other._arrMaterialTexSrv[texIndex] != head._arrMaterialTexSrv[texIndex] )
                    return false;
            }
            return true;
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct GpuMaterialElementKey
     * @brief 머티리얼 데이터 원소 하나를 가리키는 키, 곧 (머티리얼, 인스턴스) 쌍입니다.
     * @details 인스턴스가 없으면 머티리얼 자신이 원소입니다. 인스턴스는 CB 값만 덮어쓰므로 부모 머티리얼과 함께 봐야 합니다.
     */
    // SW_OWNERSHIP_RAW_OK( _pMaterial, _pInstance ): 정체성 키다. 비교만 하고 **역참조하지 않는다**. 소유를 실으면 키가 수명을 붙들어,
    //                                               회수돼야 할 머티리얼이 스냅샷이 사는 동안 살아남는다(그것이 원소 표가 따로 있는 이유다).
    struct GpuMaterialElementKey
    {
        Material*         _pMaterial{ nullptr };
        MaterialInstance* _pInstance{ nullptr };
        /** @brief 두 포인터가 모두 같으면 true 를 반환합니다. */
        bool operator==( const GpuMaterialElementKey& other ) const
        {
            return _pMaterial == other._pMaterial && _pInstance == other._pInstance;
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct GpuMaterialElement
     * @brief 머티리얼 데이터 원소 하나입니다. (머티리얼, 인스턴스) 쌍의 **소유**입니다.
     * @details 키(`GpuMaterialElementKey`)는 정체성이라 생포인터이고, 원소는 렌더 스레드가 `getBuffer()` 로 읽으므로
     *          소유를 듭니다. 인스턴스가 없으면 머티리얼 자신이 원소입니다.
     */
    struct GpuMaterialElement
    {
        shared_ptr<Material>         _material;
        shared_ptr<MaterialInstance> _instance;
    };
} // namespace sw

namespace sw
{
    /// @brief GpuMaterialElementKey 의 해시입니다. 포인터 둘을 섞습니다.
    struct GpuMaterialElementKeyHash
    {
        /** @brief 키의 해시를 반환합니다. */
        size_t operator()( const GpuMaterialElementKey& key ) const
        {
            size_t hash = reinterpret_cast<size_t>( key._pMaterial ) * 1315423911u;
            hash ^= reinterpret_cast<size_t>( key._pInstance ) + HashUtil::kGoldenRatio32 + ( hash << 6 ) + ( hash >> 2 );
            return hash;
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct GpuShaderPermutation
     * @brief 머티리얼이 요구하는 셰이더 변형 하나(경로 + 정적 define)입니다.
     * @details 언리얼의 `FMaterialShaderMap` 이 있는 자리입니다. 같은 .hlsl 이라도 정적 스위치가 다르면 **다른 셰이더**라서
     *          다른 PSO 로 그려야 합니다. 주의: 셰이더 **경로**만 보면 유리 머티리얼처럼 always-define 을 가진 것이
     *          불투명 머티리얼과 한 배치로 접히고, 배치는 PSO 하나로 그리므로 그 정보가 그냥 버려집니다.
     *
     *          렌더 스레드는 씬을 못 보므로(Material* 을 따라갈 수 없습니다) 값으로 실어 나릅니다.
     */
    struct GpuShaderPermutation
    {
        /// @brief 머티리얼이 선언한 셰이더입니다. 패스가 자기 셰이더를 쓰는 경우(그림자 · 뎁스)에는 무시됩니다.
        string _shaderPath;
        /// @brief always-define + 정적 스위치 + 멀티컴파일 선택입니다. 그대로 PSO 의 셰이더 define 이 됩니다.
        vector<string> _listDefine;
        /// @brief (경로, define) 해시입니다. PSO 캐시 키이자 **배치 병합 키**입니다.
        uint64 _hash{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct GpuMaterialGroup
     * @brief 셰이더 타입(머티리얼 셰이더 경로)별 머티리얼 데이터 원소 목록입니다. CPU 스냅샷의 일부입니다.
     * @details 원소 순서가 곧 materialIndex 입니다. 같은 셰이더를 쓰는 머티리얼 · 인스턴스는 구조체 레이아웃이 같아 한 버퍼에 쌓입니다.
     */
    struct GpuMaterialGroup
    {
        string                     _shaderPath;
        vector<GpuMaterialElement> _listEntry;
        // 원소 인덱스 표(키 → 인덱스 · 마지막 사용 빌드 · 프리리스트 · 직전 조회)는 **빌더의 것**이다
        // (`GpuSceneBuilder::MaterialGroupState`). 스냅샷에는 RT 가 읽는 것만 싣는다. 표를 여기 두면
        // 프레임마다 패킷으로 복사된다(맵 하나 + 벡터 둘, 그룹마다).
    };
} // namespace sw

namespace sw
{
    /** @brief 인스턴스 배열에서 바뀐 구간 `[_start, _start + _count)` 입니다. */
    struct GpuInstanceRun
    {
        uint32 _start{ 0 };
        uint32 _count{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct GpuSkinPalette
     * @brief 스킨드 메시 하나의 팔레트 구간입니다. 행은 `GpuSceneSnapshot::_pListSkinPaletteRow` 의 float4 이고 본 하나가 셋입니다(행벡터 4x4 의 0 · 1 · 2 열).
     */
    // SW_OWNERSHIP_RAW_OK( _pMesh ): 정체성 키다. RT 의 모프 풀이 자기 구간과 짝짓는 데만 쓰고 역참조하지 않는다 — 메시 소유는 배치(`GpuMeshBatch::_mesh`)가 싣는다.
    struct GpuSkinPalette
    {
        const Mesh* _pMesh{ nullptr };
        uint32      _firstRow{ 0 };
        uint32      _boneCount{ 0 };
        uint32      _firstMorphWeight{ 0 }; ///< `GpuSceneSnapshot::_pListMorphWeight` 안의 이 메시 모프 가중치 시작
        uint32      _morphWeightCount{ 0 }; ///< 모프 가중치 수(0 이면 모프 없음)
    };
} // namespace sw

namespace sw
{
    /**
     * @struct GpuViewTransparentOrder
     * @brief 추가 뷰 하나의 투명 그리기 순서입니다. 주 뷰의 순서(꼬리 인스턴스 배치)는 그대로 두고 이 뷰가 다르게 그릴 것만 싣습니다.
     * @details GT 가 그 뷰의 눈 · 시선 축으로 꼬리를 다시 정렬해 만든다(`GpuSceneBuilder::buildViewTransparentOrders`). RT 는 순번 표를 정렬 디스패치에,
     *          꼬리 슬롯 표를 컬링이 없는 백엔드의 인스턴스 슬롯 스트림에, 배치 순서를 투명 패스 드로우 순서에 쓴다.
     */
    struct GpuViewTransparentOrder
    {
        shared_ptr<const vector<uint32>> _pListRank;      ///< 꼬리 인스턴스(꼬리 시작 기준)마다 이 뷰에서 그리는 순번(0 = 가장 먼저 = 가장 멀다)
        shared_ptr<const vector<uint32>> _pListTailSlot;  ///< 배치마다 안쪽을 이 뷰 순서로 다시 놓은 꼬리 인스턴스 번호(전역) — 꼬리 길이와 같다
        vector<uint32>                   _listBatchOrder; ///< 이 뷰에서 투명 배치를 그리는 순서(`_listTransparentBatch` 의 번호)
        uint64                           _viewId{ 0 };    ///< `RenderViewRequest::_viewId`
        uint32                           _tailBase{ 0 };  ///< 꼬리의 첫 인스턴스 번호(불투명 인스턴스 수)
    };
} // namespace sw

namespace sw
{
    /**
     * @struct GpuSceneSnapshot
     * @brief 게임 스레드가 만들고 렌더 패킷에 실어 렌더 스레드로 **옮기는 전부**입니다.
     *
     * @details 소유 규칙이 이 타입에 적혀 있습니다:
     *          - 렌더 스레드가 역참조하는 CPU 객체(메시 · 머티리얼 · 인스턴스)는 **shared_ptr 로 소유를 함께 싣습니다.**
     *            패킷이 살아 있는 동안(렌더 큐 깊이만큼) 게임 스레드가 놓아도 삽니다. 생포인터는 정렬 키 같은
     *            **정체성**에만 쓰고 스레드를 넘어 역참조하지 않습니다.
     *          - RT 가 upload() 에서 만드는 `GpuScene` 의 값(인스턴스 · 간접 인자 버퍼, 간접 개수, 컬 뷰)은 여기 없습니다.
     *            그래서 옮겨질 수 없고, "한쪽만 만드는 값을 스냅샷이 매 프레임 덮어쓴다" 는 버그가 타입으로 막힙니다.
     *            배치의 GPU 필드(`_vertexBuffer` · `_materialCb` 등)는 RT 가 받은 사본에 덧칠하는 값입니다.
     *          - 모듈(게임 DLL)이 만든 객체를 엔진이 마지막까지 들 수 있으므로, 실리는 객체는 Engine 의
     *            create() 팩토리로 만듭니다(제어 블록이 Engine.dll 에 삽니다).
     *          검사는 Scripts/lint/gate/CheckRenderOwnership.py 가 합니다.
     */
    struct GpuSceneSnapshot
    {
        /**
         * @brief 인스턴스 배열입니다. **값이 아니라 공유합니다.** RT 는 읽기만 하므로 프레임마다 복사할 이유가 없습니다.
         *
         * @details 값으로 두면 `exportCpuSnapshot` 이 매 프레임 통째로 복사합니다. 아무것도 움직이지 않는 정적 씬에서도
         *          인스턴스당 55 ns(엔티티 8000 개면 441 us)이고, 그것이 정적 씬에서 **게임 스레드의 유일한 실제 작업**이 됩니다.
         *
         *          배치 · 머티리얼 목록은 그대로 값입니다. **RT 가 GPU 핸들을 거기에 덧칠하므로**
         *          공유하면 안 됩니다(`GpuMeshBatch::_firstVertex` · `_materialCb` 주석 참고). 대신 개수가
         *          적어 복사가 쌉니다. 큰 것만 공유하고 작은 것은 복사하는 것이 이 타입의 규칙입니다.
         */
        shared_ptr<const vector<GpuInstance>> _pListInstance;
        vector<GpuMeshBatch>                  _listOpaqueBatch;
        vector<GpuMeshBatch>                  _listTransparentBatch;
        vector<GpuMeshBatch>                  _listAllBatch;      ///< 불투명 다음 투명. 간접 슬롯과 일치
        vector<GpuMaterialGroup>              _listMaterialGroup; ///< 셰이더 타입별 머티리얼 원소
        /**
         * @brief 퍼뮤테이션 목록입니다. 배치의 `_shaderPermutation` 이 가리킵니다. **불변 목록을 공유합니다.**
         * @details 문자열 경로와 define 목록이 든 값이라 프레임마다 복사하면 그 문자열들이 모두 힙 할당입니다(프레임당 ~30 회).
         *          빌더는 새 퍼뮤테이션이 나타날 때만 목록을 새로 만들어 바꿔 끼웁니다(copy-on-write). RT 는 읽기만 합니다.
         */
        shared_ptr<const vector<GpuShaderPermutation>> _pListShaderPermutation;
        /// @brief 스킨드 메시마다의 팔레트 구간입니다(작아서 복사한다).
        vector<GpuSkinPalette> _listSkinPalette;
        /**
         * @brief 이번 프레임의 스킨 팔레트 행 전부입니다(본 하나 = float4 셋). **공유합니다** — RT 는 읽기만 하고, GT 는 프레임마다 새 배열을 만듭니다.
         * @details 팔레트는 애니메이션 시스템이 틱 뒤에 만들고 빌더가 여기로 옮깁니다. RT 가 모프 풀의 스킨 구간 순서로 다시 올립니다(`GpuMeshMorphPool::uploadSkinPalettes`).
         */
        shared_ptr<const vector<float4>> _pListSkinPaletteRow;
        /**
         * @brief 이번 프레임의 모프 타깃 가중치 전부입니다(스킨드 메시마다 `GpuSkinPalette::_firstMorphWeight` 부터). **공유합니다** — 팔레트 행과 같은 규칙입니다.
         * @details 유닛(`SkeletalMeshComponent::getMorphWeights`)이 내고, RT 가 팔레트 버퍼 뒤에 실어 스키닝 컴퓨트가 레스트에 차이를 더합니다.
         */
        shared_ptr<const vector<float32>> _pListMorphWeight;
        /// @brief GPU 회전을 요청한 인스턴스 수입니다(0 이면 애니메이션 디스패치를 건너뜁니다).
        uint32 _spinInstanceCount{ 0 };
        /**
         * @brief 정점 애니메이션(VAT) 시계(초)입니다. 게임 스레드의 군중 시계(`AnimationCrowd::getClock`)를 그대로 옮겨, 인스턴스 시각 오프셋과 더하면 CPU 가
         *        본 클립 시각과 같습니다. 패스마다 PassCB `g_SwVertexAnimationTime` 으로 갑니다.
         */
        float32 _vertexAnimationTime{ 0.0f };
        /// @brief 마지막 buildFromScene 이 내용을 바꿨는지입니다. RT 는 0 이면 인스턴스 재업로드를 생략합니다.
        uint8 _bCpuDirty{ SW_TRUE };
        /**
         * @brief 1 이면 인스턴스 배열 **전체**가 바뀌었고, 0 이면 `_listDirtyInstanceRun` 만 바뀌었습니다.
         * @details 전체 재구축은 1, 물체만 움직인 프레임은 0 입니다. 버퍼를 새로 만든 프레임도 받는 쪽이
         *          전체로 취급해야 합니다. 새 버퍼에는 아직 아무것도 없습니다.
         */
        uint8 _bAllInstancesDirty{ SW_TRUE };
        /**
         * @brief 바뀐 인스턴스 구간들입니다(`_bAllInstancesDirty` 가 0 일 때만 뜻이 있습니다).
         * @details 하나만 움직여도 인스턴스 버퍼 **전체**를 다시 올리면 8000 개 중 10 개만 움직여도 800 개를 움직일 때와
         *          같은 100 us 가 듭니다. 그래서 바뀐 구간만 올립니다. 구간이 너무 잘게 흩어지면
         *          작은 업로드가 도리어 비싸므로, 빌더가 개수 상한을 넘기면 전체로 돌립니다.
         */
        vector<GpuInstanceRun> _listDirtyInstanceRun;
        /// @brief 이번 프레임에 그리는 추가 뷰마다의 투명 순서입니다. 뷰가 투명을 안 보거나 꼬리가 없으면 비어 있습니다(그 뷰는 주 순서로 그린다).
        vector<GpuViewTransparentOrder> _listViewTransparentOrder;

        /** @brief 인스턴스 배열을 반환합니다. 아직 발행 전이면 빈 배열입니다. */
        const vector<GpuInstance>& getInstances() const
        {
            static const vector<GpuInstance> s_listEmpty{};
            return ( _pListInstance != nullptr ) ? *_pListInstance : s_listEmpty;
        }
    };
} // namespace sw
