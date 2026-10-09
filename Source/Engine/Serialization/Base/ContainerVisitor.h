/**
 * @file ContainerVisitor.h
 * @brief 리플렉션 컨테이너(시퀀스 · 맵 · 중첩 · 소유 포인터)를 도는 순회 하나와, 형식(XML · JSON · 바이너리)이 구현하는 쓰기 · 읽기 계약입니다.
 * @details 원소가 어떤 모양으로 실리는지(중첩 컨테이너 · 소유 포인터 · 값 구조체 · 스칼라)는 순회가 컨테이너마다 한 번 정하고, 형식은 그 모양의 원소
 *          하나를 어떻게 적고 읽는지만 답합니다. 새 컨테이너 종류는 래퍼(`ReflectionContainers.h`)와 이 순회 한 곳만 고칩니다.
 */
#pragma once
#include "Core/Delegate/Delegate.h"

#include "Engine/EngineMinimal.h"

namespace sw
{
    struct NestedContainerInfo;
    struct TypeInfo;

    class SerializeContext;

    /** @brief 컨테이너 원소(시퀀스 원소 · 맵 값)가 실리는 모양입니다. 컨테이너 하나에 한 번 정합니다(`ContainerElementPlan`). */
    enum class ContainerElementKind : uint8
    {
        NestedContainer, ///< 원소가 또 컨테이너입니다(`NestedContainerInfo::_elementNested`).
        OwnedPointer,    ///< 다형 소유 포인터(`T*`) — 런타임 타입 + 본문. 시퀀스 원소만입니다.
        ValueObject,     ///< 반사된 값 구조체(`SerializerUtil::findNestedObjectType`).
        Scalar,          ///< 그 밖의 값(텍스트 · 바이너리 핸들러 · enum).
    };

    /** @brief 원소가 놓이는 자리입니다. 형식마다 자리별 표기가 다릅니다(XML 은 시퀀스 원소를 `<item>` 으로, 맵 값을 `<entry>` 안에 바로 적는다). */
    enum class ContainerSlot : uint8
    {
        SequenceElement,
        MapValue,
    };

    /** @brief 원소 하나 · 컨테이너 하나를 읽은 결과입니다. 뒤로 갈수록 나쁩니다(`ContainerVisitor::mergeResult`). */
    enum class ContainerReadResult : uint8
    {
        Read,         ///< 모두 읽었습니다.
        FieldFailed,  ///< 일부 원소 · 항목을 못 읽었습니다 — 그것만 빼고 다음 자리로 갔습니다(칸은 실패로 알린다).
        StreamBroken, ///< 다음 자리를 모릅니다 — 읽기를 멈췄습니다.
    };
} // namespace sw

namespace sw
{
    /** @brief 컨테이너 하나의 원소 계획입니다. 원소마다 타입 표를 다시 찾지 않게 순회 전에 한 번 만듭니다. */
    struct ContainerElementPlan
    {
        const TypeInfo*      _pElementType{ nullptr }; ///< `ValueObject` 일 때 원소 타입입니다.
        ContainerElementKind _kind{ ContainerElementKind::Scalar };

        /**
         * @brief @p nested 의 원소를 @p slot 자리에 둘 때의 계획을 만듭니다. 판정 순서는 중첩 → 소유 포인터 → 값 구조체 → 스칼라입니다.
         * @details 맵 값은 소유 포인터가 되지 않습니다 — 세 형식 모두 맵 값의 다형을 싣지 않고, 그 이름은 스칼라 길로 흐릅니다.
         */
        static ContainerElementPlan make( const NestedContainerInfo& nested, ContainerSlot slot, const SerializeContext& ctx );
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 형식 하나가 컨테이너를 적는 방법입니다. `ContainerVisitor::write` 가 이 순서로 부릅니다.
     * @details beginSequence → (원소마다 write* 하나, 원소가 컨테이너면 beginNestedContainer … endNestedContainer) → endSequence,
     *          beginMap → (항목마다 beginMapEntry → 값 하나 → endMapEntry) → endMap.
     */
    class IContainerWriter
    {
    public:
        IContainerWriter()                                         = default;
        virtual ~IContainerWriter()                                = default;
        IContainerWriter( const IContainerWriter& )                = default;
        IContainerWriter& operator=( const IContainerWriter& )     = default;
        IContainerWriter( IContainerWriter&& ) noexcept            = default;
        IContainerWriter& operator=( IContainerWriter&& ) noexcept = default;

        /** @brief 시퀀스를 엽니다. @p elementCount 는 널 소유 포인터까지 센 원소 수입니다. */
        virtual void beginSequence( size_t elementCount ) = 0;
        virtual void endSequence()                        = 0;
        /** @brief 맵을 엽니다. */
        virtual void beginMap( size_t entryCount ) = 0;
        virtual void endMap()                      = 0;
        /** @brief 맵 항목 하나를 열고 키를 적습니다. */
        virtual void beginMapEntry( const void* pKey, const hashed_string& keyTypeName ) = 0;
        virtual void endMapEntry()                                                       = 0;
        /** @brief @p slot 자리의 원소가 컨테이너일 때 그 컨테이너를 적기 전후에 부릅니다. */
        virtual void beginNestedContainer( ContainerSlot slot ) = 0;
        virtual void endNestedContainer( ContainerSlot slot )   = 0;
        /** @brief 소유 포인터 원소 하나를 적습니다. @p pObject 는 널일 수 있습니다(건너뛸지 빈 자리를 적을지는 형식이 정한다). */
        virtual void writeOwnedPointer( const void* pObject ) = 0;
        /** @brief 값 구조체 원소 하나를 적습니다. */
        virtual void writeValueObject( const void* pValue, const hashed_string& typeName, const TypeInfo& typeInfo, ContainerSlot slot ) = 0;
        /** @brief 스칼라 원소 하나를 적습니다. */
        virtual void writeScalar( const void* pValue, const hashed_string& typeName, ContainerSlot slot ) = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief 원소 하나를 읽게 하는 콜백입니다. 인자는 읽는 순서의 순번(0 부터)입니다. 형식이 그 원소의 자리로 옮긴 뒤 부릅니다. */
    using ContainerElementVisitDelegate = Delegate<ContainerReadResult( size_t elementIndex )>;

    /**
     * @brief 형식 하나가 컨테이너를 읽는 방법입니다. `ContainerVisitor::read` 가 이 순서로 부릅니다.
     * @details beginSequence · beginMap 이 지금 자리의 값을 열고, forEachElement 가 원소(맵이면 항목)마다 자리를 옮겨 콜백을 부릅니다. 콜백 안에서
     *          read* 가 그 자리의 값을 읽습니다 — 원소가 또 컨테이너면 그 자리에서 begin* 이 다시 불립니다.
     *          실패의 뜻은 세 형식이 같습니다: 자리를 알면 FieldFailed(그 원소만 뺀다), 모르면 StreamBroken(멈춘다).
     */
    class IContainerReader
    {
    public:
        IContainerReader()                                         = default;
        virtual ~IContainerReader()                                = default;
        IContainerReader( const IContainerReader& )                = default;
        IContainerReader& operator=( const IContainerReader& )     = default;
        IContainerReader( IContainerReader&& ) noexcept            = default;
        IContainerReader& operator=( IContainerReader&& ) noexcept = default;

        /** @brief 지금 자리의 값을 시퀀스로 엽니다. @p outCountHint 는 미리 잡을 원소 수(모르면 0)입니다. 모양이 다르면 Read 가 아닙니다. */
        [[nodiscard]] virtual ContainerReadResult beginSequence( size_t& outCountHint ) = 0;
        /** @brief 지금 자리의 값을 맵으로 엽니다. */
        [[nodiscard]] virtual ContainerReadResult beginMap( size_t& outCountHint ) = 0;
        /** @brief 방금 연 컨테이너의 원소 · 항목마다 그 자리로 옮겨 @p visit 를 부르고 결과를 합칩니다. StreamBroken 이 나오면 멈춥니다. */
        [[nodiscard]] virtual ContainerReadResult forEachElement( const ContainerElementVisitDelegate& visit ) = 0;
        /** @brief 지금 항목의 키를 @p pKey(만들어 둔 키)에 읽습니다. */
        [[nodiscard]] virtual ContainerReadResult readMapKey( void* pKey, const hashed_string& keyTypeName ) = 0;
        /**
         * @brief 지금 원소를 다형 소유 포인터로 읽습니다.
         * @details 만들고 소유자에 붙이는 것은 팩토리(`SerializeContext::createOwnedPointer`)이고, 모르는 타입은 원문을 맡깁니다(`keepOpaqueElement`).
         */
        [[nodiscard]] virtual ContainerReadResult readOwnedPointer() = 0;
        /** @brief 지금 원소 · 맵 값을 값 구조체로 읽습니다. */
        [[nodiscard]] virtual ContainerReadResult readValueObject( void* pValue, const hashed_string& typeName, const TypeInfo& typeInfo, ContainerSlot slot ) = 0;
        /** @brief 지금 원소 · 맵 값을 스칼라로 읽습니다. */
        [[nodiscard]] virtual ContainerReadResult readScalar( void* pValue, const hashed_string& typeName, ContainerSlot slot ) = 0;
        /** @brief 넣을 칸이 없는 원소(고정 배열보다 많다)를 지나갑니다. 원소 크기를 모르는 형식은 StreamBroken 입니다. */
        [[nodiscard]] virtual ContainerReadResult skipElement( size_t elementIndex ) = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief 컨테이너 순회 하나입니다. 형식은 `IContainerWriter` · `IContainerReader` 로 들어옵니다. */
    struct SW_API ContainerVisitor
    {
        /** @brief @p pContainer 를 @p writer 의 형식으로 적습니다. 컨테이너나 래퍼가 없으면 아무것도 적지 않습니다. */
        static void write( const void* pContainer, const NestedContainerInfo& nested, IContainerWriter& writer, const SerializeContext& ctx );

        /**
         * @brief @p reader 의 지금 자리에서 컨테이너를 읽어 @p pContainer 를 채웁니다.
         * @details 소유 포인터 원소의 컨테이너는 비우지 않습니다 — 원소를 넣는 것은 팩토리(소유자)이고, 비우면 팩토리가 방금 붙인 것까지 날아갑니다.
         *          그 밖의 컨테이너는 모양을 확인한 뒤 비우고 채웁니다. 원소를 넣는 방법은 컨테이너가 정합니다(`appendElement`).
         */
        [[nodiscard]] static ContainerReadResult read( void* pContainer, const NestedContainerInfo& nested, IContainerReader& reader, const SerializeContext& ctx );

        /** @brief 두 결과 중 나쁜 쪽입니다(StreamBroken > FieldFailed > Read). */
        static ContainerReadResult mergeResult( ContainerReadResult lhs, ContainerReadResult rhs ) noexcept;
    };
} // namespace sw
