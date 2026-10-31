/**
 * @file FixedCodecGenerator.h
 * @brief 고정 배치형(B) 코덱 생성기입니다. `REFLECT` 타입마다 쓰기 · 읽기 · 비교 · 스키마 해시 함수를 직선 코드로 만듭니다.
 * @details 명세는 `Source/Engine/Serialization/Fixed/README.md` 3 · 4 절, 런타임은 `Engine/Serialization/Fixed/FixedLayout.h` 입니다.
 *
 *          - 사용자는 B 를 선언하지 않습니다. 필드 타입이 인코딩 표 안이면 코덱을 만들고, 아니면 이유를 등록합니다(오류가 아닙니다).
 *          - 범위 · 길이는 기존 `PROPERTY` 키 `Min` · `Max` 와 `Step` · `MaxCount` 에서 읽습니다. `Min` · `Max` 는 앞선 필드 이름도 받습니다.
 *          - 키를 잘못 적은 것은 헤더 경로 · 줄 · 필드와 함께 생성 오류로 멈춥니다. 범위 필드가 뒤에 있음, float32 로 정확히 나타낼 수 없는
 *            정수 경계, 실수 필드를 범위로 쓰는 정수 필드, 찾지 못한 범위 이름, 숫자가 아닌 필드의 `Min` · `Max`, 맞지 않는 `Step` · `MaxCount` 입니다.
 *          - 같은 헤더의 중첩 타입은 공개 필드뿐이면 부모 코덱 안에 펼쳐 씁니다. 바깥 필드 이름 범위는 이때만 풀립니다.
 *            펼칠 수 없으면 그 타입의 코덱을 실행 시간에 찾아 부릅니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"

#include "Programs/ReflectionParser/CodeEmit.h"
#include "Programs/ReflectionParser/ParsedReflection.h"

namespace sw
{
    /** @brief 헤더 하나의 타입마다 B 코덱을 계획하고 그 코드를 냅니다. 계획(`makePlan`)을 먼저 부릅니다. */
    class FixedCodecGenerator
    {
    public:
        FixedCodecGenerator( const ParsedHeader& header, const string& sourceFilePath );

        /** @brief 타입마다 적격 판정 · 코드를 만듭니다. 생성 오류가 있으면 모두 알리고 false 입니다. */
        [[nodiscard]] bool makePlan();

        /** @brief `RegisterType` 안, `registry.registerClass` 앞에 코덱 테이블을 `info._pFixedCodec` 에 거는 줄들을 씁니다. */
        void emitRegistration( CodeEmit& emit, size_t typeIndex ) const;
        /** @brief `Registrar<T>` 안의 정적 멤버(쓰기 · 읽기 · 비교 · 해시 · 도우미) 글입니다. 적격이 아니면 비어 있습니다. */
        const string& getMemberText( size_t typeIndex ) const;
        /** @brief `<FQN> | <정규 문자열 또는 이유>` 한 줄(끝 줄바꿈 없음)입니다. 스탬프의 `schema` 줄로 적히고 `FixedSchemas.gen.txt` 가 그 줄을 모읍니다. */
        [[nodiscard]] string makeSchemaLine( size_t typeIndex ) const;
        /** @brief `--dump` 의 한 줄입니다. */
        string makeDumpLine( size_t typeIndex ) const;

    private:
        struct TypePlan
        {
            string                       _canonical;
            string                       _reason;
            string                       _memberText;
            vector<pair<string, string>> _listDependency; ///< (필드 이름, FQN). 실행 시간에 찾는 코덱입니다
            string                       _unresolvedName; ///< 자기 타입에서 찾지 못한 범위 이름입니다(바깥 타입이 펼치며 풀어야 합니다)
            string                       _unresolvedMember;
            uint32                       _unresolvedLine;
            uint8                        _bEligible         : 1;
            uint8                        _bResolvedByParent : 1; ///< 같은 헤더의 부모가 펼치며 바깥 범위 이름을 풀었습니다
            [[maybe_unused]] uint8       _reserved          : 6;

            TypePlan() noexcept;
        };

        const ParsedHeader& _header;
        string              _sourceFilePath;
        vector<TypePlan>    _listPlan;
    };
} // namespace sw
