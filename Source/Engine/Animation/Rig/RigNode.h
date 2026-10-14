/**
 * @file RigNode.h
 * @brief 후처리 리그의 노드 기반입니다. 대상(본, 소켓, 오브젝트) 서술, 노드가 받는 컨텍스트(준비, 평가, 바인딩), 엄격한 JSON 읽기, 노드 기반 클래스를 담습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Animation/Skeletal/Pose.h"
#include "Engine/Common/EngineDefines.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw
{
    struct RigSolveSpace;

    class RigInstance;
    class RigPoseBuffer;
    class Skeleton;

    /** @brief 대상이 무엇을 따르는가입니다. */
    enum class RigTargetKind : uint8
    {
        Bone = 0, ///< 본이다. 자기 유닛이면 현재 작업 포즈를, 다른 유닛이면 그 유닛의 이번 프레임 포즈를 쓴다.
        Socket,   ///< 소켓(부모 본 + 로컬 변환)이다. 자기 유닛이면 본과 같고, 다른 유닛이면 그 유닛의 소켓 테이블로 찾는다.
        Object,   ///< 다른 GameObject 의 월드 변환이다(게임 스레드에서 프레임 시작에 기록한다).
    };

    /**
     * @brief 리그 데이터의 대상 하나입니다. 노드는 대상을 이름으로 가리킵니다.
     * @details `_unit` 이 있으면 다른 유닛(장비 부품, 다른 캐릭터)의 본이나 소켓이며, 그 유닛이 먼저 평가되도록 의존을 건다(고리면 로드 오류).
     *          `_space` 가 있으면 대상을 프레임 시작에 그 **자기 본** 기준으로 기록해 두었다가 이번 포즈의 그 본에 다시 적용한다.
     *          무기처럼 손에 붙어 따라오는 것이 한 프레임 늦지 않게 따라가고, 무기 유닛에 의존을 걸지 않는다(손, 무기, 손 순환을 피한다).
     */
    struct SW_API RigTargetDef
    {
        hashed_string _name{};
        hashed_string _bone{};
        hashed_string _socket{};
        hashed_string _object{};
        hashed_string _unit{};
        hashed_string _space{};
        BoneTransform _offset{};
        RigTargetKind _kind{ RigTargetKind::Bone };

        /** @brief 다른 유닛이나 오브젝트를 따라가므로 호스트가 값을 넣어야 하는 대상인지 반환합니다. */
        bool isExternal() const { return _kind == RigTargetKind::Object || _unit.empty() == false; }
    };
} // namespace sw

namespace sw
{
    /** @brief 호스트가 프레임마다 넣는 바깥 대상 값입니다(유닛 모델 공간). */
    struct RigTargetValue
    {
        float3     _position{};
        quaternion _rotation{};
        int32      _relativeBone{ -1 }; ///< 0 이상이면 값이 이 본 기준의 상대 변환이며, 평가할 때 현재 그 본에 적용한다.
        uint8      _bValid{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 땅 질의의 결과입니다(월드). */
    struct RigGroundHit
    {
        float3 _position{};
        float3 _normal{ 0.0f, 1.0f, 0.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 발 디딤이 쓰는 땅 광선 질의입니다(게임 스레드). 물리 씬을 감싼 쪽이 구현합니다. */
    class SW_API IRigGroundQuery
    {
    public:
        IRigGroundQuery()                                    = default;
        virtual ~IRigGroundQuery()                           = default;
        IRigGroundQuery( const IRigGroundQuery& )            = delete;
        IRigGroundQuery& operator=( const IRigGroundQuery& ) = delete;

        /** @brief 월드 광선을 쏘아 처음 닿은 땅입니다. 없으면 false 입니다. */
        virtual bool raycastGround( const float3& origin, const float3& direction, float32 maxDistance, RigGroundHit& outHit ) const = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief 노드 가중치를 움직이는 클립 커브 값입니다(애니메이터가 구현). */
    class SW_API IRigCurveSource
    {
    public:
        IRigCurveSource()                                    = default;
        virtual ~IRigCurveSource()                           = default;
        IRigCurveSource( const IRigCurveSource& )            = delete;
        IRigCurveSource& operator=( const IRigCurveSource& ) = delete;

        /** @brief 이번 프레임의 커브 값입니다. 없는 커브는 0 입니다. */
        virtual float32 getCurveValue( const hashed_string& curveName ) const = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief 게임 스레드 준비 단계의 문맥입니다(프레임 시작, 단계들 앞). */
    struct RigPrepareContext
    {
        float4x4               _worldFromModel{};
        const IRigGroundQuery* _pGroundQuery{ nullptr };
        float3                 _worldGravity{ 0.0f, -constant::kDefaultGravity, 0.0f }; ///< 월드 중력이다. 호스트가 설정된 물리 중력을 넣는다(애니메이션 계층은 물리를 모른다).
        float32                _deltaSeconds{ 0.0f };
        /**
         * @brief 유닛의 화면 크기(`AnimationLODState::_screenSize`, 경계 구 지름 / 화면 높이)입니다. 노드의 `min_screen_size` 와 비교합니다.
         * @details LOD 판정을 하지 않는 호스트(테스트, 서버)는 기본값 1 을 두어 모든 노드를 켭니다.
         */
        float32 _screenSize{ 1.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 워커 평가 단계의 문맥입니다. */
    struct RigEvaluateContext
    {
        RigPoseBuffer*       _pPose{ nullptr };
        RigInstance*         _pInstance{ nullptr };
        float4x4             _worldFromModel{};
        float4x4             _modelFromWorld{};
        const RigSolveSpace* _pSpace{ nullptr };
        float3               _worldGravity{ 0.0f, -constant::kDefaultGravity, 0.0f }; ///< 준비 단계가 받은 월드 중력이다.
        float32              _deltaSeconds{ 0.0f };                                   ///< 지난 평가 뒤 흐른 시간이다(LOD 로 건너뛴 프레임 포함).
        float32              _nodeWeight{ 1.0f };                                     ///< 현재 노드의 가중치다. 본이 아닌 출력(모프 가중치)에는 노드가 직접 곱한다.
    };
} // namespace sw

namespace sw
{
    /**
     * @class RigJSONReader
     * @brief 노드나 대상 객체 하나를 읽는 엄격한 리더입니다. 읽은 키를 기록해 두고, `finish` 가 모르는 키를 오류로 냅니다.
     * @details 모든 실패는 컨텍스트(파일, 노드 이름)와 함께 로그로 남고 리더는 실패 상태가 된다. 데이터가 조용히 기본값이 되지 않게 하려는 것이다.
     */
    class SW_API RigJSONReader
    {
    public:
        RigJSONReader( const JSONValue& object, string_view context );

        [[nodiscard]] bool readName( string_view key, hashed_string& outValue, bool bRequired );
        [[nodiscard]] bool readFloat( string_view key, float32& outValue, bool bRequired );
        [[nodiscard]] bool readUint( string_view key, uint32& outValue, bool bRequired );
        [[nodiscard]] bool readBool( string_view key, bool& outValue, bool bRequired );
        [[nodiscard]] bool readFloat3( string_view key, float3& outValue, bool bRequired );
        /** @brief 도 단위 [피치, 요, 롤] 을 회전으로 읽습니다. */
        [[nodiscard]] bool readRotationDegrees( string_view key, quaternion& outValue, bool bRequired );
        [[nodiscard]] bool readNameList( string_view key, vector<hashed_string>& outListValue, bool bRequired );
        /** @brief 배열(객체들)을 꺼냅니다. 없거나 배열이 아니면 무효 값입니다. */
        JSONValue readArray( string_view key, bool bRequired );
        /** @brief 객체 하나를 꺼냅니다. 없거나 객체가 아니면 무효 값입니다. */
        JSONValue readObject( string_view key, bool bRequired );
        /** @brief 이름이나 열거 문자열 하나를 @p ppChoice 중에서 고릅니다(대소문자 구분). */
        [[nodiscard]] bool readChoice( string_view key, const utf8* const* ppChoice, uint32 choiceCount, uint32& outIndex, bool bRequired );

        /** @brief 오류를 남기고 실패로 둡니다. */
        void fail( string_view message );
        /** @brief 모르는 키를 오류로 내고 지금까지 성공했는지 반환합니다. */
        [[nodiscard]] bool finish();
        bool               isOk() const { return _bOk == SW_TRUE; }
        string_view        getContext() const { return _context; }

    private:
        JSONValue readMember( string_view key, bool bRequired );

        JSONValue      _object; ///< 값으로 보관한다. `array.at( i )` 같은 임시 객체를 받아도 리더보다 먼저 사라지지 않게 하려는 것이며, 핸들이라 복사가 싸다.
        vector<string> _listUsedKey;
        string         _context;
        uint8          _bOk;
    };
} // namespace sw

namespace sw
{
    /** @brief 노드를 스켈레톤과 대상에 바인딩하는 컨텍스트입니다(게임 스레드에서 리그를 붙일 때 한 번 씁니다). */
    struct SW_API RigBindContext
    {
        const Skeleton* _pSkeleton{ nullptr };
        RigInstance*    _pInstance{ nullptr };
        string_view     _label{};

        /** @brief 본 번호입니다. 없으면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool findBone( const hashed_string& name, uint32& outBone ) const;
        /** @brief 대상 번호입니다. 없으면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool findTarget( const hashed_string& name, uint32& outTarget ) const;
        /** @brief 본 목록을 번호로 바꿉니다. @p bChain 이면 사슬의 각 본이 다음 본의 조상인지 검사합니다. */
        [[nodiscard]] bool findBones( const vector<hashed_string>& listName, vector<uint32>& outListBone, bool bChain ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class RigNode
     * @brief 노드 하나입니다. 데이터(파라미터)와 실행 상태(스프링 입자, 부모 바꾸기 오프셋 등)를 함께 보관합니다. 에셋이 원형을 가지고 인스턴스가 복제합니다.
     * @details 가중치 = `_weight` × (커브가 있으면 그 값) × (시퀀서 슬롯이 있으면 그 값, 정하지 않았으면 1)이다. 가중치가 1 보다 작으면 노드가 쓴
     *          본(`collectWrittenBones`)의 로컬을 노드 앞의 값과 섞는다. 가중치가 0 이면 노드를 실행하지 않는다(상태도 멈춘다).
     *          화면 크기가 `_minScreenSize` 보다 작으면 노드를 끄고, 꺼지는 순간 `reset` 한다(다시 켜지면 애니메이션 포즈에서 시작한다).
     *          언리얼 애니메이션 노드의 LOD Threshold 에 해당하며, 거리 대신 화면 크기를 써서 시야각과 해상도가 달라도 같은 기준이 된다.
     */
    class SW_API RigNode
    {
    public:
        RigNode();
        virtual ~RigNode()                   = default;
        RigNode( const RigNode& )            = default;
        RigNode& operator=( const RigNode& ) = default;

        /** @brief 같은 종류와 같은 파라미터의 새 노드를 만듭니다(상태는 초기화됩니다). */
        virtual unique_ptr<RigNode> clone() const = 0;
        /** @brief 종류 이름(등록 이름)입니다. */
        virtual const utf8* getTypeName() const = 0;
        /** @brief 노드 자기 키를 읽습니다(공통 키는 에셋이 읽었습니다). */
        [[nodiscard]] virtual bool parse( RigJSONReader& reader ) = 0;
        /** @brief 이름을 번호로 바인딩합니다. 모르는 본이나 대상은 오류입니다. */
        [[nodiscard]] virtual bool bind( const RigBindContext& context ) = 0;
        /** @brief 게임 스레드 준비 단계입니다(땅 광선, 거리 LOD). */
        virtual void prepare( const RigPrepareContext& context ) { (void)context; }
        /** @brief 워커에서 포즈를 고칩니다. */
        virtual void evaluate( RigEvaluateContext& context ) = 0;
        /** @brief 쓰는 본을 모읍니다(가중치 섞기와 진단에 씁니다). 바인딩한 뒤에 호출합니다. */
        virtual void collectWrittenBones( vector<uint32>& inoutListBone ) const = 0;
        /** @brief 실행 상태를 버립니다(순간이동이나 다시 켤 때). */
        virtual void reset() {}
        /** @brief 이름 붙은 조절 값을 받습니다(부모 바꾸기의 활성 부모 등). 모르는 조절이면 false 입니다. */
        virtual bool setControl( const hashed_string& control, float32 value )
        {
            (void)control;
            (void)value;
            return false;
        }

        const hashed_string& getName() const { return _name; }
        float32              getWeight() const { return _weight; }
        /** @brief 기본 가중치를 바꿉니다(인스턴스의 복제본에만, 에디터 리그 패널과 런타임 조절이 씁니다). */
        void setWeight( float32 weight ) { _weight = weight; }
        /** @brief 노드를 켜 두는지입니다. 끄면 가중치가 0 인 것처럼 실행하지 않습니다(유니티 Animation Rigging 제약의 켜짐). */
        bool                 isEnabled() const { return _bEnabled == SW_TRUE; }
        void                 setEnabled( bool bEnabled ) { _bEnabled = bEnabled ? SW_TRUE : SW_FALSE; }
        const hashed_string& getWeightCurve() const { return _weightCurve; }
        const hashed_string& getWeightSlot() const { return _weightSlot; }
        /** @brief 노드를 켜 두는 가장 작은 화면 크기입니다. 0 이면 항상 켭니다. */
        float32 getMinScreenSize() const { return _minScreenSize; }
        /** @brief 공통 키(name, weight, weight_curve, weight_slot, min_screen_size)를 읽습니다(에셋이 호출합니다). */
        [[nodiscard]] bool parseCommon( RigJSONReader& reader );

    protected:
        hashed_string _name;
        hashed_string _weightCurve;
        hashed_string _weightSlot;
        float32       _weight;
        float32       _minScreenSize;
        uint8         _bEnabled;
    };
} // namespace sw
