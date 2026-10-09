/**
 * @file GimmickNodeRegistry.h
 * @brief 기믹 노드 종류 등록부 — 종류 이름 → 입력 · 출력 포트, 매개변수 표, 상태 크기, 걸음 함수입니다.
 * @details 회로 데이터는 종류를 **이름으로** 고릅니다. 코드에는 이 등록부(이름 붙은 연산의 표)만 있고, 새 센서 · 연산자 · 액추에이터는 종류 하나를
 *          등록하면 데이터에서 씁니다. 포트 값은 모두 참/거짓 수준(level)이고, "On…" 출력은 한 걸음만 참인 펄스입니다. 노드의 상태는 고정 크기의
 *          실수 · 정수 칸뿐이라 회로 전체 상태를 바이트로 저장 · 해시 · 되감기할 수 있습니다(`GimmickCircuit`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/RegistrationList.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 노드가 회로에서 맡는 몫입니다. 센서는 바깥(씬 · 시험)이 값을 넣고, 액추에이터는 바깥이 상태를 읽어 오브젝트에 겁니다. */
    enum class GimmickNodeCategory : uint8
    {
        Sensor = 0,
        Operator,
        Actuator
    };

    /** @brief 매개변수 값의 모양입니다. */
    enum class GimmickParamType : uint8
    {
        Number = 0, ///< 실수 하나
        Vector,     ///< 실수 1..4 개("0 3 0")
        Text        ///< 글 그대로(이름 · 경로 · 목록) — 종류의 초기화가 해석한다
    };

    /** @brief 매개변수 하나의 이름 · 모양 · 기본값입니다. */
    struct GimmickParamSpec
    {
        hashed_string    _name{};
        string           _defaultText{};
        GimmickParamType _type{ GimmickParamType::Number };
    };
} // namespace sw

namespace sw
{
    /** @brief 노드 하나의 매개변수를 해석한 값입니다(칸 번호 = 종류의 매개변수 표 순서). */
    struct GimmickParamValue
    {
        float4 _vector{};
        string _text{};
    };
} // namespace sw

namespace sw
{
    /**
     * @struct GimmickNodeContext
     * @brief 걸음 함수 · 초기화 함수가 보는 노드 한 개의 창입니다. 상태 칸은 회로의 저장 대상이고, 나머지는 이번 걸음의 입력입니다.
     */
    struct SW_GF_API GimmickNodeContext
    {
        const GimmickParamValue* _pParam{ nullptr };
        float32*                 _pFloatState{ nullptr };
        int32*                   _pIntState{ nullptr };
        string*                  _pError{ nullptr };     ///< 초기화만 — 매개변수가 틀렸으면 이유를 적는다
        float32                  _sensorValue{ 0.0f };   ///< 센서의 지금 수준(인원 · 무게 · 거리)
        float32                  _sensorImpulse{ 0.0f }; ///< 지난 걸음 뒤로 쌓인 충격(피해 · 사용 횟수) — 이번 걸음이 먹는다
        float32                  _pathLength{ 0.0f };    ///< 무버가 따라갈 길의 길이(바깥이 묶는다, 0 이면 매개변수 `length`)
        float32                  _stepTime{ 0.0f };
        uint32                   _stepIndex{ 0 };
        uint32                   _inputBits{ 0 };
        uint32                   _previousInputBits{ 0 };
        uint32                   _connectedInputBits{ 0 };
        uint32                   _outputBits{ 0 }; ///< 들어올 때는 지난 걸음의 출력, 나갈 때는 이번 걸음의 출력

        bool isInput( uint32 port ) const { return ( _inputBits & ( 1u << port ) ) != 0; }
        bool wasInput( uint32 port ) const { return ( _previousInputBits & ( 1u << port ) ) != 0; }
        bool isConnected( uint32 port ) const { return ( _connectedInputBits & ( 1u << port ) ) != 0; }
        bool isRising( uint32 port ) const { return isInput( port ) && wasInput( port ) == false; }
        bool wasOutput( uint32 port ) const { return ( _outputBits & ( 1u << port ) ) != 0; }
        /** @brief 연결됐으면 그 수준, 아니면 @p bUnconnected 입니다(자동 시작 · 처음부터 켜짐 같은 매개변수가 기본을 정한다). */
        bool          isInputHigh( uint32 port, bool bUnconnected ) const { return isConnected( port ) ? isInput( port ) : bUnconnected; }
        float32       getNumber( uint32 param ) const { return _pParam[param]._vector._x; }
        float3        getVector( uint32 param ) const { return float3{ _pParam[param]._vector._x, _pParam[param]._vector._y, _pParam[param]._vector._z }; }
        const string& getText( uint32 param ) const { return _pParam[param]._text; }
        /** @brief 초 → 걸음 수(반올림, 최소 @p minSteps)입니다. 시간 규칙은 모두 걸음 수로 세어 누적 오차가 없습니다. */
        int32 toSteps( float32 seconds, int32 minSteps ) const;
        /** @brief 출력 비트 하나를 세우거나 내립니다. */
        void setOutput( uint32 port, bool bValue );
    };
} // namespace sw

namespace sw
{
    /** @brief 노드 종류 하나입니다. */
    struct GimmickNodeKind
    {
        using InitializeFunc = bool ( * )( GimmickNodeContext& context );
        using StepFunc       = void ( * )( GimmickNodeContext& context );

        hashed_string            _name{};
        vector<hashed_string>    _listInput{};  ///< 포트 번호 = 자리(최대 32)
        vector<hashed_string>    _listOutput{}; ///< 포트 번호 = 자리(최대 32)
        vector<GimmickParamSpec> _listParam{};
        InitializeFunc           _pInitialize{ nullptr }; ///< 처음 상태를 매개변수로 채운다. 매개변수가 틀렸으면 `_pError` 에 적고 false
        StepFunc                 _pStep{ nullptr };
        /**
         * @brief 고리를 끊는 노드(지연)만 — 걸음 끝에 이번 걸음의 입력을 받아 둡니다. 이 노드의 걸음 함수는 입력을 보지 않고 저장된 것으로
         *        출력을 내므로 다른 노드보다 먼저 돌고, 이 노드로 들어가는 배선은 평가 순서를 묶지 않습니다(이것을 지나는 고리는 허용).
         */
        StepFunc            _pCommit{ nullptr };
        uint16              _floatStateCount{ 0 };
        uint16              _intStateCount{ 0 };
        GimmickNodeCategory _category{ GimmickNodeCategory::Operator };

        int32 findInput( const hashed_string& name ) const;
        int32 findOutput( const hashed_string& name ) const;
        int32 findParam( const hashed_string& name ) const;
        bool  breaksCycle() const { return _pCommit != nullptr; }
    };
} // namespace sw

namespace sw
{
    /**
     * @class GimmickNodeRegistry
     * @brief 종류 이름 → 정의입니다. `getDefault()` 가 내장 종류(센서 · 연산자 · 액추에이터)를 든 등록부이고, 게임은 그것을 복사해 종류를 더한 등록부로
     *        회로를 지을 수 있습니다.
     */
    class SW_GF_API GimmickNodeRegistry
    {
    public:
        /** @brief 내장 종류를 모두 든 등록부입니다(처음 부를 때 짓고 바꾸지 않는다). */
        static const GimmickNodeRegistry& getDefault();

        /** @brief 종류를 더합니다. 이름이 이미 있거나 포트가 32 개를 넘으면 false 입니다. */
        [[nodiscard]] bool registerKind( const GimmickNodeKind& kind );
        /** @brief 이름의 종류입니다. 없으면 nullptr 입니다. */
        const GimmickNodeKind*         findKind( const hashed_string& name ) const;
        const vector<GimmickNodeKind>& getKinds() const { return _registry.getItems(); }

        /** @brief 내장 종류를 @p inoutRegistry 에 더합니다(`getDefault` 가 쓴다). */
        static void registerBuiltinKinds( GimmickNodeRegistry& inoutRegistry );

    private:
        NameRegistry<GimmickNodeKind> _registry{};
    };
} // namespace sw
