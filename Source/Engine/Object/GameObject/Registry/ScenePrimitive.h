/**
 * @file ScenePrimitive.h
 * @brief 프리미티브 레지스트리가 아는 유일한 타입 둘입니다. 코어가 기능 컴포넌트를 모르고도 그릴 것을 등록받게 하는 데이터 슬롯입니다.
 *
 * [왜 기반 타입인가]
 * 레지스트리는 등록 · 해제 · 더티 표시에 번호 하나와 트랜스폼 칸만 씁니다. 그것만 코어에 두면 레지스트리가 메시 헤더를 include 할
 * 이유가 없어집니다(`Object/Component/3D` 는 그래픽스를 알고, 코어가 그것을 include 하면 코어 → 기능 화살표가 생깁니다).
 * 가상 함수를 더하지 않습니다. 트랜스폼 전파가 레지스트리에 더티를 알리는 핫 패스는 지금처럼 슬롯 번호로 가고, 간접 호출이 없습니다
 * (`SceneTransformPage::_arrPrimitiveIndex`). 메시를 읽어야 하는 곳(`GPUSceneBuilder`)은 기능 층 위라서 그쪽에서 `static_cast` 합니다.
 *
 * [불변식: 들어오는 것은 메시 계열뿐]
 * 두 기반의 생성자는 private 이고 friend 는 `MeshComponent` 와 `MeshInstanceBatch` 하나씩입니다. 그래서 레지스트리의 등록 함수가 기반 타입을
 * 받아도 실제 타입은 그 둘(과 그 파생)뿐이고, 빌더의 `static_cast` 가 형식으로 맞습니다. 다른 그릴 것을 더하려면 이 friend 를 바꾸고
 * 빌더가 타입을 가르는 법을 함께 정해야 합니다(조용히 잘못된 캐스트가 되지 않습니다).
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class MeshComponent;
    class MeshInstanceBatch;
    class PrimitiveRegistry;

    /**
     * @class ScenePrimitive
     * @brief 레지스트리에 하나의 프리미티브 번호로 들어가는 씬 컴포넌트입니다. 번호 하나만 가집니다.
     * @details 언리얼의 `UPrimitiveComponent` 자리이지만 렌더 상태는 없습니다. 렌더 상태는 파생 타입(`MeshComponent`)이 가집니다.
     */
    REFLECT( Abstract, Category = "Rendering 3D", DisplayName = "Scene Primitive", Tooltip = "Registry slot of a drawable scene component" )
    class SW_API ScenePrimitive : public SceneComponent
    {
        /// 번호를 적는 유일한 주체입니다.
        friend class PrimitiveRegistry;
        /// 생성할 수 있는 유일한 타입입니다(파일 머리 "불변식").
        friend class MeshComponent;

    public:
        REFLECT_BODY();

        /** @brief 등록되지 않은 프리미티브의 번호입니다. */
        static constexpr uint32 kInvalidPrimitiveIndex = invalid_index::kUint32;

        /** @brief 기본 소멸자입니다. */
        virtual ~ScenePrimitive() override = default;

    private:
        /** @brief 등록되지 않은 상태로 만듭니다. `MeshComponent` 만 부릅니다. */
        ScenePrimitive();

        /** @brief 레지스트리 번호입니다. 등록되지 않았으면 kInvalidPrimitiveIndex 입니다. */
        uint32 getPrimitiveIndex() const { return _primitiveIndex; }
        /** @brief 레지스트리 번호를 적고, 트랜스폼 칸에도 같은 번호를 적습니다. 틱 뒤 적용 · 플러시는 이 번호로 레지스트리에 바로 더티를 찍습니다. */
        void setPrimitiveIndex( uint32 index );

        /** @brief 레지스트리 번호입니다. 등록되지 않았으면 kInvalidPrimitiveIndex 입니다. */
        uint32 _primitiveIndex;
    };
} // namespace sw

namespace sw
{
    /**
     * @class ScenePrimitiveBatch
     * @brief 씬 컴포넌트 없이 항목 N 개를 프리미티브 번호 N 개로 등록하는 배치의 기반입니다. 레지스트리 포인터와 항목 수만 가집니다.
     * @details 항목 수는 만들 때 정해집니다. 소멸자가 등록되어 있으면 레지스트리에서 빠지므로 파생 타입은 따로 풀지 않습니다.
     *          소멸자는 가상이 아니고 protected 입니다. 기반 포인터로 지우는 일이 없게 합니다.
     */
    class SW_API ScenePrimitiveBatch
    {
        /// 등록 상태를 적는 유일한 주체입니다.
        friend class PrimitiveRegistry;
        /// 생성할 수 있는 유일한 타입입니다(파일 머리 "불변식").
        friend class MeshInstanceBatch;

    public:
        ScenePrimitiveBatch( const ScenePrimitiveBatch& )            = delete;
        ScenePrimitiveBatch& operator=( const ScenePrimitiveBatch& ) = delete;

        /** @brief 항목 수입니다. */
        uint32 getCount() const { return _entryCount; }
        /** @brief 레지스트리에 들어 있으면 true 입니다. */
        bool isRegistered() const { return _pRegistry != nullptr; }

    protected:
        /** @brief 등록되어 있으면 레지스트리에서 빠집니다(`unregisterBatch`). */
        ~ScenePrimitiveBatch();

        /**
         * @brief 등록되어 있으면 레지스트리에서 뺍니다. 여러 번 불러도 됩니다.
         * @details 파생 타입은 소멸자 **맨 앞에서** 부릅니다. 기반 소멸자는 파생 멤버(항목 목록)가 사라진 뒤에 돌므로, 그때까지 레지스트리에
         *          남아 있으면 그 사이에 항목을 읽는 쪽이 사라진 메모리를 봅니다.
         */
        void unregisterBatch();

        /** @brief 항목 하나를 레지스트리에 더티로 알립니다. 등록 전이면 아무것도 하지 않습니다. */
        void markEntryDirty( uint32 index );

    private:
        /** @brief 항목 @p entryCount 개로, 등록되지 않은 상태로 만듭니다. `MeshInstanceBatch` 만 부릅니다. */
        explicit ScenePrimitiveBatch( uint32 entryCount );

        PrimitiveRegistry* _pRegistry;  ///< 등록된 레지스트리. 레지스트리가 먼저 사라지면 레지스트리가 비웁니다
        uint32             _firstEntry; ///< 레지스트리의 인스턴스 항목 목록에서 첫 항목 자리. 레지스트리가 적습니다
        uint32             _entryCount; ///< 항목 수. 만들 때 정해집니다
    };
} // namespace sw
