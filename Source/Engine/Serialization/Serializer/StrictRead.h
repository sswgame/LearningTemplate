/**
 * @file StrictRead.h
 * @brief 리플렉션 구조체를 엄격하게 읽은 결과입니다. 모르는 칸 · 읽지 못한 값 · 범위 밖 값 · 빠진 필수 칸을 모두 담고, 알리는 로그 id 는 한 벌입니다.
 * @details 만드는 쪽은 `XMLSerializer::readStrict` · `JSONSerializer::readStrict` 둘뿐이다. 설정 · 에셋 · 문화권 · 오브젝트 상태 읽기는 결과를 로그(`report`)나
 *          오류 값(`describe`)으로 옮기기만 한다. 언리얼 `FJsonObjectConverter` · 유니티 `JsonUtility.FromJsonOverwrite` 처럼 읽기 판정을 한 곳에 둔다.
 *          호출부마다 판정 순서와 로그 id 를 다시 적으면 한쪽만 고쳐 갈린다(`Config.KeyUnknown` 과 `Serialization.OrphanValue` 가 그렇게 갈렸다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Serialization/Format/SchemaMigrate.h"

namespace sw
{
    struct TypeInfo;

    /**
     * @class StrictRead
     * @brief 엄격한 읽기 하나의 결과입니다. 문제가 하나도 없을 때만 읽은 것입니다.
     * @details 범위 밖 값은 읽기 공통 끝(`PropertyConstraints::isWithinDeclaredRanges`)이 칸마다 이미 알렸다 — 여기서는 결과(`Rejected`)만 든다.
     */
    class [[nodiscard]] SW_API StrictRead
    {
    public:
        StrictRead( const TypeInfo& typeInfo, SoftReadResult result, vector<SchemaOrphanValue> listOrphan, vector<string> listMissing );

        /** @brief 본문을 읽었고, 모르는 칸 · 읽지 못한 값 · 범위 밖 값 · 빠진 필수 칸이 없으면 true 입니다. */
        [[nodiscard]] bool isRead() const noexcept;
        /**
         * @brief 문제를 모두 오류 로그로 알리고 `isRead()` 를 돌려줍니다. @p sourceName 은 어느 파일 · 오브젝트인지입니다.
         * @details 로그 id: 본문을 못 읽음 `Serialization.NotRead`, 모르는 칸 · 읽지 못한 값 `Serialization.OrphanValue`, 빠졌거나 빈 필수 칸 `Serialization.RequiredMissing`.
         *          범위 밖 값 하나가 다른 문제를 삼키지 않게 끝까지 알린다.
         */
        [[nodiscard]] bool report( string_view sourceName ) const;
        /** @brief 문제를 한 줄로 적습니다(오류 값으로 옮기는 쪽). 문제가 없으면 빈 글입니다. */
        [[nodiscard]] string describe() const;

        [[nodiscard]] SoftReadResult                   getResult() const noexcept { return _result; }
        [[nodiscard]] const vector<SchemaOrphanValue>& getOrphans() const noexcept { return _listOrphan; }
        [[nodiscard]] const vector<string>&            getMissingRequired() const noexcept { return _listMissing; }

    private:
        const TypeInfo*           _pTypeInfo;
        vector<SchemaOrphanValue> _listOrphan;
        vector<string>            _listMissing; ///< 빠졌거나 빈 필수 칸의 경로(`_listBone[2]._parentIndex`)
        SoftReadResult            _result;
    };
} // namespace sw
