/**
 * @file PropertyConstraints.h
 * @brief `PROPERTY` 가 선언한 제약(`Min` · `Max` · `Required`)을 읽은 값이 지키는지 봅니다. 세 형식의 읽기 끝과 설정 · 데이터 로드가 함께 씁니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Serialization/Serializer/PropertyReadRollback.h"
#include "Engine/Serialization/Serializer/ValueSerializer.h"

namespace sw
{
    /** @brief 필수 판정의 범위입니다 — 원소가 부모를 가리키면(`PROPERTY( InheritParent )` 칸이 적힘) `RequiredAtRoot` 칸은 물려받은 값이다. */
    enum class RequiredScope : uint8
    {
        Root,      ///< 부모가 없다 — `Required` 와 `RequiredAtRoot` 칸이 모두 필수다.
        Inherited, ///< 부모를 가리킨다 — `Required` 칸만 필수다.
    };

    struct PropertyConstraints
    {
        /**
         * @brief 필수 칸(`PROPERTY( Required )`)이 빠졌는지 봅니다 — 적히지 않았거나, 이름 · 경로 형식(`isNameValueType`)인데 적힌 글이 비었다.
         * @details XML · JSON 의 `collectMissingRequired` 가 함께 쓰는 한 판정이다(한쪽만 바뀌면 두 포맷이 같은 데이터를 다르게 통과시킨다).
         *          빈 이름은 형식만 있고 값이 없는 칸이다 — 에디터가 빈 칸을 속성으로 적어도 데이터가 값을 준 것이 아니다.
         * @param pWrittenText 적힌 글(스칼라 칸). 적히지 않았으면 nullptr, 객체 · 목록으로 적혔으면 빈 값이 아닌 것으로 본다(@p bWritten 만 본다).
         * @param scope 원소가 부모를 가리키는지(`findRequiredScope`). 부모를 모르는 읽기(객체 · 인스턴스 구조체)는 `Root` 다.
         */
        SW_API static bool isRequiredValueMissing( const PropertyInfo& prop, bool bWritten, const string_view* pWrittenText, RequiredScope scope );
        /**
         * @brief 원소의 필수 판정 범위를 정합니다 — `InheritParent` 칸이 비어 있지 않게 적혔으면 `Inherited` 입니다.
         * @param fnWrittenText 칸 색인 → 적힌 글(적히지 않았으면 빈 글). 형식마다 원소를 읽는 법만 다르다.
         */
        template <typename TList, typename FnWrittenText>
        static RequiredScope findRequiredScope( const TList& listProperty, FnWrittenText&& fnWrittenText )
        {
            for ( size_t propIndex = 0; propIndex < listProperty.size(); ++propIndex )
            {
                if ( listProperty[propIndex]._metadata->_bInheritParent == SW_TRUE && fnWrittenText( propIndex ).empty() == false )
                    return RequiredScope::Inherited;
            }
            return RequiredScope::Root;
        }
        /** @brief 이름 · 경로 형식(hashed_string · TagID · DataPath · AssetID · CatalogKey · AssetRef<…>)인지 봅니다 — 빈 값이 "없음" 인 형식이다. 글(`string`)은 빈 값도 값이라 넣지 않는다. */
        SW_API static bool isNameValueType( const hashed_string& typeName );

        /**
         * @brief `Min`/`Max` 밖 숫자 칸마다 "경로 = 값 is outside [아래, 위]" 한 줄을 모읍니다(로드 시점 계약 검사 — 설정 JSON 과 데이터 XML 이 함께 씁니다).
         * @details 구조체 칸과 시퀀스(vector, 고정 배열) 원소는 늘 들어가 본다(구조체 원소는 그 칸, 숫자 원소는 그 프로퍼티의 범위). 비트필드 · 접근자 칸은 보지 않는다.
         *          맵과 중첩 컨테이너는 원소 경로를 하나로 정할 수 없어 보지 않는다.
         *          범위를 어디까지 보는지 부르는 쪽이 고르지 않는다. 고르게 하면 읽기 경로마다 계약이 달라진다(설정 JSON · 데이터 XML · 세이브가 같은 `Min` · `Max` 를 다르게 지켰다).
         *          경계는 인스펙터와 같은 함수로 푼다(`PropertyMetadata::computeAllowedRange`) — 숫자 경계와 필드 경계(`Min = "-_halfSize"`)를 함께 지킨다.
         *          @p instance 는 @p typeInfo 객체의 주소다(필드 경계를 이 객체에서 읽는다).
         */
        SW_API static void collectRangeErrors( const TypeInfo& typeInfo, ObjectAddress instance, const string& prefix, vector<string>& outListError,
                                               const vector<RestoredPropertyValue>* pListRestored = nullptr );
        /**
         * @brief `collectRangeErrors` 의 칸 하나입니다. @p instance 는 @p prop 을 가진 객체의 주소입니다.
         * @details 파일에 적힌 칸만 보는 읽기(`XMLFields::readProperties`)가 부른다 — 적지 않은 칸은 코드의 초기값이라 데이터 오류가 아니다.
         */
        SW_API static void collectPropertyRangeErrors( const PropertyInfo& prop, ObjectAddress instance, const string& prefix, vector<string>& outListError,
                                                       const vector<RestoredPropertyValue>* pListRestored = nullptr );
        /**
         * @brief 읽기를 끝낸 인스턴스가 `Min` · `Max` 를 지키는지 봅니다. 어기면 칸마다 오류 로그를 남기고 false 입니다.
         * @details 세 포맷(바이너리 · JSON · XML)의 읽기 공통 끝이 부릅니다 — 호출부마다 따로 부르면 부르지 않는 경로의 값이 범위 밖인 채 성공으로 돌아온다(A-19).
         *          위반이 없으면 글을 만들지 않고 첫 위반에서 멈추는 빠른 길로 끝납니다.
         */
        [[nodiscard]] SW_API static bool isWithinDeclaredRanges( const TypeInfo& typeInfo, ObjectAddress instance );
        /**
         * @brief 로그 없이 범위만 봅니다. 남이 보낸 상태 바이트(롤백 · 넷 · 세이브)의 읽기용. 거절은 읽기 실패로만 알린다(손 `io.check` 와 같은 계약, 깨진 바이트로 오류 로그를 쏟게 하지 않는다).
         */
        [[nodiscard]] SW_API static bool areValuesWithinDeclaredRanges( const TypeInfo& typeInfo, ObjectAddress instance );
        /**
         * @brief 위와 같되 소프트 읽기가 읽기 전 값으로 되돌린 칸(@p listRestored — 어느 깊이든)은 보지 않습니다. 세 형식(JSON · XML · 바이너리)의 소프트 읽기 끝이 모두 이것을 지난다.
         * @details 되돌린 칸은 파일 값이 아니라 읽기 전 값이고(바탕 층 형식의 새 객체면 0 — 범위 밖), 그 칸의 잘못은 orphan 이 이미 알렸다.
         *          보면 모르는 키 · 읽지 못한 값 하나가 범위 밖 오류 묶음으로 덧나 알려진다. 목록은 `PropertyReadRollback::getRestoredValues` 가 준다.
         */
        [[nodiscard]] SW_API static bool isWithinDeclaredRanges( const TypeInfo& typeInfo, ObjectAddress instance, const vector<RestoredPropertyValue>& listRestored );
    };
} // namespace sw
