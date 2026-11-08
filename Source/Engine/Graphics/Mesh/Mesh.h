/**
 * @file Mesh.h
 * @brief CPU 메시 데이터와 GPU 정점 버퍼 업로드를 정의합니다. 씬 지오메트리용이며 RHI 데모용이 아닙니다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/IdentityToken.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Graphics/Mesh/MeshBuildVertex.h"
#include "Engine/Graphics/RHI/RHIDeviceBuffer.h"
#include "Engine/Graphics/RHI/RHIRenderResource.h"
#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    struct MeshVertexAnimation;

    class IRHIDevice;

    /**
     * @struct MeshSkinVertex
     * @brief 스킨드 메시 정점 하나의 본 영향(본 번호 4개와 가중치 4개)입니다. 가중치 합은 1이고, 쓰지 않는 슬롯의 가중치는 0입니다.
     * @details 본 번호는 메시가 따르는 스켈레톤의 본 순서다(`Skeleton`). `Mesh` 가 정점과 같은 순서, 같은 길이로 가지고 있다.
     */
    struct MeshSkinVertex
    {
        uint16  _arrJoint[4]{ 0, 0, 0, 0 };
        float32 _arrWeight[4]{ 1.0f, 0.0f, 0.0f, 0.0f };
    };

    /**
     * @struct MeshSkinBinding
     * @brief 메시가 스켈레톤에 묶인 방식입니다 — 본마다 역 바인드(스킨 팔레트 = 역 바인드 * 모델 공간)와, 메시가 저작된 본 배치의 지문입니다.
     * @details 역 바인드는 메시의 값이다(언리얼 Skeletal Mesh 의 Ref Skeleton · 유니티 `Mesh.bindposes`). 그래서 스켈레톤 한 벌을 비율이 다른 메시가 나눠 쓰고,
     *          메시 한 벌을 비율이 다른 스켈레톤 변형에 그릴 수 있다. 본 수는 이 목록의 길이이고, 스켈레톤보다 짧으면 앞쪽 본만 쓴다(변형이 뒤에 붙인 본).
     */
    struct SW_API MeshSkinBinding
    {
        /** @brief 본 배치를 따지지 않는 메시입니다(강체 사본 · 파괴 조각처럼 코드가 지은 것) — 본 수만 맞으면 어느 스켈레톤에도 걸린다. */
        static constexpr uint64 kAnyLayout = 0;
        /** @brief 두 메시의 역 바인드를 같다고 볼 허용 오차입니다(임포트가 스킨끼리 견주는 값과 같다). */
        static constexpr float32 kInverseBindTolerance = 1e-3f;

        vector<float4x4> _listInverseBind;
        /** @brief 메시가 저작된 스켈레톤의 앞 `getBoneCount` 개 본 배치 지문입니다(`Skeleton::getHierarchyHash`). */
        uint64 _layoutHash{ kAnyLayout };

        /** @brief 본 수입니다. 0 이면 스킨이 없습니다. */
        uint32 getBoneCount() const { return static_cast<uint32>( _listInverseBind.size() ); }
        /** @brief 단위 역 바인드 @p boneCount 개짜리 묶음입니다(정점이 이미 각 본의 공간에 있는 메시). */
        static MeshSkinBinding makeIdentity( uint32 boneCount ) { return MeshSkinBinding{ vector<float4x4>( boneCount, float4x4::Identity ), kAnyLayout }; }
        /** @brief 한 팔레트로 함께 그릴 수 있으면 true 입니다 — 본 수와 역 바인드가 허용 오차 안이고 본 배치가 같습니다(`kAnyLayout` 은 어느 배치와도 맞습니다). */
        bool matches( const MeshSkinBinding& other ) const;
        /**
         * @brief @p listSkin 정점을 이 묶음의 팔레트로 그려도 같은 그림이면 true 입니다 — 본 수와 배치가 같고 정점이 쓰는 본의 역 바인드가 허용 오차 안입니다.
         * @details 쓰지 않는 본의 차이는 그림에 닿지 않는다. 부품 파일마다 저작 때 안 쓰는 본의 자세가 다른 캐릭터 팩(머리카락과 몸)이 있다.
         */
        bool matchesForVertices( const MeshSkinBinding& other, const vector<MeshSkinVertex>& listSkin ) const;
        /** @brief 본 수와 배치가 같으면 true 입니다. 같으면 정점을 옮겨(`makeRebindMatrices`) 한 팔레트로 그릴 수 있습니다. */
        bool hasSameLayout( const MeshSkinBinding& other ) const;
        /**
         * @brief 이 묶음의 정점을 @p target 의 바인드 자세로 옮기는 본마다의 행렬입니다. 본 수가 같아야 합니다.
         * @details 언리얼의 스켈레탈 메시 병합이 기준 자세를 맞추는 일과 같다. 본 하나에만 묶인 정점은 어느 자세에서든 정확하고, 여러 본에 걸친 정점은 가중 평균이라 자세가 바인드에서 멀수록 근사다.
         */
        [[nodiscard]] vector<float4x4> makeRebindMatrices( const MeshSkinBinding& target ) const;
        /** @brief @p vertex 의 위치와 노멀을 @p listRebind(`makeRebindMatrices`)로 옮깁니다. */
        static void rebindVertex( const vector<float4x4>& listRebind, const MeshSkinVertex& skin, RHIVertex& inoutVertex );

    private:
        /** @brief 본 하나의 역 바인드가 허용 오차 안이면 true 입니다. 본 수가 같아야 합니다. */
        bool hasSameBone( const MeshSkinBinding& other, size_t boneIndex ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct MeshMorphDelta
     * @brief 모프 타깃 하나가 정점 하나를 옮기는 양(위치와 노멀 차이)입니다. 가중치가 1이면 레스트에 차이를 더한 값이 됩니다.
     */
    struct MeshMorphDelta
    {
        uint32 _vertexIndex{ 0 };
        float3 _position{};
        float3 _normal{};
    };
} // namespace sw

namespace sw
{
    /**
     * @enum MeshPersonVisibility
     * @brief 구간이 1인칭 · 3인칭 시점에서 보이는지입니다(VRM firstPerson 의 meshAnnotations). 값은 `.mesh` 의 `SECT` 덩어리에 그대로 들어갑니다.
     */
    enum class MeshPersonVisibility : uint8
    {
        Both            = 0, ///< 두 시점 모두 보인다
        ThirdPersonOnly = 1, ///< 3인칭에서만 보인다(1인칭 카메라에 머리 · 머리카락이 걸리지 않게)
        FirstPersonOnly = 2, ///< 1인칭에서만 보인다
        Count
    };

    /**
     * @struct MeshSection
     * @brief 메시의 구간 하나입니다 — 인덱스 범위(삼각형 단위라 3 의 배수)와 이름, 시점 가시성입니다.
     * @details 머티리얼은 구간마다 다르다. 구간을 그리는 컴포넌트(`MeshComponent::_sectionName`)가 머티리얼을 갖고, 같은 메시 객체를 나눠 써서 스킨 · 모프 결과도
     *          한 벌이다(언리얼 Skeletal Mesh 의 Section, 유니티 Mesh 의 SubMesh 에 해당한다). 구간이 없는 메시는 전체가 구간 하나다.
     */
    struct MeshSection
    {
        hashed_string        _name;
        uint32               _firstIndex{ 0 };
        uint32               _indexCount{ 0 };
        MeshPersonVisibility _personVisibility{ MeshPersonVisibility::Both };
        hashed_string        _sourceMaterial{}; ///< 원본 파일의 날 머티리얼 이름(임포트가 적는다 — 다시 임포트할 때 구간 이름을 지키는 열쇠, 편집 창의 별칭 대응 표시)
        hashed_string        _sourceNode{};     ///< 원본 파일에서 이 구간의 첫 삼각형이 온 메시(노드) 이름

        /** @brief 날 이름(머티리얼 · 노드)이 있으면 true 입니다(옛 임포트는 없다). */
        bool hasSourceNames() const { return _sourceMaterial.empty() == false; }
    };
} // namespace sw

namespace sw
{
    /** @brief LOD 단계 안에서 구간 하나의 인덱스 범위입니다(구간 순서는 LOD 0 의 `Mesh::getSections` 와 같다 — 언리얼 스켈레탈 메시의 LOD 마다 섹션 목록). */
    struct MeshLODSectionRange
    {
        uint32 _firstIndex{ 0 };
        uint32 _indexCount{ 0 };
    };

    /**
     * @struct MeshLODLevel
     * @brief 메시의 낮은 상세 단계(LOD 1 이상) 하나의 저작 형태입니다 — 그 단계의 고유 정점 · 인덱스(번호는 이 단계 정점 안)와, 이 단계로 넘어가는 화면 크기 임계값입니다.
     * @details 화면 크기는 경계 구의 화면 높이 비율이다(`LODView::computeScreenSize` 와 같은 값). 화면 크기가 임계값보다 작아지면 이 단계 이상을 그린다.
     *          모델 임포트가 단순화로 만들고(`ModelImporter`, 비율 · 임계값은 임포트 규칙 데이터) `.mesh` 의 `LODS` 덩어리에 싣는다.
     */
    struct MeshLODLevel
    {
        vector<RHIVertex> _listVertex;
        vector<uint32>    _listIndex;
        float32           _screenSize{ 0.0f };
        /// @brief 스킨드 메시의 단계면 정점마다의 본 영향(`_listVertex` 와 길이가 같다). 스킨 없는 메시의 단계는 비어 있다. `.mesh` 의 `LSKN` 덩어리에 싣는다.
        vector<MeshSkinVertex> _listSkinVertex{};
        /// @brief 구간 있는 메시의 단계면 구간마다 이 단계 인덱스(`_listIndex`) 안의 범위(LOD 0 구간 순서). 구간 없는 메시는 비어 있다.
        vector<MeshLODSectionRange> _listSectionRange{};
    };
} // namespace sw

namespace sw
{
    /**
     * @struct MeshLOD
     * @brief 그릴 흐름 안에서 LOD 단계 하나의 범위입니다. 정점 흐름은 LOD 0(`Mesh::getVertices`) 뒤에 낮은 단계 정점(`Mesh::getLODVertices`)이,
     *        인덱스 흐름은 LOD 0(`Mesh::getIndices`) 뒤에 낮은 단계 인덱스(`Mesh::getLODIndices` — 값은 정점 흐름 번호)가 이어진 것입니다.
     */
    struct MeshLOD
    {
        uint32                      _firstIndex{ 0 };
        uint32                      _indexCount{ 0 };
        uint32                      _firstVertex{ 0 }; ///< 이 단계 정점의 정점 흐름 안 시작(스키닝 · 미리보기가 센다)
        uint32                      _vertexCount{ 0 };
        float32                     _screenSize{ 0.0f }; ///< 이 단계로 넘어가는 화면 크기 임계값(`MeshLODLevel::_screenSize`)
        vector<MeshLODSectionRange> _listSectionRange{}; ///< 구간마다 그릴 인덱스 흐름 안의 범위(LOD 0 구간 순서). 구간 없는 메시는 비어 있다
    };
} // namespace sw

namespace sw
{
    /**
     * @struct MeshMorphTarget
     * @brief 이름이 붙은 모프 타깃(블렌드 셰이프)입니다. 움직이는 정점만 담습니다(얼굴 타깃은 메시의 작은 일부만 옮깁니다).
     */
    struct MeshMorphTarget
    {
        hashed_string          _name;
        vector<MeshMorphDelta> _listDelta;
    };
} // namespace sw

namespace sw
{
    /**
     * @class Mesh
     * @brief 인덱스 삼각형 메시입니다 — 고유 정점(위치, 노멀, UV, 색)과 삼각형마다 정점 번호 셋(uint32)입니다. initRHI() 로 올린 GPU 정점 버퍼를 소유합니다.
     * @details 언리얼 · 유니티 · Godot 과 같이 모든 메시가 인덱스를 가진다(색인 없는 모드는 없다). 구간 · LOD 는 인덱스 범위이고, 스킨 · 모프 · VAT 는 고유 정점 번호다.
     */
    class SW_API Mesh final : public RHIRenderResource
    {
    public:
        /**
         * @brief 생성 키입니다. create() 만 만들 수 있습니다.
         * @details 생성자가 이 키를 요구하므로 `make_shared<Mesh>()` 도 스택의 `Mesh x;` 도 **컴파일되지 않는다.**
         *          모든 Mesh 가 Engine 안에서 shared_ptr 로 만들어진다는 것을 컴파일러가 보장한다. 그래서 렌더 패킷이
         *          소유권을 가질 수 있고, 제어 블록이 모듈 DLL 에 있는 일이 없다. 린트가 아니라 타입이 지킨다.
         */
        struct CreateKey
        {
        private:
            CreateKey() = default;
            friend class Mesh;
        };
        /** @brief create() 전용 생성자입니다. 빈 메시를 만듭니다. */
        explicit Mesh( CreateKey ) noexcept {}
        /**
         * @brief 정점 버퍼를 해제합니다. 디바이스가 살아 있으면 반환하고, 이미 없으면 핸들만 버립니다.
         * @note 디바이스가 먼저 죽었다면 통보(`releaseRHI`, `clearRHI`)가 먼저 와서 핸들을 비워 두므로,
         *       소멸 순서와 상관없이 죽은 디바이스로 destroy 하지 않습니다.
         */
        ~Mesh() override;

        /** @brief (RHIRenderResource) 살아 있는 디바이스에 정점 버퍼를 반환합니다. */
        void releaseRHI( IRHIDevice* pDevice ) override;
        /** @brief (RHIRenderResource) 디바이스가 이미 없을 때 부릅니다. 핸들만 버립니다. */
        void clearRHI( IRHIDevice* pDevice ) override;

        /** @brief 복사를 금지합니다. */
        Mesh( const Mesh& ) = delete;
        /** @brief 대입을 금지합니다. */
        Mesh& operator=( const Mesh& ) = delete;

        /**
         * @brief 빈 메시를 Engine.dll 안에서 shared_ptr 로 만듭니다.
         * @details 도형을 만드는 것은 이 클래스의 일이 아니다. `MeshUtil` 이 이 함수로 메시를 만들어 정점을 채운다.
         *          Mesh 가 책임지는 것은 **정점 버퍼와 그 수명**뿐이다.
         */
        static shared_ptr<Mesh> create();
        /**
         * @brief 스킨 없는 모프 메시(블렌드 셰이프만 있는 소품 · 얼굴)를 본 하나에 가중치 1 로 묶은 스킨 원본을 돌려줍니다(정점 · 구간 · LOD · 모프 타깃 복사).
         * @details 모프 가중치는 GPU 스키닝 컴퓨트가 스키닝 앞에 건다. 스킨이 없으면 그 길에 들지 못하므로 고정 본 하나를 준다(임포트가 몸에 합칠 때와 같은 규칙).
         *          `SkeletalMeshComponent` 가 이런 메시를 받으면 이 함수로 원본을 바꿔 그린다 — 스켈레톤이 없으면 암묵 스켈레톤(본 하나)이다.
         *          같은 원본(내용 번호)의 유닛은 한 벌을 나눈다 — 스킨 결과는 칸마다라 원본은 하나면 되고, 유닛마다 사본이면 정점 풀 · 배치가 유닛마다 갈린다(StructureRefactor 11).
         *          약한 참조라 그리는 유닛이 없으면 사라지고, 핫 리로드한 원본은 내용 번호가 바뀌어 새 것을 받는다. 어느 스레드에서 불러도 된다.
         */
        static shared_ptr<Mesh> acquireRigidSkinSource( const Mesh& source );
        /** @brief 강체 스킨 원본 한 벌을 새로 짓습니다(`acquireRigidSkinSource` 의 캐시가 없을 때 부른다). */
        static shared_ptr<Mesh> createRigidSkinSource( const Mesh& source );

        /**
         * @brief 고유 정점과 인덱스를 설정합니다. 탄젠트는 받은 값을 버리고 `computeTangents` 로 다시 구합니다.
         * @details 인덱스 수가 3 의 배수이고 번호가 정점 수보다 작아야 한다 — 아니면 오류를 남기고 메시를 비운 채 false 다.
         */
        [[nodiscard]] bool setGeometry( vector<RHIVertex>&& listVertex, vector<uint32>&& listIndex );
        /** @brief 탄젠트가 이미 들어 있는 고유 정점과 인덱스를 그대로 설정합니다(`.mesh` 를 읽은 값, 다른 메시의 사본). 검사는 `setGeometry` 와 같습니다. */
        [[nodiscard]] bool setGeometryWithTangents( vector<RHIVertex>&& listVertex, vector<uint32>&& listIndex );
        /**
         * @brief 삼각형 목록 정점(정점 셋이 삼각형 하나, 공유 정점 없음)을 설정합니다 — 인덱스는 0..n-1 입니다. 탄젠트는 `computeTangents` 로 다시 구합니다.
         * @details 탄젠트는 위치 · 노멀 · UV 에서 나오는 값이라(경계 반지름처럼) 메시가 정한다. 정점을 만드는 쪽(도형 · 지형 · 물 · 잘린 조각 · 테스트)은
         *          탄젠트를 채우지 않아도 되고, 채우지 않은 정점이 0 탄젠트로 그려지는 일이 없다. 이미 구한 탄젠트(`.mesh`)는 `setVerticesWithTangents` 로 넣는다.
         */
        void setVertices( const vector<RHIVertex>& listVertex );
        void setVertices( vector<RHIVertex>&& listVertex );
        /** @brief 게임 코드의 정점(`MeshBuildVertex`)으로 `setVertices` 와 같이 설정합니다 — GPU 정점 칸으로 옮기는 일은 여기 하나다. */
        void setBuildVertices( const vector<MeshBuildVertex>& listVertex );
        /**
         * @brief 탄젠트가 이미 들어 있는 정점 배열을 그대로 설정합니다(`.mesh` 를 읽은 값, 다른 메시의 사본).
         * @details 임포트가 `computeTangents` 로 구해 파일에 실은 값이다. 런타임이 로드마다 다시 구하지 않는다(언리얼 · 유니티가 임포트 때 MikkTSpace 를 베이크하는 것과 같다).
         */
        void setVerticesWithTangents( vector<RHIVertex>&& listVertex );
        /**
         * @brief 인덱스 삼각형의 탄젠트(`RHIVertex::_arrTangent`)를 위치 · 노멀 · UV 로 구합니다. 기준은 이 함수 하나입니다(런타임 · 임포트 · `.mesh` 쓰기).
         * @details MikkTSpace 와 같은 규약이다 — 면마다 UV 기울기에서 탄젠트 · 바이탄젠트를 구해 모서리 각도로 가중하고, 위치 · 노멀 · UV 가 비트까지 같은 정점끼리
         *          (손잡이가 같은 면끼리만) 모아 더한 뒤 노멀에 직교화한다. W 는 바이탄젠트 부호(cross( N, T ) 와 면의 바이탄젠트가 같은 쪽이면 +1)다.
         *          UV 가 퇴화해(면적 0) 기울기가 없는 정점은 노멀에 수직이고 +X 에 가장 가까운 방향을 받는다(노멀이 X 축이면 +Z). 셰이더의 노멀 맵은 평평한 맵에서 노멀을 그대로 낸다.
         */
        static void computeTangents( vector<RHIVertex>& inoutListVertex, const vector<uint32>& listIndex );
        /** @brief 삼각형 목록 정점(인덱스 0..n-1)의 탄젠트를 구합니다. */
        static void computeTangents( vector<RHIVertex>& inoutListVertex );
        /** @brief 삼각형 목록(정점 셋이 삼각형 하나)의 인덱스 0..@p vertexCount-1 입니다. */
        static vector<uint32> makeTriangleListIndices( uint32 vertexCount );
        /**
         * @brief 삼각형 목록에서 모든 칸(위치 · 노멀 · UV · 색 · 탄젠트)이 비트까지 같은 정점을 하나로 합쳐 고유 정점과 인덱스로 만듭니다. 마지막 덜 찬 삼각형은 버립니다.
         * @details 그림은 비트까지 같다 — 합치는 정점은 셰이더가 받는 값이 같다. 고유 정점은 처음 쓰인 순서다(정점 읽기가 삼각형 순서를 따른다).
         *          탄젠트를 먼저 구한 정점을 넘긴다(구하기 전에 합치면 이웃 면의 탄젠트가 합쳐진 정점 하나에 모여 값이 달라진다 — 결과는 같지만 순서를 고정한다).
         */
        static void weldTriangleList( const vector<RHIVertex>& listTriangleVertex, vector<RHIVertex>& outListVertex, vector<uint32>& outListIndex );
        /**
         * @brief 삼각형 목록 정점을 탄젠트를 구한 뒤 합쳐(`weldTriangleList`) 설정합니다. 정점마다의 스킨 · 모프를 뒤따라 붙이지 않는 생산자(도형 · 지형 · 물)가 씁니다.
         * @details 정점 번호가 바뀌므로, 삼각형 목록 순서로 스킨 · 모프를 만든 생산자는 `setVertices` 를 쓴다.
         */
        void setTriangleListWelded( vector<RHIVertex>&& listTriangleVertex );
        /** @brief CPU 고유 정점 배열입니다. GPU 업로드 뒤에도 원본은 여기 남습니다. */
        const vector<RHIVertex>& getVertices() const { return _listVertex; }
        /** @brief 삼각형마다 정점 번호 셋입니다(LOD 0). */
        const vector<uint32>& getIndices() const { return _listIndex; }
        /** @brief LOD 0 의 인덱스 수입니다(삼각형 수 × 3). */
        uint32 getIndexCount() const { return static_cast<uint32>( _listIndex.size() ); }

        /**
         * @brief 이 메시의 정점을 GPU 가 변형해도 되는지 표시합니다(기본은 꺼짐).
         * @details 유니티의 `Mesh.vertexBufferTarget |= Raw` 옵트인에 해당한다. 켠 메시만 모프 풀에
         *          들어간다. 모두 넣으면 풀이 쓸데없이 커지고, 예산을 넘긴 메시는 어차피 레스트로 그려진다.
         * @note **GPU 쪽 변형은 CPU 사본(`getVertices`)에 반영되지 않는다.** 유니티 문서도 같은 주의를 준다.
         *       CPU 는 레스트 포즈만 안다(피킹과 바운드는 그 값을 본다).
         */
        void setGPUMorphEnabled( bool bEnabled ) { _bGPUMorph = bEnabled ? SW_TRUE : SW_FALSE; }
        /** @brief GPU 모프를 요청했는지 반환합니다. */
        bool isGPUMorphEnabled() const { return _bGPUMorph != SW_FALSE; }

        /**
         * @brief 프레임마다 새로 만드는 메시(천 같은 시뮬레이션 결과)라고 표시합니다(기본은 꺼짐). 내용 번호는 바뀌지 않습니다.
         * @details 동적 메시도 씬 정점 풀(`GPUMeshVertexPool`)에 든다 — 풀은 바뀐 메시를 "나감 + 들어옴" 으로 보고 그 구간만 올리고, 구간 반납 지연이
         *          앞 프레임이 읽는 자리를 지킨다. 표시가 하는 일은 게임 스레드가 이 메시를 업로드 큐(`GPUUploadQueue`)에 넣지 않게 하는 것이다 — 자기 정점 버퍼를
         *          미리 만들면 프레임마다 만들고 버린다(풀을 끈 구성에서만 렌더 스레드가 그 프레임에 만든다). 정점은 제자리에서 바꾸지 않는다 — 렌더 스냅샷이 옛 메시를
         *          쥐고 있으므로 프레임마다 `create` 로 새 메시를 만들어 컴포넌트에 건다(`ClothComponent`).
         */
        void setDynamic( bool bDynamic ) { _bDynamic = bDynamic ? SW_TRUE : SW_FALSE; }
        /** @brief 동적 메시로 표시했는지 반환합니다. */
        bool isDynamic() const { return _bDynamic != SW_FALSE; }

        /**
         * @brief GPU 정점 형식(속성 스트림)을 정합니다. 기본은 `Packed`(노멀 · 탄젠트 snorm8). GPU 에 올리기 전에 정한다.
         * @details 노멀 · 탄젠트 칸에 단위 벡터가 아닌 데이터를 싣는 메시(지형 형태 변환 — `TerrainMeshBuilder`)는 `Full` 이어야 한다. 바꾸면 내용 번호를 새로 받아
         *          정점 풀이 다시 올린다. 배치의 머티리얼 PSO 가 이 형식으로 지어진다(언리얼에서 메시 종류가 vertex factory 를 고르는 자리).
         */
        void setVertexFormat( RHIMeshVertexFormat format );
        /** @brief GPU 정점 형식을 반환합니다. */
        RHIMeshVertexFormat getVertexFormat() const { return _vertexFormat; }
        /** @brief GPU 정점 버퍼에서 속성 구간이 시작하는 바이트입니다(위치 구간 뒤 — `RHIMeshVertexStreams`). 슬롯 2 를 여기서 건다. */
        uint32 getAttributeStreamOffset() const;

        /**
         * @brief 모프 컴퓨트가 기본 물결 대신 실행할 변형 프로그램(float4 목록)을 넣습니다. 빈 목록이면 프로그램을 뗍니다. 내용 번호는 바뀌지 않습니다.
         * @details 메시 모디파이어 미리보기(`GF_EditorPlugin_MeshModifiers`)가 쓴다 — 모양은 `mesh_modifier.hlsli` 머리 주석이 기준이다. GPU 모프(`setGPUMorphEnabled`)가 켜진 메시에만 걸린다.
         *          게임 스레드가 아무 때나 바꾸고 렌더 스레드는 프레임마다 사본을 잡아 읽으므로(잠금) 정점 버퍼를 다시 만들지 않고 매개변수만 바꿀 수 있다.
         *          목록은 엔진 안에서 복사해 든다 — 넘긴 쪽(확장 모듈)이 내려가도 소멸 코드가 엔진에 남는다.
         */
        void setGPUDeformProgram( const vector<float4>& listProgram );
        /** @brief 변형 프로그램의 사본(공유)입니다. 없으면 nullptr 입니다. 렌더 스레드가 프레임마다 부릅니다. */
        shared_ptr<const vector<float4>> getGPUDeformProgram() const;
        /** @brief 변형 프로그램을 바꿀 때마다 오르는 번호입니다(0 은 한 번도 넣지 않음). 모프 풀이 다시 올릴지 이 값으로 정합니다. */
        uint64 getGPUDeformProgramGeneration() const { return _deformProgramGeneration.load( std::memory_order_acquire ); }

        /**
         * @brief 스킨(정점마다의 본 영향과 역 바인드)을 설정합니다. 길이는 정점 수와 같아야 하고 역 바인드가 있어야 하며, 아니면 스킨을 지웁니다. 내용 번호가 바뀝니다.
         * @details 스킨이 있는 메시는 GPU 스키닝 풀(모프 풀의 스킨 구간)에 들어가 정점 셰이더가 변형된 정점을 읽는다. 팔레트(본 행렬)는
         *          메시가 아니라 그것을 그리는 컴포넌트가 프레임마다 낸다(`SkeletalMeshComponent`). 그래서 포즈가 다른 캐릭터는 메시 객체를 따로 둔다.
         * @param skinBinding 본마다 역 바인드(길이가 팔레트 길이)와 본 배치 지문입니다.
         */
        void setSkin( vector<MeshSkinVertex> listSkinVertex, MeshSkinBinding skinBinding );
        /** @brief 정점마다의 본 영향입니다. 스킨이 없으면 비어 있습니다. */
        const vector<MeshSkinVertex>& getSkinVertices() const { return _listSkinVertex; }
        /** @brief 스킨이 있으면 true 입니다. */
        bool hasSkin() const { return getSkinBoneCount() > 0 && _listSkinVertex.size() == _listVertex.size(); }
        /** @brief 메시가 쓰는 본 수(팔레트 길이)입니다. 스킨이 없으면 0 입니다. */
        uint32 getSkinBoneCount() const { return _skinBinding.getBoneCount(); }
        /** @brief 역 바인드와 본 배치 지문입니다. 스킨이 없으면 비어 있습니다. */
        const MeshSkinBinding& getSkinBinding() const { return _skinBinding; }
        /**
         * @brief 모프 타깃(블렌드 셰이프)을 설정합니다. 정점 번호가 범위 밖인 차이는 버립니다. 내용 번호가 바뀝니다.
         * @details 타깃은 GPU 스키닝 컴퓨트가 스키닝 앞에 가중치만큼 더한다(모프 풀의 원본 데이터이며 사본은 공유한다). 가중치는 메시가 아니라 그리는 유닛이 낸다
         *          (`SkeletalMeshComponent::setMorphWeight`). 그래서 모프 타깃이 있는 메시는 스킨 구간으로 간다(스킨이 없으면 임포트가 고정 본 하나를 준다).
         */
        void setMorphTargets( vector<MeshMorphTarget> listTarget );
        /**
         * @brief 구간을 설정합니다. 구간은 인덱스 범위 안이고 3 의 배수이며 이름이 겹치지 않아야 합니다 — 아니면 오류를 남기고 구간을 비운 채 false 입니다.
         *        인덱스가 0 인 구간은 이름은 있되 그릴 것이 없는 구간입니다(외형 조립이 덮인 삼각형을 모두 뺀 구간 — 그 구간 컴포넌트는 아무것도 그리지 않는다).
         * @details 내용 번호는 바꾸지 않는다(정점이 그대로라 정점 · 모프 풀을 다시 올릴 이유가 없다). 그리는 범위는 배치 키라 다음 수집이 다시 나눈다.
         */
        [[nodiscard]] bool setSections( vector<MeshSection> listSection );
        /** @brief 구간들입니다. 비면 구간이 없는 메시(전체가 하나)입니다. */
        const vector<MeshSection>& getSections() const { return _listSection; }
        /**
         * @brief 낮은 상세 단계(LOD 1 이상)를 설정합니다. 정점은 탄젠트가 든 값이어야 합니다(`.mesh` 를 읽은 값 — 쓰기가 단계마다 `computeTangents` 로 구했다).
         * @details 단계마다 정점이 있고 인덱스 수가 0 이 아닌 3 의 배수이며 번호가 그 단계 정점 안이고, 임계값은 (0, 1) 안에서 단계가 낮아질수록 작아야 한다. 정점 애니메이션이 있는 메시는
         *          LOD 를 받지 않는다. 구간 있는 메시는 단계마다 구간 범위(`MeshLODLevel::_listSectionRange`, 구간 순서 · 수가 LOD 0 과 같다)를 받는다 — **구간을 먼저 설정**한다.
         *          모프 타깃이 있는 메시도 받지만 낮은 단계 정점은 모프 차이가 없다(먼 캐릭터의 표정은 보이지 않는다 — 언리얼도 낮은 LOD 의 모프 타깃을 덜어 낸다). 스킨드 메시는 받는다 — 단계마다 스킨 정점(`MeshLODLevel::_listSkinVertex`)이 정점과 같은 길이로 오고 뼈 번호가 `getSkinBoneCount` 안이어야 하며,
         *          스킨 없는 메시의 단계는 스킨이 없어야 한다(그래서 **스킨을 먼저 설정**한다 — `setSkin` 은 LOD 를 뗀다). GPU 스키닝은 LOD 0 뒤의 낮은 단계 정점도 변형한다.
         *          어기면 오류를 남기고 LOD 를 비운 채 false 입니다. 빈 목록은 LOD 를 뗀다.
         *          CPU 정점(`getVertices`)은 LOD 0 그대로다 — 물리 · 내비 · 피킹은 LOD 를 보지 않는다. 내용 번호가 바뀐다(정점 풀이 다시 올린다).
         */
        [[nodiscard]] bool setLODs( vector<MeshLODLevel> listLevel );
        /** @brief LOD 1 이상의 범위입니다(그릴 흐름 기준). 비면 LOD 0 하나입니다. */
        const vector<MeshLOD>& getLODs() const { return _listLOD; }
        /** @brief LOD 단계 수입니다(LOD 0 포함, 1 이상). */
        uint32 getLODCount() const { return 1u + static_cast<uint32>( _listLOD.size() ); }
        /** @brief LOD 1 이상의 정점을 단계 순서로 이은 것입니다. GPU 업로드가 LOD 0 뒤에 붙입니다. */
        const vector<RHIVertex>& getLODVertices() const { return _listLODVertex; }
        /** @brief LOD 1 이상의 인덱스를 단계 순서로 이은 것입니다. 값은 정점 흐름(LOD 0 뒤에 낮은 단계 정점) 번호입니다. */
        const vector<uint32>& getLODIndices() const { return _listLODIndex; }
        /** @brief LOD 1 이상 정점의 본 영향을 `getLODVertices` 와 같은 순서로 이은 것입니다. 스킨드 메시만 가지며 아니면 비어 있습니다. GPU 스키닝이 LOD 0 의 스킨 뒤에 붙입니다. */
        const vector<MeshSkinVertex>& getLODSkinVertices() const { return _listLODSkinVertex; }
        /** @brief 그릴 정점 흐름의 길이입니다(LOD 0 + 낮은 단계). 정점 풀 · 정점 버퍼가 이만큼 올린다. */
        uint32 getDrawVertexCount() const { return static_cast<uint32>( _listVertex.size() + _listLODVertex.size() ); }
        /** @brief 그릴 인덱스 흐름의 길이입니다(LOD 0 + 낮은 단계). */
        uint32 getDrawIndexCount() const { return static_cast<uint32>( _listIndex.size() + _listLODIndex.size() ); }
        /** @brief 화면 크기 @p screenSize 에 맞는 LOD 단계입니다 — 임계값이 화면 크기보다 큰 단계의 수(0 이면 LOD 0)입니다. */
        uint32 selectLOD( float32 screenSize ) const;
        /** @brief 이름의 구간 번호입니다. 없으면 비어 있습니다. */
        [[nodiscard]] optional<uint32> findSectionIndex( const hashed_string& name ) const;
        /** @brief 모프 타깃들입니다. */
        const vector<MeshMorphTarget>& getMorphTargets() const { return _listMorphTarget; }
        /** @brief 모프 타깃 수입니다. */
        uint32 getMorphTargetCount() const { return static_cast<uint32>( _listMorphTarget.size() ); }
        /** @brief 이름으로 모프 타깃 번호를 찾습니다. 없으면 없음입니다. */
        optional<uint32> findMorphTargetIndex( const hashed_string& name ) const;
        /**
         * @brief 스킨 데이터(레스트 정점과 가중치)의 식별자입니다 — 내용 번호입니다.
         * @details 모프 풀이 같은 번호의 스킨 인스턴스(칸)들을 한 원본 구간으로 묶는다(언리얼 스킨 캐시가 같은 스켈레탈 메시의 정점 팩토리를 공유하는 것과 같은 방식이다).
         *          캐릭터마다 사본을 두지 않는다 — 결과는 스킨 칸(`AnimationSkinSlot`)마다다.
         */
        const IdentityToken& getSkinDataID() const { return _contentID; }

        /**
         * @brief 정점 애니메이션 테이블(VAT)을 설정합니다. 설정한 메시는 스키닝 대신 정점 셰이더가 테이블을 인스턴스마다의 시각으로 읽습니다(먼 군중). 내용 번호가 바뀝니다.
         * @details 테이블은 같은 원본과 같은 클립을 쓰는 모든 캐릭터가 공유한다(메시 객체 하나이므로 배치 하나, 드로우 하나다). 스킨은 설정하지 않는다.
         */
        void setVertexAnimation( shared_ptr<const MeshVertexAnimation> animation );
        /** @brief 정점 애니메이션 테이블입니다. 없으면 nullptr 입니다. */
        const MeshVertexAnimation* findVertexAnimation() const { return _vertexAnimation.get(); }
        /** @brief 정점 개수를 반환합니다. */
        uint32 getVertexCount() const { return static_cast<uint32>( _listVertex.size() ); }
        /**
         * @brief 경계 상자 중심(`getLocalBoundsCenter`)에서 가장 먼 정점까지의 거리입니다(상자 중심 경계 구의 반지름). `setVertices` 가 구합니다.
         * @details 언리얼 `FBoxSphereBounds` 처럼 구의 중심은 원점이 아니라 상자 중심이다 — 발밑 피벗 캐릭터를 원점 중심 구로 감싸면 반지름이 키만큼이라
         *          LOD 화면 크기가 두 배, 컬링 구가 몇 배로 부푼다. 컴포넌트의 경계(`MeshComponent::getWorldBounds`)와 GPU 컬링이 이 구를 쓴다.
         */
        float32 getBoundingRadius() const { return _boundingRadius; }
        /** @brief 메시 공간 경계 상자의 중심입니다(경계 구의 중심). 정점이 없으면 원점입니다. */
        const float3& getLocalBoundsCenter() const { return _localBoundsCenter; }
        /** @brief 메시 공간의 축 정렬 경계(최소)입니다. 정점이 없으면 원점입니다. `setVertices` 가 구합니다. */
        const float3& getLocalBoundsMin() const { return _localBoundsMin; }
        /** @brief 메시 공간의 축 정렬 경계(최대)입니다. */
        const float3& getLocalBoundsMax() const { return _localBoundsMax; }
        /**
         * @brief 이 메시 **내용**의 정체성 토큰입니다(`IdentityToken`). 만들 때와 `setVertices` 때마다 새 토큰을 받습니다.
         * @details 정점 풀과 모프 풀은 메시 집합이 그대로인지를 이 토큰으로 판단한다. 포인터만 보면 메시가 지워진 위치에 새 메시가
         *          생기거나(할당기는 같은 크기의 공간을 곧바로 다시 준다) 같은 메시의 정점을 바꿀 때 "같은 집합" 으로 보여 옛 정점을 그린다.
         *          기억하는 쪽은 토큰을 값으로 들어 주소 재사용이 없다 — 프로세스 번호 발급기를 두지 않는다(언리얼 `FRenderResource` 가 GPU 자원을 객체 수명에 묶는 자리).
         */
        const IdentityToken& getContentID() const { return _contentID; }

        /** @brief 디바이스에 업로드(또는 재업로드)합니다. 같은 디바이스면 여러 번 불러도 결과가 같습니다. */
        [[nodiscard]] bool initRHI( IRHIDevice* pDevice ) override;

        /** @brief GPU 정점 버퍼 핸들을 반환합니다(그릴 정점 흐름 — LOD 0 뒤에 낮은 단계). */
        RHIBufferHandle getVertexBuffer() const { return _vertex._buffer; }
        /** @brief GPU 인덱스 버퍼(uint32) 핸들을 반환합니다(그릴 인덱스 흐름 — LOD 0 뒤에 낮은 단계, 값은 정점 흐름 번호). */
        RHIBufferHandle getIndexBuffer() const { return _index._buffer; }
        /**
         * @brief 살아 있는 디바이스에 올라가 있는지 반환합니다.
         * @details 옛 디바이스가 죽으면 통보(`RHIRenderResource`)가 먼저 와서 핸들을 비운다. 그래서 값이 남아
         *          있다는 것만으로 "살아 있는 디바이스의 것" 임이 보장된다(업로드 큐는 백엔드 교체 뒤 다시 올릴 메시를 이 값으로 고른다).
         */
        bool isRHIValid() const { return _vertex.isResident() && _index.isResident(); }

    private:
        /** @brief 정점 · 인덱스 버퍼를 실제로 해제합니다. 살아 있는 디바이스면 반환하고, 아니면 버립니다. */
        void releaseVertexBuffer();
        /** @brief 정점에서 경계 반지름을 다시 구합니다. */
        void refreshBoundingRadius();
        /** @brief 낮은 단계를 뗍니다(정점 · 스킨이 바뀌어 옛 단계가 맞지 않을 때). */
        void clearLODs();

    private:
        vector<RHIVertex> _listVertex;
        /// @brief getIndices 를 참고하세요. 수는 3 의 배수이고 번호는 `_listVertex` 안입니다.
        vector<uint32> _listIndex;
        /// @brief setSkin 을 참고하세요. 비어 있거나 `_listVertex` 와 길이가 같습니다.
        vector<MeshSkinVertex> _listSkinVertex;
        /// @brief setSkin 을 참고하세요. 비어 있으면 스킨이 없습니다.
        MeshSkinBinding _skinBinding;
        /// @brief setMorphTargets 를 참고하세요.
        vector<MeshMorphTarget> _listMorphTarget;
        /// @brief setSections 를 참고하세요.
        vector<MeshSection> _listSection;
        /// @brief setLODs 를 참고하세요. LOD 1 이상의 범위(그릴 정점 흐름 기준)와 그 정점입니다.
        vector<MeshLOD>        _listLOD;
        vector<RHIVertex>      _listLODVertex;
        vector<uint32>         _listLODIndex;
        vector<MeshSkinVertex> _listLODSkinVertex;
        /// @brief setVertexAnimation 을 참고하세요.
        shared_ptr<const MeshVertexAnimation> _vertexAnimation;
        /// @brief getBoundingRadius 를 참고하세요. setVertices 가 구합니다.
        float32 _boundingRadius{ 0.0f };
        float3  _localBoundsCenter{};
        float3  _localBoundsMin{};
        float3  _localBoundsMax{};
        /// @brief getContentID 를 참고하세요. 만들 때와 setVertices 때 새로 받습니다.
        IdentityToken _contentID{ IdentityToken::make() };
        /// @brief setGPUMorphEnabled 를 참고하세요. 이 메시가 모프 풀에 들어갈지 여부입니다.
        uint8 _bGPUMorph{ SW_FALSE };
        /// @brief setDynamic 을 참고하세요. 업로드 큐에 넣지 않는(정점 풀이 올리는) 프레임마다의 메시입니다.
        uint8 _bDynamic{ SW_FALSE };
        /// @brief setVertexFormat 을 참고하세요.
        RHIMeshVertexFormat _vertexFormat{ RHIMeshVertexFormat::Packed };
        /// @brief setGPUDeformProgram 을 참고하세요. 잠금(`_deformProgramMutex`) 안에서만 읽고 씁니다.
        shared_ptr<const vector<float4>> _deformProgram SW_GUARDED_BY( _deformProgramMutex );
        mutable mutex                                   _deformProgramMutex;
        atomic<uint64>                                  _deformProgramGeneration{ 0 };
        /** @brief 정점 · 인덱스 버퍼입니다. 어느 디바이스의 것인지를 함께 가지고 있습니다(RHIDeviceBuffer). 둘은 함께 만들고 함께 놓는다. */
        RHIDeviceBuffer _vertex;
        RHIDeviceBuffer _index;
    };
} // namespace sw
