/**
 * @file MeshModifier.h
 * @brief 메시 모디파이어 하나의 공통 틀(메시 데이터 · 기즈모 · 선언적 매개변수 · 외부 입력 · 정점 변형)을 선언합니다. ImGui 와 씬을 모릅니다.
 * @details 새 모디파이어를 더하는 법(다른 파일은 고치지 않는다):
 *          1) `Modifiers/<이름>Modifier.cpp` 하나를 만들고 `MeshModifier` 를 상속한다(패널이 따로 알아야 할 상태가 있으면 `.h` 도 둔다).
 *          2) 생성자에서 `addParam` 으로 매개변수를 선언한다. **기본값은 항등이어야 한다**(기본값 그대로면 메시가 변하지 않는다 —
 *             공용 속성 테스트가 등록된 모든 모디파이어에 대해 확인한다).
 *          3) 정점 하나씩 옮기는 모디파이어는 `deformPoint` 만 구현한다. UV · 색 · 선택 가중치를 바꾸거나 정점 전체를 봐야 하는 모디파이어는
 *             `applyToMesh` 를 재정의한다. 어느 쪽이든 결정적이어야 한다(같은 입력 · 같은 시간 → 같은 출력, 전역 상태 · 시계 · 난수 금지).
 *          4) 파일 끝에 `SW_MESH_MODIFIER( 클래스, "id", "표시 이름", "카테고리" );` 한 줄을 둔다. 패널의 추가 메뉴 · 저장 문서 · 테스트가 자동으로 본다.
 *
 *          정점은 먼저 모디파이어 공간(기즈모 중심이 원점, 기즈모 회전을 뺀 공간)으로 옮겨 변형하고 다시 메시 로컬 공간으로 돌려놓는다.
 *          `_bWorldSpace` 면 모디파이어 공간의 기준이 메시 로컬이 아니라 월드(또는 기준 오브젝트의 월드 변환)다 — 월드 공간 워프의 자리.
 *          변형 결과는 정점의 선택 가중치(`_listWeight`, 없으면 1)만큼만 섞는다. 그래서 선택 모디파이어가 가중치를 쓰면 뒤따르는 모디파이어의 세기가 된다.
 *          법선은 변형 함수의 야코비안(법선에 수직인 접선 두 개를 조금 밀어 본 차분)으로 옮긴다. 스택이 법선 재계산을 고르면 끝에 면 법선으로 다시 만든다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"
#include "Core/Task/TaskTypes.h"

#include "Engine/Utility/FloatCurve.h"

namespace sw
{
    class JSONValue;
    class TaskManager;
} // namespace sw

namespace sw::editor
{
    /**
     * @struct MeshModifierMesh
     * @brief 모디파이어가 변형하는 메시 정점입니다(메시 로컬 공간, 인덱스 없는 삼각형 목록 — 정점 3 개가 삼각형 하나).
     * @details 위치 밖의 배열은 비었거나 위치와 같은 길이다. 비었으면 그 채널이 없는 것이다(모디파이어는 건너뛴다).
     */
    struct MeshModifierMesh
    {
        vector<float3>  _listPosition;
        vector<float3>  _listNormal;
        vector<float2>  _listUV;     ///< UV 채널 0
        vector<float4>  _listColor;  ///< 정점 색(RGBA)
        vector<float32> _listWeight; ///< 선택 가중치 [0,1]. 비었으면 모두 1 입니다

        size_t getVertexCount() const { return _listPosition.size(); }
        bool   hasNormal() const { return _listNormal.size() == _listPosition.size(); }
        bool   hasUv() const { return _listUV.size() == _listPosition.size(); }
        bool   hasColor() const { return _listColor.size() == _listPosition.size(); }
        bool   hasWeight() const { return _listWeight.size() == _listPosition.size(); }
        /** @brief 정점의 선택 가중치입니다. 가중치 채널이 없으면 1 입니다. */
        float32 getWeight( size_t vertexIndex ) const { return hasWeight() ? _listWeight[vertexIndex] : 1.0f; }
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 축 정렬 경계 상자입니다. `_min` 이 `_max` 보다 크면 무효입니다. */
    struct MeshModifierBounds
    {
        float3 _min{ 0.0f, 0.0f, 0.0f };
        float3 _max{ -1.0f, -1.0f, -1.0f };

        bool   isValid() const { return _min._x <= _max._x && _min._y <= _max._y && _min._z <= _max._z; }
        float3 getCenter() const { return ( _min + _max ) * 0.5f; }
        float3 getSize() const { return _max - _min; }
        /** @brief 점 목록을 감싸는 상자를 만듭니다. 목록이 비면 무효 상자입니다. */
        static MeshModifierBounds makeFromPoints( const vector<float3>& listPoint );
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 모디파이어가 놓이는 자리(중심 · 회전 · 주축)입니다. 기준 공간은 메시 로컬 또는 월드입니다(`MeshModifier::isWorldSpace`). */
    struct MeshModifierGizmo
    {
        float3 _center{ 0.0f, 0.0f, 0.0f };
        float3 _rotationDegree{ 0.0f, 0.0f, 0.0f }; ///< X → Y → Z 순서로 거는 오일러 각(도)
        uint32 _axisIndex{ 1 };                     ///< 주축 성분 번호(0 = X, 1 = Y, 2 = Z)

        /** @brief 기준 공간 점을 모디파이어 공간으로 옮깁니다. */
        float3 toModifierSpace( const float3& point ) const;
        /** @brief 모디파이어 공간 점을 기준 공간으로 돌려놓습니다. */
        float3 toBaseSpace( const float3& modifierPoint ) const;
        /** @brief 기준 공간 방향을 모디파이어 공간으로 옮깁니다(평행 이동 없음). */
        float3 toModifierDirection( const float3& direction ) const;
        /** @brief 모디파이어 공간 방향을 기준 공간으로 돌려놓습니다. */
        float3 toBaseDirection( const float3& modifierDirection ) const;
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 매개변수 값의 종류입니다. 패널 입력칸과 저장 문서의 모양을 정합니다. */
    enum class MeshModifierParamKind : uint8
    {
        Float,
        Int,
        Bool,
        Axis,          ///< 0 = X, 1 = Y, 2 = Z
        Vector3,       ///< 세 성분 모두 [`_min`, `_max`]
        Curve,         ///< [0,1] → 값 곡선(`FloatCurve`). 키는 시간 오름차순이고 키마다 보간(Constant · Linear · Cubic)과 접선을 든다
        Reference,     ///< 씬 오브젝트 참조(오브젝트 이름 — 스플라인 · 대상 메시 · 콜라이더 · 워프 기준). 글로 둡니다
        AssetPath,     ///< 에셋 경로(텍스처 · 포인트 캐시 · 정점 애니메이션 · 메시). 리소스 id 글로 둡니다
        NumberList,    ///< 수 목록. `_stride` 개씩 한 항목(예: 충돌점 = x y z 반경 세기 → stride 5)
        CurveList,     ///< 곡선 목록(예: 층마다 곡선). 항목은 `Curve` 와 같은 모양이고 `_max` 가 항목 수 상한이다. 기본은 빈 목록
        ReferenceList, ///< 참조 목록(예: 대상 N 개). 항목은 `Reference` 와 같은 모양이고 `_max` 가 항목 수 상한이다. 기본은 빈 목록
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief NumberList 표의 열 하나가 차지하는 원소와 입력칸 모양입니다. */
    enum class MeshModifierListColumnKind : uint8
    {
        Float,   ///< 원소 하나, 실수 입력
        Int,     ///< 원소 하나, 정수 입력(반올림)
        Combo,   ///< 원소 하나, 이름 목록의 번호(0 부터)
        Vector3, ///< 원소 셋을 한 칸에 묶은 xyz 입력
    };

    /**
     * @struct MeshModifierListColumn
     * @brief NumberList 표의 열 선언입니다. 모디파이어 파일의 정적 배열로 두고 `MeshModifierParamDef::withColumns` 로 붙입니다.
     * @details 열 너비(원소 수) 합이 `_stride` 와 같아야 한다. 다르면 패널은 열 선언을 무시하고 일반 숫자 표로 그린다.
     */
    struct MeshModifierListColumn
    {
        const utf8*                _pName{ nullptr }; ///< 열 머리 이름(영문)
        MeshModifierListColumnKind _kind{ MeshModifierListColumnKind::Float };
        const utf8*                _pComboItems{ nullptr }; ///< Combo 의 이름 목록. ImGui 처럼 '\0' 으로 나누고 빈 이름으로 끝냅니다(예: "Sphere\0Box\0Plane\0")
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @brief 매개변수 하나의 선언입니다. 모디파이어 생성자가 `addParam` 으로 냅니다.
     * @details `_pHint` 뒤의 칸은 모두 패널 표시용 선택 칸이다(기본값이 있어 기존 선언은 고치지 않는다). 저장 문서 · 값 구조에는 영향이 없다.
     *          긴 위치 초기화 대신 `with*` 함수로 이어 붙인다 — `MeshModifierParamDef{ … }.withLayout( "Shape", "Bend angle", "deg" )`.
     */
    struct MeshModifierParamDef
    {
        static constexpr size_t kDefaultListLimit = 64; ///< 목록 종류의 `_max` 를 안 정했을 때의 항목 수 상한

        const utf8*                   _pName{ nullptr }; ///< 저장 문서의 키이자 패널 표시 이름(영문, 모디파이어 안에서 유일)
        MeshModifierParamKind         _kind{ MeshModifierParamKind::Float };
        float32                       _min{ 0.0f };                       ///< 수 종류의 하한(NumberList 는 원소마다)
        float32                       _max{ 1.0f };                       ///< 수 종류의 상한
        float3                        _default{ 0.0f, 0.0f, 0.0f };       ///< 스칼라 종류는 `_x` 만 씁니다. 곡선은 (0, `_x`) → (1, `_y`) 직선입니다
        uint32                        _stride{ 1 };                       ///< NumberList 의 항목 하나 원소 수
        const utf8*                   _pHint{ nullptr };                  ///< 참조 · 경로 · 목록의 뜻(영문, 패널 도움말 — 예: "Spline object", "x y z radius strength")
        const utf8*                   _pGroup{ nullptr };                 ///< 패널의 접히는 구역 이름(영문). 없으면 구역 없이 맨 위에 둡니다
        const utf8*                   _pTooltip{ nullptr };               ///< 입력칸 툴팁(영문 한 줄)
        const utf8*                   _pUnit{ nullptr };                  ///< 값 뒤에 붙여 보일 단위(예: "deg", "m")
        const MeshModifierListColumn* _pColumn{ nullptr };                ///< NumberList 의 열 선언(정적 배열). 없으면 일반 숫자 표입니다
        uint32                        _columnCount{ 0 };                  ///< `_pColumn` 원소 수
        const utf8*                   _pRequiredComponentHint{ nullptr }; ///< Reference 고르기에서 걸러 볼 컴포넌트 타입 이름(예: "SplineComponent")
        const utf8*                   _pAssetFilterHint{ nullptr };       ///< AssetPath 슬롯이 받을 확장자(예: ".pc2"). 없으면 모두 받습니다

        /** @brief 목록 종류(`CurveList` · `ReferenceList`)가 받는 항목 수 상한입니다. `_max` 가 1 이상이면 그 값, 아니면 안전한 기본값입니다. */
        size_t getListLimit() const { return _max >= 1.0f ? static_cast<size_t>( _max ) : kDefaultListLimit; }

        /** @brief 구역 · 툴팁 · 단위를 넣은 사본을 냅니다. */
        MeshModifierParamDef withLayout( const utf8* pGroup, const utf8* pTooltip, const utf8* pUnit = nullptr ) const
        {
            MeshModifierParamDef def = *this;
            def._pGroup              = pGroup;
            def._pTooltip            = pTooltip;
            def._pUnit               = pUnit;
            return def;
        }
        /** @brief NumberList 열 선언을 넣은 사본을 냅니다. */
        template <size_t TCount>
        MeshModifierParamDef withColumns( const MeshModifierListColumn ( &arrColumn )[TCount] ) const
        {
            MeshModifierParamDef def = *this;
            def._pColumn             = arrColumn;
            def._columnCount         = static_cast<uint32>( TCount );
            return def;
        }
        /** @brief Reference 고르기의 컴포넌트 걸러 보기를 넣은 사본을 냅니다. */
        MeshModifierParamDef withRequiredComponent( const utf8* pComponentTypeName ) const
        {
            MeshModifierParamDef def    = *this;
            def._pRequiredComponentHint = pComponentTypeName;
            return def;
        }
        /** @brief AssetPath 슬롯의 확장자 걸러 받기를 넣은 사본을 냅니다. */
        MeshModifierParamDef withAssetFilter( const utf8* pExtension ) const
        {
            MeshModifierParamDef def = *this;
            def._pAssetFilterHint    = pExtension;
            return def;
        }
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 매개변수 하나의 현재 값입니다. 종류마다 쓰는 칸이 다릅니다. */
    struct MeshModifierParamValue
    {
        float3             _value{ 0.0f, 0.0f, 0.0f }; ///< 스칼라 종류는 `_x`, Vector3 는 셋
        FloatCurve         _curve;                     ///< Curve
        vector<FloatCurve> _listCurve;                 ///< CurveList
        vector<float32>    _listNumber;                ///< NumberList
        string             _text;                      ///< Reference · AssetPath
        vector<string>     _listText;                  ///< ReferenceList
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class IMeshModifierInputs
     * @brief 모디파이어가 다른 자원(스플라인 · 다른 메시 · 오브젝트 변환 · 텍스처)을 읽는 창구입니다. 에디터 세션이 씬과 에셋으로 구현하고, 테스트는 가짜를 넘깁니다.
     * @details 모든 함수는 못 찾으면 false 이고, 그때 모디파이어는 그 효과를 건너뛴다(항등). 좌표는 모두 월드 공간이다.
     */
    class IMeshModifierInputs
    {
    public:
        virtual ~IMeshModifierInputs() = default;

        /** @brief 참조(오브젝트 이름)의 월드 변환 행렬입니다(워프 기준 · 피벗 · 콜라이더). */
        [[nodiscard]] virtual bool tryGetWorldMatrix( string_view reference, float4x4& outMatrix ) const = 0;
        /** @brief 참조가 가리키는 곡선(스플라인)의 호 길이입니다. */
        [[nodiscard]] virtual bool tryGetCurveLength( string_view reference, float32& outLength ) const = 0;
        /** @brief 곡선을 호 길이 비율 [0,1] 에서 표본합니다(위치 · 단위 접선). */
        [[nodiscard]] virtual bool trySampleCurve( string_view reference, float32 fraction, float3& outPosition, float3& outTangent ) const = 0;
        /** @brief 참조가 가리키는 메시 오브젝트의 월드 삼각형 목록입니다(Conform 대상). */
        [[nodiscard]] virtual bool tryGetMeshTriangles( string_view reference, vector<float3>& outListPosition ) const = 0;
        /** @brief 텍스처 에셋을 UV 에서 표본합니다(RGBA, [0,1]). 베이크에서는 정적 텍스처 파일만 됩니다. */
        [[nodiscard]] virtual bool trySampleTexture( string_view assetPath, const float2& uv, float4& outColor ) const = 0;
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 스택 한 번 거는 동안 모든 모디파이어가 함께 보는 값입니다. */
    struct MeshModifierEvaluation
    {
        float4x4                   _meshToWorld{};      ///< 메시 로컬 → 월드(월드 공간 모디파이어 · 외부 입력의 좌표 변환)
        const IMeshModifierInputs* _pInputs{ nullptr }; ///< 없으면 외부 입력을 쓰는 모디파이어는 항등입니다
        /**
         * @brief 모디파이어 적용을 병렬로 돌릴 작업 관리자입니다. nullptr 이면 현재 스레드가 한 번에 돕니다(테스트 · 서비스 연결 전의 직렬 실행이며 결과는 같다).
         * @details 엔진 내부 전용 도우미(`engine::runParallel`)는 확장 모듈이 쓸 수 없어서, 세션이 에디터 서비스에서 얻은 관리자를 평가마다 싣는다.
         *          전에는 이 모듈의 전역 설정 함수(`setMeshModifierTaskManager`)로 건 정적 포인터였다 - 평가가 소유자를 정하므로 호출마다 명시되고 테스트끼리 섞이지 않는다.
         */
        TaskManager* _pTaskManager{ nullptr };
        float32      _time{ 0.0f }; ///< 초. 애니메이션 모디파이어(Waving · Dynamic Ripple · Vertex Anim)가 씁니다
        /** @brief 스택에 들어온 변형 전 정점 위치(메시 로컬)입니다. 스택이 채우고, 없거나 정점 수가 다르면 각 모디파이어의 입력 위치를 대신 씁니다. */
        const vector<float3>* _pListStackInputPosition{ nullptr };
        /**
         * @brief 넣으면 `MeshModifier::apply` 가 켜진 모디파이어마다 입력 상자(모디파이어 공간, 앞 모디파이어까지 건 결과)를 차례로 붙입니다.
         * @details GPU 프로그램(`MeshModifierGPUProgram`)이 CPU 기준과 같은 상자를 싣는 데 쓴다 — 미리보기가 이미 도는 CPU 적용에서 공짜로 얻는다. 게임 스레드에서만 채운다.
         */
        vector<MeshModifierBounds>* _pListInputBounds{ nullptr };
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief `deformPoint` 한 번의 입력 정점입니다(모디파이어 공간). */
    struct MeshModifierPoint
    {
        float3 _position{ 0.0f, 0.0f, 0.0f };
        float3 _normal{ 0.0f, 1.0f, 0.0f }; ///< 단위 법선
        float2 _uv{ 0.0f, 0.0f };
        float4 _color{ 1.0f, 1.0f, 1.0f, 1.0f };
        uint32 _vertexIndex{ 0 };
        /** @brief 스택 입력(변형 전 원본) 위치를 모디파이어 공간으로 옮긴 값입니다. `_bHasOriginalPosition` 이 false 면 쓰지 않습니다. */
        float3 _originalPosition{ 0.0f, 0.0f, 0.0f };
        bool   _bHasOriginalPosition{ false }; ///< `needsOriginalPosition` 이 true 인 모디파이어에만 모디파이어 스택이 채운다
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 모디파이어 하나를 거는 동안의 문맥입니다. 경계 상자는 모디파이어 공간에서 측정한 입력 정점의 상자입니다. */
    struct MeshModifierContext
    {
        MeshModifierBounds            _bounds;
        float4x4                      _modifierToWorld{}; ///< 모디파이어 공간 → 월드
        float4x4                      _worldToModifier{}; ///< 월드 → 모디파이어 공간
        const MeshModifierEvaluation* _pEvaluation{ nullptr };
        uint32                        _axisIndex{ 1 };                   ///< 주축 성분 번호(0 = X, 1 = Y, 2 = Z)
        const vector<float3>*         _pListOriginalPosition{ nullptr }; ///< 변형 전 원본 위치(모디파이어 공간). `needsOriginalPosition` 일 때만 채운다

        float32                    getTime() const { return _pEvaluation != nullptr ? _pEvaluation->_time : 0.0f; }
        TaskManager*               getTaskManager() const { return _pEvaluation != nullptr ? _pEvaluation->_pTaskManager : nullptr; }
        const IMeshModifierInputs* getInputs() const { return _pEvaluation != nullptr ? _pEvaluation->_pInputs : nullptr; }
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class MeshModifier
     * @brief 모디파이어 하나의 공통 틀입니다. 파생은 매개변수 선언과 `deformPoint`(또는 `applyToMesh`)만 정합니다.
     */
    class MeshModifier
    {
    public:
        MeshModifier();
        virtual ~MeshModifier() = default;

        MeshModifier( const MeshModifier& )            = delete;
        MeshModifier& operator=( const MeshModifier& ) = delete;

        /** @brief 메시를 제자리에서 변형합니다. 꺼져 있으면 아무것도 하지 않습니다. */
        void apply( MeshModifierMesh& inoutMesh, const MeshModifierEvaluation& evaluation ) const;
        /** @brief 기즈모 중심을 메시 로컬 상자 가운데로 옮기고, 파생이 자기 크기 매개변수를 맞추게 합니다. */
        void fitToBounds( const MeshModifierBounds& localBounds );
        /** @brief 모든 매개변수를 선언의 기본값(항등)으로 되돌립니다. 기즈모는 그대로 둡니다. */
        void resetParams();
        /** @brief 메시 로컬 → 모디파이어 공간 행렬을 구합니다(월드 공간 · 기준 오브젝트 반영). */
        float4x4 computeLocalToModifier( const MeshModifierEvaluation& evaluation ) const;

        /** @brief 모디파이어 공간 점 하나를 옮깁니다. 기본은 그대로 둡니다. */
        virtual float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const;
        /**
         * @brief 메시 전체에 겁니다. 기본은 정점마다 `deformPoint` 를 부르고, 선택 가중치로 섞고, 법선을 야코비안으로 옮깁니다.
         * @details UV · 색 · 가중치 모디파이어, 정점 전체를 봐야 하는 모디파이어(Relax · Conform), 묶음(Deformable)이 재정의한다.
         */
        virtual void applyToMesh( MeshModifierMesh& inoutMesh, const MeshModifierContext& context ) const;
        /** @brief 매개변수 밖의 추가 상태(격자 제어점 등)를 저장 문서 객체에 씁니다. 기본은 없습니다. */
        virtual void writeExtraJSON( const JSONValue& object ) const;
        /** @brief 추가 상태를 읽습니다. 틀리면 false 이고 상태는 기본값으로 둡니다. 기본은 아무것도 읽지 않고 true 입니다. */
        [[nodiscard]] virtual bool readExtraJSON( const JSONValue& object );
        /** @brief 매개변수 값이 바깥(패널 · 저장 문서)에서 바뀐 뒤 부릅니다. 파생이 값에 묶인 상태를 다시 맞춥니다. */
        virtual void onParamsChanged();
        /**
         * @brief GPU 컴퓨트 미리보기로 옮길 수 있는 모디파이어면 true 입니다. 기본은 false(CPU 만)입니다.
         * @details 외부 입력(스플라인 · 다른 메시 · 포인트 캐시 · 텍스처)을 읽거나 `applyToMesh` 를 재정의한 모디파이어는 false 로 둔다.
         *          CPU 구현이 기준이다 — 베이크 · 테스트는 늘 CPU 로 돈다.
         */
        virtual bool supportsGPU() const { return false; }
        /** @brief GPU 프로그램에 싣는 곡선 매개변수의 표본 수입니다(4 의 배수. 셰이더 `SW_MODIFIER_CURVE_SAMPLES` 와 같다). */
        static constexpr uint32 kGPUCurveSampleCount = 32;
        /** @brief `MeshModifierPoint::_originalPosition` 이 필요하면 true 입니다. 기본은 false(복사 비용을 내지 않는다)입니다. */
        virtual bool needsOriginalPosition() const { return false; }
        /**
         * @brief GPU 구조 버퍼에 넣을 평탄한 매개변수를 @p outListValue 뒤에 붙입니다. `supportsGPU` 가 false 면 아무것도 붙이지 않고 false 입니다.
         * @details 기본 배치: 스칼라(Float · Int · Bool · Axis)는 선언 순서대로 float4 의 x 에 하나씩, Vector3 는 xyz, Curve 는 [0,1] 균등 표본 `kGPUCurveSampleCount` 개
         *          (float4 `kGPUCurveSampleCount / 4` 개 — `evaluateCurve` 로 뽑아 셰이더가 선형 보간한다), 그 밖의 종류는 건너뛴다.
         *          파생이 다른 배치가 필요하면 재정의한다. 공간 행렬 · 주축 · 상자는 모디파이어 스택이 따로 넣는다.
         */
        virtual bool packGPUParams( vector<float4>& outListValue ) const;
        /**
         * @brief 외부 입력(스플라인 · 텍스처 · 격자 제어점 · 단순 도형 표)을 `packGPUParams` 가 낸 매개변수 뒤에 이어 싣습니다. 기본은 아무것도 싣지 않고 true 입니다.
         * @details 입력은 별도 GPU 버퍼가 아니라 같은 프로그램 목록(`g_DeformPrograms`)의 기록 뒤쪽에 인라인으로 싣는다 - 기록의 매개변수 길이(`record.y`)가 가변이라
         *          새 바인딩 없이 네 백엔드가 같은 길로 읽고, 입력이 모디파이어 하나에 묶여 스택 순서 · 켜짐 끔이 그대로 따른다. 모양은 모디파이어마다 정한다 -
         *          첫 float4 의 x 는 "입력을 얻었다(1) · 못 얻었다(0)" 이고 0 이면 셰이더가 항등으로 둔다(CPU 가 입력을 못 얻을 때와 같다). 해석되는 좌표는 모두 모디파이어 공간이다.
         *          표본 수 · 해상도는 `kGPU*` 상수와 셰이더 `SW_MODIFIER_*` 매크로가 짝이다.
         * @param context 이 모디파이어를 거는 문맥(입력 상자 · 주축 · 월드 ↔ 모디파이어 행렬 · 평가)입니다. CPU `applyToMesh` 가 보는 것과 같다.
         */
        virtual bool packGPUInputs( const MeshModifierContext& context, vector<float4>& outListValue ) const;
        /** @brief 정점 UV 가 필요하면 true 입니다(텍스처 표본). 프로그램 끝에 UV 표가 붙고 셰이더 `ModifierContext::_uv` 가 읽습니다. 기본은 false 입니다. */
        virtual bool needsGPUVertexUV() const { return false; }
        /** @brief 텍스처 입력의 GPU 격자 한 변 텍셀 수입니다(셰이더 `SW_MODIFIER_TEXTURE_RESOLUTION` 과 같다. 4 의 배수). 높이/마스크 한 채널을 이 격자로 눌러 싣는다. */
        static constexpr uint32 kGPUTextureResolution = 64;
        /** @brief 스플라인 입력의 GPU 표본 구간 수입니다(셰이더 `SW_MODIFIER_PATH_SEGMENTS` 와 같다. 표본은 하나 더 - 위치 · 접선 · 단면 축을 싣는다). */
        static constexpr uint32 kGPUPathSegmentCount = 128;
        /** @brief 패널 추가 메뉴의 툴팁으로 보일 설명 한 줄(영문)입니다. 기본은 빈 글입니다. */
        virtual const utf8* getDescription() const { return ""; }

        const vector<MeshModifierParamDef>& getParamDefs() const { return _listParamDef; }
        uint32                              getParamCount() const { return static_cast<uint32>( _listParamDef.size() ); }
        /** @brief 이름의 매개변수 번호를 찾습니다. 없으면 false 입니다. */
        [[nodiscard]] bool            tryGetParamIndex( string_view name, uint32& outIndex ) const;
        const MeshModifierParamValue& getParamValue( uint32 paramIndex ) const { return _listParamValue[paramIndex]; }

        float32                getFloat( uint32 paramIndex ) const { return _listParamValue[paramIndex]._value._x; }
        int32                  getInt( uint32 paramIndex ) const;
        bool                   getBool( uint32 paramIndex ) const { return _listParamValue[paramIndex]._value._x != 0.0f; }
        uint32                 getAxisIndex( uint32 paramIndex ) const;
        const float3&          getVector3( uint32 paramIndex ) const { return _listParamValue[paramIndex]._value; }
        const vector<float32>& getNumberList( uint32 paramIndex ) const { return _listParamValue[paramIndex]._listNumber; }
        const string&          getText( uint32 paramIndex ) const { return _listParamValue[paramIndex]._text; }
        /** @brief 곡선 값을 구합니다. 키가 없으면 0, 하나면 그 값입니다. 범위 밖은 끝 키 값입니다. */
        float32 evaluateCurve( uint32 paramIndex, float32 x ) const;
        /** @brief 스칼라 값(Float · Int · Bool · Axis)을 선언 범위로 잘라 넣습니다. Int · Axis 는 반올림합니다. */
        void setScalar( uint32 paramIndex, float32 value );
        /** @brief 벡터 값을 선언 범위로 잘라 넣습니다. */
        void setVector3( uint32 paramIndex, const float3& value );
        /** @brief 선형 곡선 키를 넣습니다. x 는 [0,1] 로 자르고 x 순서로 정렬합니다(보간 · 접선을 정하려면 `setParamValue`). */
        void setCurveKeys( uint32 paramIndex, const vector<float2>& listKey );
        /** @brief 수 목록을 넣습니다. 원소는 선언 범위로 자르고, 길이는 `_stride` 의 배수로 자릅니다. */
        void setNumberList( uint32 paramIndex, const vector<float32>& listNumber );
        /** @brief 참조 · 경로 글을 넣습니다. */
        void setText( uint32 paramIndex, string_view text );
        /**
         * @brief 매개변수 값 전체를 종류에 맞는 칸만 읽어 선언 규칙(범위 · 정렬 · 항목 수 상한)으로 다듬어 넣습니다.
         * @details 종류별 setter 로 못 하는 값 — 보간 · 접선이 있는 곡선, 곡선 목록, 참조 목록 — 을 넣는 유일한 길이다(저장 문서 읽기 · 패널 편집이 쓴다).
         *          곡선은 키의 시간을 [0,1] 로 자르고 정렬하며 자동 접선을 다시 센다. 목록은 `_max` 개까지만 받는다. 값이 같은 종류의 칸만 본다.
         */
        void setParamValue( uint32 paramIndex, const MeshModifierParamValue& value );

        /** @brief 레지스트리 id 입니다(`SW_MESH_MODIFIER` 의 둘째 인자 — 레지스트리가 만들 때 채웁니다). */
        const string&            getTypeID() const { return _typeID; }
        void                     setTypeID( string_view typeID ) { _typeID = string( typeID ); }
        const string&            getName() const { return _name; }
        void                     setName( string_view name ) { _name = string( name ); }
        bool                     isEnabled() const { return _bEnabled; }
        void                     setEnabled( bool bEnabled ) { _bEnabled = bEnabled; }
        MeshModifierGizmo&       getGizmo() { return _gizmo; }
        const MeshModifierGizmo& getGizmo() const { return _gizmo; }
        /** @brief 참이면 기즈모가 메시 로컬이 아니라 월드(기준 오브젝트가 있으면 그 월드 변환) 기준입니다 — 여러 메시에 같은 워프를 거는 자리. */
        bool          isWorldSpace() const { return _bWorldSpace; }
        void          setWorldSpace( bool bWorldSpace ) { _bWorldSpace = bWorldSpace; }
        const string& getSpaceReference() const { return _spaceReference; }
        void          setSpaceReference( string_view reference ) { _spaceReference = string( reference ); }

    protected:
        /** @brief 매개변수 하나를 선언하고 기본값을 넣은 뒤 그 번호를 반환합니다. 생성자에서만 부릅니다. */
        uint32 addParam( const MeshModifierParamDef& def );
        /** @brief `fitToBounds` 가 기즈모를 맞춘 뒤 부릅니다. @p modifierBounds 는 모디파이어 공간 상자입니다. */
        virtual void onFitToBounds( const MeshModifierBounds& modifierBounds );

    private:
        vector<MeshModifierParamDef>   _listParamDef;
        vector<MeshModifierParamValue> _listParamValue;
        string                         _typeID;
        string                         _name;
        string                         _spaceReference; ///< 월드 공간일 때 기준 오브젝트 이름(비면 월드 원점)
        MeshModifierGizmo              _gizmo;
        bool                           _bEnabled;
        bool                           _bWorldSpace;
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 모디파이어가 함께 쓰는 수학 도우미입니다. */
    struct MeshModifierUtil
    {
        /** @brief 병렬 덩어리 하나의 정점 수입니다. 디스패치 비용(수십 us)보다 덩어리 일이 충분히 크게 잡는다. */
        static constexpr uint32 kChunkVertexCount = 4096;

        /** @brief 야코비안 법선 차분의 걸음을 상자 대각선에 곱하는 비율과 그 하한입니다(GPU 프로그램도 같은 걸음을 싣는다). */
        static constexpr float32 kJacobianStepRatio = 1e-3f;
        static constexpr float32 kMinJacobianStep   = 1e-5f;

        /** @brief 입력 상자(모디파이어 공간)의 야코비안 차분 걸음입니다. */
        static float32 computeJacobianStep( const MeshModifierBounds& bounds );
        /** @brief 정점 수를 `kChunkVertexCount` 로 나눈 덩어리 수입니다. */
        static uint32 computeChunkCount( size_t vertexCount );
        /**
         * @brief [0, count) 덩어리 번호를 작업 관리자 @p pTaskManager(평가의 `_pTaskManager`)로 나눠 돌립니다. nullptr 이면 현재 스레드가 한 번에 돕니다.
         * @details 본문은 자기 덩어리의 정점만 쓰고, 스크래치는 본문 안에서 가져야 한다(덩어리마다 다른 스레드일 수 있다).
         */
        static void runBlocks( TaskManager* pTaskManager, uint32 count, uint32 serialThreshold, const ParallelBlockDelegate& body );
        /**
         * @brief 텍스처 에셋 한 장의 한 채널(0 R · 1 G · 2 B · 3 A · 4 밝기)을 `MeshModifier::kGPUTextureResolution` 제곱 격자로 눌러 GPU 프로그램에 싣습니다.
         * @details 앞에 float4 하나(x = 얻었다 1 · 못 얻었다 0)가 붙고, 얻었을 때만 격자(텍셀 중심 (i + 0.5) / 해상도의 `trySampleTexture` 값을 행 우선으로 float4 하나에 넷)가 뒤따른다.
         *          셰이더가 같은 규약(텍셀 중심 · 반복 감김 · 이중선형)으로 읽으니 원본이 그 해상도 이하면 CPU 표본과 같고, 크면 격자 해상도로 무뎌진다.
         * @return 경로가 비었거나 입력이 없거나 표본에 실패하면 "못 얻었다" 한 칸만 싣고 false 입니다(호출자는 그래도 프로그램을 만들 수 있다 - 셰이더가 항등으로 둔다).
         */
        static bool packGPUTextureGrid( const MeshModifierContext& context, const string& assetPath, int32 channel, vector<float4>& outListValue );
        /** @brief 성분 번호(0 · 1 · 2)로 값을 읽습니다. */
        static float32 getComponent( const float3& value, uint32 axisIndex );
        /** @brief 성분 번호로 값을 씁니다. */
        static void setComponent( float3& inoutValue, uint32 axisIndex, float32 component );
        /** @brief 길이가 0 에 가까우면 @p fallback 을 냅니다. */
        static float3 normalizeOr( const float3& value, const float3& fallback );
        /** @brief 문맥 상자의 주축 길이입니다(0 에 가까우면 1). */
        static float32 getAxisLength( const MeshModifierContext& context );
        /** @brief 주축 위 비율 t(상자 아래 0 · 위 1)입니다. 자르지 않습니다. */
        static float32 getAxisFraction( const float3& point, const MeshModifierContext& context );
        /**
         * @brief 삼각형 목록(정점 3 개씩)의 면 법선으로 법선을 다시 만듭니다. 넓이가 0 인 삼각형은 옛 법선을 둡니다.
         * @details `.mesh` 는 인덱스 없는 삼각형 목록이라 정점마다 한 삼각형에만 속한다 — 그래서 각진 법선이 된다.
         */
        static void recomputeFaceNormals( const vector<float3>& listPosition, vector<float3>& inoutListNormal );
    };
} // namespace sw::editor
