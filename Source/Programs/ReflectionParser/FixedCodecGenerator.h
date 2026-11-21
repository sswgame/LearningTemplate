/**
 * @file FixedCodecGenerator.h
 * @brief 고정 배치형(B) 코덱 생성기입니다. `REFLECT` 타입마다 서술자 표(`::sw::FixedFieldDesc`)와 정규 문자열 · 의존 목록을 냅니다.
 * @details 명세는 `Source/Engine/Serialization/Fixed/README.md` 3 · 4 절, 런타임은 `Engine/Serialization/Fixed/FixedLayout.h` 입니다.
 *          쓰기 · 읽기 · 비교 본문은 내지 않는다 — 런타임 해석기 하나가 표로 한다(언리얼 `FRepLayout` 자리). 예전에는 타입마다 직선 코드를 냈다.
 *
 *          - 사용자는 B 를 선언하지 않습니다. 필드 타입이 인코딩 표 안이면 코덱을 만들고, 아니면 이유를 등록합니다(오류가 아닙니다).
 *          - 범위 · 길이는 기존 `PROPERTY` 키 `Min` · `Max` 와 `Step` · `MaxCount` 에서 읽습니다. `Min` · `Max` 는 앞선 필드 이름도 받습니다.
 *          - 키를 잘못 적은 것은 헤더 경로 · 줄 · 필드와 함께 생성 오류로 멈춥니다. 범위 필드가 뒤에 있음, float32 로 정확히 나타낼 수 없는
 *            정수 경계, 실수 필드를 범위로 쓰는 정수 필드, 찾지 못한 범위 이름, 숫자가 아닌 필드의 `Min` · `Max`, 맞지 않는 `Step` · `MaxCount` 입니다.
 *          - 같은 헤더의 중첩 타입은 공개 필드뿐이면 부모 표 안에 펼칩니다(`Inline` 칸). 바깥 필드 이름 범위는 이때만 풀립니다.
 *            펼칠 수 없으면 그 타입의 코덱을 실행 시간에 찾아 부릅니다(`Codec` 칸).
 *          - **복제 스키마**: `PROPERTY( Replicated )` 칸이 있는 타입은 그 칸만 든 두 번째 표와 정규 문자열(`R=` 로 시작)을 내고 `FixedCodec::_pReplicated` 에 겁니다
 *            (런타임 `ReplicatedLayout`). 타입 전체가 적격이 아니어도 낸다. `Replicated` 칸의 형식이 인코딩 표 밖이거나, 범위 이름이 앞선 `Replicated` 칸이
 *            아니면 생성 오류다 — 전체 코덱의 "이유 등록" 과 달리 `Replicated` 는 명시한 요청이다(docs/NEXT.md 5-4 N1).
 */
#pragma once
#include "Engine/EngineMinimal.h"

#include "Programs/ReflectionParser/CodeEmit.h"
#include "Programs/ReflectionParser/FixedCodecEmitState.h"
#include "Programs/ReflectionParser/ParsedReflection.h"

namespace sw
{
    /** @brief 헤더 하나의 타입마다 B 코덱을 계획하고 그 표를 냅니다. 계획(`makePlan`)을 먼저 부릅니다. */
    class FixedCodecGenerator
    {
    public:
        FixedCodecGenerator( const ParsedHeader& header, const string& sourceFilePath );

        /** @brief 타입마다 적격 판정 · 서술자 칸을 만듭니다. 생성 오류가 있으면 모두 알리고 false 입니다. */
        [[nodiscard]] bool makePlan();

        /** @brief `RegisterType` 안, `registry.registerClass` 앞에 서술자 표와 코덱을 `info._pFixedCodec` 에 거는 줄들을 씁니다. */
        void emitRegistration( CodeEmit& emit, size_t typeIndex ) const;
        /** @brief `<FQN> | <정규 문자열 또는 이유>` 한 줄(끝 줄바꿈 없음)입니다. 스탬프의 `schema` 줄로 적히고 `FixedSchemas.gen.txt` 가 그 줄을 모읍니다. */
        [[nodiscard]] string makeSchemaLine( size_t typeIndex ) const;
        /** @brief `--dump` 의 한 줄입니다. */
        string makeDumpLine( size_t typeIndex ) const;

    private:
        /** @brief 칸 묶음 하나(전체 코덱 또는 복제 스키마)의 표 재료입니다. */
        struct TablePlan
        {
            vector<FixedCodecNode>         _listRootNode;
            vector<const ParsedFixedEnum*> _listEnum;       ///< 등록 값 표를 낼 열거형입니다
            vector<pair<string, string>>   _listDependency; ///< (필드 이름, FQN). 실행 시간에 찾는 코덱입니다
        };

        struct TypePlan
        {
            string                         _canonical;
            string                         _reason;
            vector<FixedCodecNode>         _listRootNode;
            vector<const ParsedFixedEnum*> _listEnum;            ///< 등록 값 표를 낼 열거형입니다
            vector<pair<string, string>>   _listDependency;      ///< (필드 이름, FQN). 실행 시간에 찾는 코덱입니다. 기반이 있으면 0 번입니다
            string                         _replicatedCanonical; ///< 복제 스키마의 정규 문자열입니다. 비면 `Replicated` 칸이 없습니다
            TablePlan                      _replicated;
            string                         _unresolvedName; ///< 자기 타입에서 찾지 못한 범위 이름입니다(바깥 타입이 펼치며 풀어야 합니다)
            string                         _unresolvedMember;
            uint32                         _unresolvedLine;
            uint8                          _bEligible         : 1;
            uint8                          _bResolvedByParent : 1; ///< 같은 헤더의 부모가 펼치며 바깥 범위 이름을 풀었습니다
            uint8                          _bHasBase          : 1;
            [[maybe_unused]] uint8         _reserved          : 5;

            TypePlan() noexcept;
        };

        /** @brief @p type 의 `Replicated` 칸만으로 복제 스키마를 계획합니다. 생성 오류가 있으면 알리고 false 입니다. */
        [[nodiscard]] bool planReplicated( const ParsedTypeInfo& type, TypePlan& outPlan ) const;
        /**
         * @brief 서술자 표 · 의존 표를 @p pTablePrefix 이름(`s_arrFixedField` · `s_arrFixedDependency` 꼴)으로 쓰고, 코덱 생성자의 (표, 개수) · (의존 표, 개수) 인자 글을 돌려줍니다.
         * @param listEmittedEnum 이 등록 함수 안에서 이미 낸 열거자 값 표입니다(같은 이름을 두 번 정의하지 않는다).
         */
        static pair<string, string> emitTables( CodeEmit& emit, const vector<FixedCodecNode>& listRootNode, const vector<const ParsedFixedEnum*>& listEnum,
                                                const vector<pair<string, string>>& listDependency, const string& tablePrefix, vector<string>& listEmittedEnum );
        /** @brief 칸 트리를 평평한 표 글로 폅니다. 묶음의 자식은 이어진 칸이고, 부모 칸 글 뒤에 첫 칸 번호(묶음이면 개수도)가 붙습니다. */
        static void flattenNodes( const vector<FixedCodecNode>& listNode, vector<string>& outListEntry );

    private:
        const ParsedHeader& _header;
        string              _sourceFilePath;
        vector<TypePlan>    _listPlan;
    };
} // namespace sw
