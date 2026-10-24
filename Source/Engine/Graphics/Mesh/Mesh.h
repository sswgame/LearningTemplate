/**
 * @file Mesh.h
 * @brief CPU 메시 데이터와 GPU 정점 버퍼 업로드를 정의합니다. 씬 지오메트리용이며 RHI 데모용이 아닙니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Graphics/RHI/RHIRenderResource.h"
#include "Engine/Graphics/RHI/RHIResidentBuffer.h"
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
     * @brief 메시의 구간 하나입니다 — 정점 범위(인덱스 없는 삼각형 목록이라 3 의 배수)와 이름, 시점 가시성입니다.
     * @details 머티리얼은 구간마다 다르다. 구간을 그리는 컴포넌트(`MeshComponent::_sectionName`)가 머티리얼을 갖고, 같은 메시 객체를 나눠 써서 스킨 · 모프 결과도
     *          한 벌이다(언리얼 Skeletal Mesh 의 Section, 유니티 Mesh 의 SubMesh 에 해당한다). 구간이 없는 메시는 전체가 구간 하나다.
     */
    struct MeshSection
    {
        hashed_string        _name;
        uint32               _firstVertex{ 0 };
        uint32               _vertexCount{ 0 };
        MeshPersonVisibility _personVisibility{ MeshPersonVisibility::Both };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct MeshLODLevel
     * @brief 메시의 낮은 상세 단계(LOD 1 이상) 하나의 저작 형태입니다 — 그 단계의 삼각형 목록과, 이 단계로 넘어가는 화면 크기 임계값입니다.
     * @details 화면 크기는 경계 구의 화면 높이 비율이다(`AnimationLODUtil::computeScreenSize` 와 같은 값). 화면 크기가 임계값보다 작아지면 이 단계 이상을 그린다.
     *          모델 임포트가 단순화로 만들고(`ModelImporter`, 비율 · 임계값은 임포트 규칙 데이터) `.mesh` 의 `LODS` 덩어리에 싣는다.
     */
    struct MeshLODLevel
    {
        vector<RHIVertex> _listVertex;
        float32           _screenSize{ 0.0f };
        /// @brief 스킨드 메시의 단계면 정점마다의 본 영향(`_listVertex` 와 길이가 같다). 스킨 없는 메시의 단계는 비어 있다. `.mesh` 의 `LSKN` 덩어리에 싣는다.
        vector<MeshSkinVertex> _listSkinVertex{};
    };
} // namespace sw

namespace sw
{
    /**
     * @struct MeshLOD
     * @brief 그릴 정점 흐름 안에서 LOD 단계 하나의 범위입니다. 흐름은 LOD 0(`Mesh::getVertices`) 뒤에 낮은 단계 정점(`Mesh::getLODVertices`)이 이어진 것입니다.
     */
    struct MeshLOD
    {
        uint32  _firstVertex{ 0 };
        uint32  _vertexCount{ 0 };
        float32 _screenSize{ 0.0f }; ///< 이 단계로 넘어가는 화면 크기 임계값(`MeshLODLevel::_screenSize`)
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
     * @brief 삼각형 리스트 메시입니다(정점은 위치, 노멀, UV, 색). initRHI() 로 올린 GPU 정점 버퍼를 소유합니다.
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
         * @note 디바이스가 먼저 죽었다면 통보(`releaseRHI`, `forgetRHI`)가 먼저 와서 핸들을 비워 두므로,
         *       소멸 순서와 상관없이 죽은 디바이스로 destroy 하지 않습니다.
         */
        ~Mesh() override;

        /** @brief (RHIRenderResource) 살아 있는 디바이스에 정점 버퍼를 반환합니다. */
        void releaseRHI( IRHIDevice* pDevice ) override;
        /** @brief (RHIRenderResource) 디바이스가 이미 없을 때 부릅니다. 핸들만 버립니다. */
        void forgetRHI( IRHIDevice* pDevice ) override;

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
         * @brief 스킨드 메시의 그릴 사본을 만듭니다(정점과 스킨을 복사하고 **스킨 데이터 번호는 원본과 공유합니다**).
         * @details GPU 스키닝 결과(모프 풀의 결과 구간)는 메시 객체마다 하나라, 포즈가 다른 캐릭터와 군중 묶음은 사본을 그린다. 사본은 원본과
         *          레스트 정점과 가중치가 같으므로 모프 풀은 스킨 데이터 번호(`getSkinDataID`)로 원본 데이터를 **한 번만** 올리고 사본은 결과 구간만 받는다.
         */
        static shared_ptr<Mesh> createSkinInstance( const Mesh& source );
        /**
         * @brief 스킨 없는 모프 메시(블렌드 셰이프만 있는 소품 · 얼굴)를 본 하나에 가중치 1 로 묶은 스킨 원본으로 만듭니다(정점 · 모프 타깃 복사).
         * @details 모프 가중치는 GPU 스키닝 컴퓨트가 스키닝 앞에 건다. 스킨이 없으면 그 길에 들지 못하므로 고정 본 하나를 준다(임포트가 몸에 합칠 때와 같은 규칙).
         *          `SkeletalMeshComponent` 가 이런 메시를 받으면 이 함수로 원본을 바꿔 그린다 — 스켈레톤이 없으면 암묵 스켈레톤(본 하나)이다.
         */
        static shared_ptr<Mesh> createRigidSkinSource( const Mesh& source );

        /**
         * @brief CPU 정점 배열을 설정합니다. 탄젠트는 받은 값을 버리고 `computeTangents` 로 다시 구합니다.
         * @details 탄젠트는 위치 · 노멀 · UV 에서 나오는 값이라(경계 반지름처럼) 메시가 정한다. 정점을 만드는 쪽(도형 · 지형 · 물 · 잘린 조각 · 테스트)은
         *          탄젠트를 채우지 않아도 되고, 채우지 않은 정점이 0 탄젠트로 그려지는 일이 없다. 이미 구한 탄젠트(`.mesh`)는 `setVerticesWithTangents` 로 넣는다.
         */
        void setVertices( const vector<RHIVertex>& listVertex );
        void setVertices( vector<RHIVertex>&& listVertex );
        /**
         * @brief 탄젠트가 이미 들어 있는 정점 배열을 그대로 설정합니다(`.mesh` 를 읽은 값, 다른 메시의 사본).
         * @details 임포트가 `computeTangents` 로 구해 파일에 실은 값이다. 런타임이 로드마다 다시 구하지 않는다(언리얼 · 유니티가 임포트 때 MikkTSpace 를 베이크하는 것과 같다).
         */
        void setVerticesWithTangents( vector<RHIVertex>&& listVertex );
        /**
         * @brief 삼각형 목록 정점의 탄젠트(`RHIVertex::_arrTangent`)를 위치 · 노멀 · UV 로 구합니다. 기준은 이 함수 하나입니다(런타임 · 임포트 · `.mesh` 쓰기).
         * @details MikkTSpace 와 같은 규약이다 — 면마다 UV 기울기에서 탄젠트 · 바이탄젠트를 구해 모서리 각도로 가중하고, 위치 · 노멀 · UV 가 비트까지 같은 정점끼리
         *          (손잡이가 같은 면끼리만) 모아 더한 뒤 노멀에 직교화한다. W 는 바이탄젠트 부호(cross( N, T ) 와 면의 바이탄젠트가 같은 쪽이면 +1)다.
         *          UV 가 퇴화해(면적 0) 기울기가 없는 정점은 노멀에 수직이고 +X 에 가장 가까운 방향을 받는다(노멀이 X 축이면 +Z). 셰이더의 노멀 맵은 평평한 맵에서 노멀을 그대로 낸다.
         */
        static void computeTangents( vector<RHIVertex>& inoutListVertex );
        /** @brief CPU 정점 배열입니다. `setVertices` 와 짝을 이루며, GPU 업로드 뒤에도 원본은 여기 남습니다. */
        const vector<RHIVertex>& getVertices() const { return _listVertex; }

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
         * @brief 모프 컴퓨트가 기본 물결 대신 실행할 변형 프로그램(float4 목록)을 넣습니다. 빈 목록이면 프로그램을 뗍니다. 내용 번호는 바뀌지 않습니다.
         * @details 메시 모디파이어 미리보기(`GF_EditorPlugIn_MeshModifiers`)가 쓴다 — 모양은 `meshmodifier.hlsli` 머리 주석이 기준이다. GPU 모프(`setGPUMorphEnabled`)가 켜진 메시에만 걸린다.
         *          게임 스레드가 아무 때나 바꾸고 렌더 스레드는 프레임마다 사본을 잡아 읽으므로(잠금) 정점 버퍼를 다시 만들지 않고 매개변수만 바꿀 수 있다.
         *          목록은 엔진 안에서 복사해 든다 — 넘긴 쪽(확장 모듈)이 내려가도 소멸 코드가 엔진에 남는다.
         */
        void setGPUDeformProgram( const vector<float4>& listProgram );
        /** @brief 변형 프로그램의 사본(공유)입니다. 없으면 nullptr 입니다. 렌더 스레드가 프레임마다 부릅니다. */
        shared_ptr<const vector<float4>> getGPUDeformProgram() const;
        /** @brief 변형 프로그램을 바꿀 때마다 오르는 번호입니다(0 은 한 번도 넣지 않음). 모프 풀이 다시 올릴지 이 값으로 정합니다. */
        uint64 getGPUDeformProgramGeneration() const { return _deformProgramGeneration.load( std::memory_order_acquire ); }

        /**
         * @brief 스킨(정점마다의 본 영향)을 설정합니다. 길이는 정점 수와 같아야 하고, 아니면 스킨을 지웁니다. 내용 번호가 바뀝니다.
         * @details 스킨이 있는 메시는 GPU 스키닝 풀(모프 풀의 스킨 구간)에 들어가 정점 셰이더가 변형된 정점을 읽는다. 팔레트(본 행렬)는
         *          메시가 아니라 그것을 그리는 컴포넌트가 프레임마다 낸다(`SkeletalMeshComponent`). 그래서 포즈가 다른 캐릭터는 메시 객체를 따로 둔다.
         * @param boneCount 메시가 따르는 스켈레톤의 본 수(팔레트 길이)입니다.
         */
        void setSkin( vector<MeshSkinVertex> listSkinVertex, uint32 boneCount );
        /** @brief 정점마다의 본 영향입니다. 스킨이 없으면 비어 있습니다. */
        const vector<MeshSkinVertex>& getSkinVertices() const { return _listSkinVertex; }
        /** @brief 스킨이 있으면 true 입니다. */
        bool hasSkin() const { return _skinBoneCount > 0 && _listSkinVertex.size() == _listVertex.size(); }
        /** @brief 스켈레톤 본 수(팔레트 길이)입니다. 스킨이 없으면 0 입니다. */
        uint32 getSkinBoneCount() const { return _skinBoneCount; }
        /**
         * @brief 모프 타깃(블렌드 셰이프)을 설정합니다. 정점 번호가 범위 밖인 차이는 버립니다. 내용 번호가 바뀝니다.
         * @details 타깃은 GPU 스키닝 컴퓨트가 스키닝 앞에 가중치만큼 더한다(모프 풀의 원본 데이터이며 사본은 공유한다). 가중치는 메시가 아니라 그리는 유닛이 낸다
         *          (`SkeletalMeshComponent::setMorphWeight`). 그래서 모프 타깃이 있는 메시는 스킨 구간으로 간다(스킨이 없으면 임포트가 고정 본 하나를 준다).
         */
        void setMorphTargets( vector<MeshMorphTarget> listTarget );
        /**
         * @brief 구간을 설정합니다. 구간은 정점 범위 안이고 3 의 배수이며 이름이 겹치지 않아야 합니다 — 아니면 오류를 남기고 구간을 비운 채 false 입니다.
         *        정점이 0 인 구간은 이름은 있되 그릴 것이 없는 구간입니다(외형 조립이 덮인 삼각형을 모두 뺀 구간 — 그 구간 컴포넌트는 아무것도 그리지 않는다).
         * @details 내용 번호는 바꾸지 않는다(정점이 그대로라 정점 · 모프 풀을 다시 올릴 이유가 없다). 그리는 범위는 배치 키라 다음 수집이 다시 나눈다.
         */
        [[nodiscard]] bool setSections( vector<MeshSection> listSection );
        /** @brief 구간들입니다. 비면 구간이 없는 메시(전체가 하나)입니다. */
        const vector<MeshSection>& getSections() const { return _listSection; }
        /**
         * @brief 낮은 상세 단계(LOD 1 이상)를 설정합니다. 정점은 탄젠트가 든 값이어야 합니다(`.mesh` 를 읽은 값 — 쓰기가 단계마다 `computeTangents` 로 구했다).
         * @details 단계마다 정점 수가 0 이 아닌 3 의 배수이고, 임계값은 (0, 1) 안에서 단계가 낮아질수록 작아야 한다. 모프 타깃 · 구간 · 정점 애니메이션이 있는 메시는
         *          LOD 를 받지 않는다. 스킨드 메시는 받는다 — 단계마다 스킨 정점(`MeshLODLevel::_listSkinVertex`)이 정점과 같은 길이로 오고 뼈 번호가 `getSkinBoneCount` 안이어야 하며,
         *          스킨 없는 메시의 단계는 스킨이 없어야 한다(그래서 **스킨을 먼저 설정**한다 — `setSkin` 은 LOD 를 뗀다). GPU 스키닝은 LOD 0 뒤의 낮은 단계 정점도 변형한다.
         *          어기면 오류를 남기고 LOD 를 비운 채 false 입니다. 빈 목록은 LOD 를 뗀다.
         *          CPU 정점(`getVertices`)은 LOD 0 그대로다 — 물리 · 내비 · 피킹은 LOD 를 보지 않는다. 내용 번호가 바뀐다(정점 풀이 다시 올린다).
         */
        [[nodiscard]] bool setLODs( vector<MeshLODLevel> listLevel );
        /** @brief LOD 1 이상의 범위입니다(그릴 정점 흐름 기준). 비면 LOD 0 하나입니다. */
        const vector<MeshLOD>& getLODs() const { return _listLOD; }
        /** @brief LOD 단계 수입니다(LOD 0 포함, 1 이상). */
        uint32 getLODCount() const { return 1u + static_cast<uint32>( _listLOD.size() ); }
        /** @brief LOD 1 이상의 정점을 단계 순서로 이은 것입니다. GPU 업로드가 LOD 0 뒤에 붙입니다. */
        const vector<RHIVertex>& getLODVertices() const { return _listLODVertex; }
        /** @brief LOD 1 이상 정점의 본 영향을 `getLODVertices` 와 같은 순서로 이은 것입니다. 스킨드 메시만 가지며 아니면 비어 있습니다. GPU 스키닝이 LOD 0 의 스킨 뒤에 붙입니다. */
        const vector<MeshSkinVertex>& getLODSkinVertices() const { return _listLODSkinVertex; }
        /** @brief 그릴 정점 흐름의 길이입니다(LOD 0 + 낮은 단계). 정점 풀 · 정점 버퍼가 이만큼 올린다. */
        uint32 getDrawVertexCount() const { return static_cast<uint32>( _listVertex.size() + _listLODVertex.size() ); }
        /** @brief 화면 크기 @p screenSize 에 맞는 LOD 단계입니다 — 임계값이 화면 크기보다 큰 단계의 수(0 이면 LOD 0)입니다. */
        uint32 selectLOD( float32 screenSize ) const;
        /** @brief 이름의 구간 번호입니다. 없으면 -1 입니다. */
        int32 findSectionIndex( const hashed_string& name ) const;
        /** @brief 모프 타깃들입니다. */
        const vector<MeshMorphTarget>& getMorphTargets() const { return _listMorphTarget; }
        /** @brief 모프 타깃 수입니다. */
        uint32 getMorphTargetCount() const { return static_cast<uint32>( _listMorphTarget.size() ); }
        /** @brief 이름으로 모프 타깃 번호를 찾습니다. 없으면 -1 입니다. */
        int32 findMorphTargetIndex( const hashed_string& name ) const;
        /**
         * @brief 스킨 데이터(레스트 정점과 가중치)의 식별자입니다. 보통은 내용 번호이고, `createSkinInstance` 의 사본은 원본의 번호를 공유합니다.
         * @details 모프 풀이 같은 번호의 메시들을 한 원본 구간으로 묶는다(언리얼 스킨 캐시가 같은 스켈레탈 메시의 정점 팩토리를 공유하는 것과 같은 방식이다).
         */
        uint64 getSkinDataID() const { return _sharedSkinDataID != 0 ? _sharedSkinDataID : _contentID; }

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
         * @brief 메시 공간 원점에서 가장 먼 정점까지의 거리입니다(원점 중심 경계 구의 반지름). `setVertices` 가 구합니다.
         * @details 컴포넌트의 경계(`MeshComponent::getBoundsRadius`)와 GPU 컬링이 이 값을 쓴다. 고정 반지름(단위 상자의 0.866)을 쓰면
         *          그보다 큰 도형(캡슐 끝, 평면)이 화면 가장자리에서 보이는데도 잘린다.
         */
        float32 getBoundingRadius() const { return _boundingRadius; }
        /** @brief 메시 공간의 축 정렬 경계(최소)입니다. 정점이 없으면 원점입니다. `setVertices` 가 구합니다. */
        const float3& getLocalBoundsMin() const { return _localBoundsMin; }
        /** @brief 메시 공간의 축 정렬 경계(최대)입니다. */
        const float3& getLocalBoundsMax() const { return _localBoundsMax; }
        /**
         * @brief 이 메시 **내용**의 프로세스 고유 번호입니다. 만들 때와 `setVertices` 때마다 새 번호를 받습니다.
         * @details 정점 풀과 모프 풀은 메시 집합이 그대로인지를 이 번호로 판단한다. 포인터만 보면 메시가 지워진 위치에 새 메시가
         *          생기거나(할당기는 같은 크기의 공간을 곧바로 다시 준다) 같은 메시의 정점을 바꿀 때 "같은 집합" 으로 보여 옛 정점을 그린다.
         */
        uint64 getContentID() const { return _contentID; }

        /** @brief 디바이스에 업로드(또는 재업로드)합니다. 같은 디바이스면 여러 번 불러도 결과가 같습니다. */
        bool initRHI( IRHIDevice* pDevice ) override;

        /** @brief GPU 정점 버퍼 핸들을 반환합니다. */
        RHIBufferHandle getVertexBuffer() const { return _vertex._buffer; }
        /**
         * @brief 살아 있는 디바이스에 올라가 있는지 반환합니다.
         * @details 옛 디바이스가 죽으면 통보(`RHIRenderResource`)가 먼저 와서 핸들을 비운다. 그래서 값이 남아
         *          있다는 것만으로 "살아 있는 디바이스의 것" 임이 보장된다(업로드 큐는 백엔드 교체 뒤 다시 올릴 메시를 이 값으로 고른다).
         */
        bool isRHIValid() const { return _vertex.isResident(); }

    private:
        /** @brief 정점 버퍼를 실제로 해제합니다. 살아 있는 디바이스면 반환하고, 아니면 버립니다. */
        void releaseVertexBuffer();
        /** @brief 새 내용 번호를 발급합니다(프로세스 전역이며 0은 쓰지 않습니다). */
        static uint64 allocateContentID();
        /** @brief 정점에서 경계 반지름을 다시 구합니다. */
        void refreshBoundingRadius();

        vector<RHIVertex> _listVertex;
        /// @brief setSkin 을 참고하세요. 비어 있거나 `_listVertex` 와 길이가 같습니다.
        vector<MeshSkinVertex> _listSkinVertex;
        /// @brief 스켈레톤 본 수입니다. 0이면 스킨이 없습니다.
        uint32 _skinBoneCount{ 0 };
        /// @brief setMorphTargets 를 참고하세요.
        vector<MeshMorphTarget> _listMorphTarget;
        /// @brief setSections 를 참고하세요.
        vector<MeshSection> _listSection;
        /// @brief setLODs 를 참고하세요. LOD 1 이상의 범위(그릴 정점 흐름 기준)와 그 정점입니다.
        vector<MeshLOD>        _listLOD;
        vector<RHIVertex>      _listLODVertex;
        vector<MeshSkinVertex> _listLODSkinVertex;
        /// @brief `createSkinInstance` 사본이 공유하는 원본의 스킨 데이터 번호입니다. 0이면 자기 내용 번호를 씁니다(정점이나 스킨을 바꾸면 0으로 돌아갑니다).
        uint64 _sharedSkinDataID{ 0 };
        /// @brief setVertexAnimation 을 참고하세요.
        shared_ptr<const MeshVertexAnimation> _vertexAnimation;
        /// @brief getBoundingRadius 를 참고하세요. setVertices 가 구합니다.
        float32 _boundingRadius{ 0.0f };
        float3  _localBoundsMin{};
        float3  _localBoundsMax{};
        /// @brief getContentID 를 참고하세요. 만들 때와 setVertices 때 새로 받습니다.
        uint64 _contentID{ allocateContentID() };
        /// @brief setGPUMorphEnabled 를 참고하세요. 이 메시가 모프 풀에 들어갈지 여부입니다.
        uint8 _bGPUMorph{ SW_FALSE };
        /// @brief setDynamic 을 참고하세요. 업로드 큐에 넣지 않는(정점 풀이 올리는) 프레임마다의 메시입니다.
        uint8 _bDynamic{ SW_FALSE };
        /// @brief setGPUDeformProgram 을 참고하세요. 잠금(`_deformProgramMutex`) 안에서만 읽고 씁니다.
        shared_ptr<const vector<float4>> _deformProgram;
        mutable mutex                    _deformProgramMutex;
        atomic<uint64>                   _deformProgramGeneration{ 0 };
        /** @brief 정점 버퍼입니다. 어느 디바이스의 것인지를 함께 가지고 있습니다(RHIResidentBuffer). */
        RHIResidentBuffer _vertex;
    };
} // namespace sw
