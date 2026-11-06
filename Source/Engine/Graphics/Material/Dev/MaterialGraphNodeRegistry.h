/**
 * @file MaterialGraphNodeRegistry.h
 * @brief 머티리얼 그래프 노드 종류의 등록표입니다. 새 노드는 `SW_MATERIAL_GRAPH_NODE` 한 줄이고(`MaterialGraphNodes.cpp`), 컴파일러와 편집기 메뉴가 이 표를 읽습니다.
 * @details 글은 짧은 기호로 적는다(문법은 `docs/agents/MaterialGraph.md`). 등록은 정적 초기화에서 글 조각(`string_view`)만 나눠 힙을 쓰지 않는다.
 *          **등록은 엔진 안에만 둔다.** 팩 쿠킹은 게임 모듈을 올리기 전에 돌아(`HeadlessSubsystem::runCookTask`) 키트 · 게임의 등록을 보지 못한다 — 게임 전용 재사용은 하위 그래프로 한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 그래프 값의 형식입니다. 벡터 네 칸의 수는 값 그대로입니다(`getComponentCount`). */
    enum class MaterialGraphValueType : uint8
    {
        Invalid = 0,
        Float   = 1,
        Float2  = 2,
        Float3  = 3,
        Float4  = 4,
        Any,    ///< 이어진 입력의 가장 큰 형식을 따른다(float 는 어느 벡터로든 넓어진다). 다른 크기의 벡터가 섞이면 오류다
        String, ///< 이름 · 경로 설정(수치가 아니다)
    };

    /** @brief 노드 종류가 컴파일에서 맡는 역할입니다. */
    enum class MaterialGraphNodeRole : uint8
    {
        Snippet,     ///< HLSL 조각을 펼친다(상수 · 산술 · 파라미터 · 텍스처)
        Output,      ///< 머티리얼 출력. 입력 핀 이름이 표면 칸이다(`material_graph.hlsli` 의 `MaterialGraphOutput`)
        Subgraph,    ///< 하위 그래프를 펼쳐 넣는다. 핀은 그 그래프의 `GraphInput` · `GraphOutput` 이다
        GraphInput,  ///< 하위 그래프의 입력 핀 선언(설정 `Name` · `Type`)
        GraphOutput, ///< 하위 그래프의 출력 핀 선언(설정 `Name`, 입력 `Value`)
    };

    /** @brief 입력 핀 선언 하나입니다. `Name:type`, 필수면 `!`, 기본이 있으면 `=수치` 또는 `=HLSL 식`. */
    struct MaterialGraphPinDecl
    {
        string_view            _name;
        string_view            _default; ///< 잇지 않았을 때의 값. 비면 없다
        MaterialGraphValueType _type{ MaterialGraphValueType::Invalid };
        bool                   _bRequired{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다)
    };

    /** @brief 출력 핀 선언 하나입니다. `Name:type=HLSL 식`. 식의 `$Name` 은 입력 핀 또는 앞선 출력 핀, `%Name` 은 설정입니다. */
    struct MaterialGraphOutputDecl
    {
        string_view            _name;
        string_view            _expression;
        MaterialGraphValueType _type{ MaterialGraphValueType::Invalid };
    };

    /** @brief 설정 선언 하나입니다. `Name:type=기본값`. 잇지 못하는 값이고 노드 값(`_listValue`)에 적습니다. */
    struct MaterialGraphSettingDecl
    {
        string_view            _name;
        string_view            _default;
        MaterialGraphValueType _type{ MaterialGraphValueType::Invalid };
    };

    /**
     * @class MaterialGraphNodeKind
     * @brief 노드 종류 하나입니다. 파일 범위 정적 객체로 두면(`SW_MATERIAL_GRAPH_NODE`) 등록표에 오르고, 사라질 때 내려갑니다.
     */
    class SW_API MaterialGraphNodeKind
    {
    public:
        static constexpr uint32 kMaxInput   = 8;
        static constexpr uint32 kMaxOutput  = 6;
        static constexpr uint32 kMaxSetting = 6;

        /**
         * @param pName 종류 이름(파일의 `_kind`)
         * @param pCategory 메뉴 묶음
         * @param pInputs 입력 핀, `|` 로 나눈다
         * @param pOutputs 출력 핀, `|` 로 나눈다
         * @param pSettings 설정, `|` 로 나눈다
         * @param pParameter 이 노드가 머티리얼 칸이 되면 `HLSL 형식:프로퍼티 형식`(`float4:Color`). 설정 `Parameter` 가 칸 이름이고 식의 `%Parameter` 가 그 칸을 읽는다. 아니면 빈 글
         * @param role 컴파일에서 맡는 역할
         */
        MaterialGraphNodeKind( const utf8* pName, const utf8* pCategory, const utf8* pInputs, const utf8* pOutputs, const utf8* pSettings, const utf8* pParameter, MaterialGraphNodeRole role );
        ~MaterialGraphNodeKind();
        MaterialGraphNodeKind( const MaterialGraphNodeKind& )            = delete;
        MaterialGraphNodeKind& operator=( const MaterialGraphNodeKind& ) = delete;

        [[nodiscard]] const utf8*                     getName() const { return _pName; }
        [[nodiscard]] const utf8*                     getCategory() const { return _pCategory; }
        [[nodiscard]] MaterialGraphNodeRole           getRole() const { return _role; }
        [[nodiscard]] uint32                          getInputCount() const { return _inputCount; }
        [[nodiscard]] uint32                          getOutputCount() const { return _outputCount; }
        [[nodiscard]] uint32                          getSettingCount() const { return _settingCount; }
        [[nodiscard]] const MaterialGraphPinDecl&     getInput( uint32 index ) const { return _arrInput[index]; }
        [[nodiscard]] const MaterialGraphOutputDecl&  getOutput( uint32 index ) const { return _arrOutput[index]; }
        [[nodiscard]] const MaterialGraphSettingDecl& getSetting( uint32 index ) const { return _arrSetting[index]; }
        /** @brief 머티리얼 칸의 HLSL 형식입니다. 파라미터 노드가 아니면 빈 글입니다. */
        [[nodiscard]] string_view getParameterHLSLType() const { return _parameterHLSLType; }
        /** @brief 머티리얼 칸의 프로퍼티 형식(`MaterialPacking::stringToType` 의 이름)입니다. */
        [[nodiscard]] string_view getParameterPropertyType() const { return _parameterPropertyType; }
        /** @brief 입력 핀 번호입니다. 없으면 -1 입니다. */
        [[nodiscard]] int32 findInputIndex( string_view name ) const;
        /** @brief 설정입니다. 없으면 nullptr 입니다. */
        [[nodiscard]] const MaterialGraphSettingDecl* findSetting( string_view name ) const;
        /** @brief 선언 글이 문법에 맞지 않으면 그 까닭입니다. 맞으면 nullptr 입니다(`MaterialGraphNodeRegistry::collectProblems`). */
        [[nodiscard]] const utf8* getProblem() const { return _pProblem; }

    private:
        const utf8*              _pName;
        const utf8*              _pCategory;
        const utf8*              _pProblem;
        string_view              _parameterHLSLType;
        string_view              _parameterPropertyType;
        MaterialGraphPinDecl     _arrInput[kMaxInput];
        MaterialGraphOutputDecl  _arrOutput[kMaxOutput];
        MaterialGraphSettingDecl _arrSetting[kMaxSetting];
        uint32                   _inputCount;
        uint32                   _outputCount;
        uint32                   _settingCount;
        MaterialGraphNodeRole    _role;
    };

    /**
     * @struct MaterialGraphNodeRegistry
     * @brief 등록된 노드 종류의 표입니다.
     */
    struct SW_API MaterialGraphNodeRegistry
    {
        /** @brief 이름의 종류입니다. 없으면 nullptr 입니다. */
        [[nodiscard]] static const MaterialGraphNodeKind* find( string_view name );
        /** @brief 등록 순서의 종류 목록을 @p outListKind 에 담습니다(편집기 메뉴). */
        static void collectKinds( vector<const MaterialGraphNodeKind*>& outListKind );
        /** @brief 문법이 틀린 등록을 글로 @p outListProblem 에 더합니다(단위 테스트가 본다). */
        static void collectProblems( vector<string>& outListProblem );
        /** @brief 형식 이름(`float3`)의 형식입니다. 모르면 `Invalid` 입니다. */
        [[nodiscard]] static MaterialGraphValueType parseValueType( string_view name );
        /** @brief 형식의 HLSL 이름입니다(`float3`). */
        [[nodiscard]] static const utf8* getHLSLTypeName( MaterialGraphValueType type );
    };
} // namespace sw

/** @brief HLSL 조각 노드 하나를 등록합니다(파일 범위, `namespace sw` 안). */
#define SW_MATERIAL_GRAPH_NODE( name, category, inputs, outputs, settings )                  \
    static const ::sw::MaterialGraphNodeKind sw_materialGraphNode_##name                     \
    {                                                                                        \
        #name, category, inputs, outputs, settings, "", ::sw::MaterialGraphNodeRole::Snippet \
    }

/** @brief 머티리얼 칸이 되는 노드 하나를 등록합니다. @p parameter 는 `HLSL 형식:프로퍼티 형식` 입니다. */
#define SW_MATERIAL_GRAPH_PARAMETER_NODE( name, category, inputs, outputs, settings, parameter )    \
    static const ::sw::MaterialGraphNodeKind sw_materialGraphNode_##name                            \
    {                                                                                               \
        #name, category, inputs, outputs, settings, parameter, ::sw::MaterialGraphNodeRole::Snippet \
    }
