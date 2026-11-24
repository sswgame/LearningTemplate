/**
 * @file ComponentStateCodec.h
 * @brief PROPERTY 밖의 컴포넌트 런타임 상태(`serializeState` · `restoreState`)를 오브젝트 하나씩 뜨고 되돌리는 형식마다의 코덱과 그 등록입니다.
 * @details 리플렉션 PROPERTY 는 오브젝트 상태가 이미 옮긴다. 보통 클래스로 든 시뮬레이션(지형 편집 · 부지 배치 · 디렉터)은 그 밖이라, 오브젝트를 내렸다 다시 짓는 길(스트리밍 셀 ·
 *          데이터 레이어 묶음 · 하위 레벨), 씬을 넘는 오브젝트(`SceneManager::markPersistent`), 세이브 · 핫 리로드 봉투(`GameStateSnapshot`)가 **모두 이 코덱 하나로** 싣는다 —
 *          길마다 다른 목록을 두면 한 길만 빠진 상태가 생긴다. 언리얼 `UObject::Serialize` 재정의가 레벨 스트리밍 · 세이브에 함께 실리는 자리다.
 *          등록: 게임 형식은 `StatefulComponentRegistrar` · `GameStateSnapshot::registerStatefulComponent`, 엔진 형식은 `SW_COMPONENT_STATE_CODEC`.
 *
 *          컴포넌트 쪽 약속(템플릿이 이름으로 부른다):
 *          - `void serializeState( StateIO& io )`: 지금 상태를 쓴다(`SW_DECLARE_STATE_IO` 가 낸다). 첫 값은 그 컴포넌트의 형식 버전으로 둔다.
 *          - `void restoreState( vector<uint8>&& bytes )`: 다시 만든 컴포넌트가 받는다. 아직 `onBeginPlay` 전이면 보관했다가 시작할 때 적용한다. 읽지 못하면 알리고 새 판으로 시작한다.
 *          - `const vector<uint8>& getPendingStateBytes() const`: 시작 전에 보관한 바이트다(보관하는 컴포넌트만 둔다 — `DeferredStateRestore` 를 쓰면 그 틀이 대신한다).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Registry/RegistrationList.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include <type_traits>
#include <utility>

namespace sw
{
    struct TypeInfo;

    class Component;
    class GameObject;

    namespace ComponentStateCodecTraits
    {
        /** @brief @p T 가 시작 전에 받은 상태를 보이는 `getPendingStateBytes()` 를 가졌는지 봅니다(C++17 이라 `requires` 를 쓰지 않는다). */
        template <typename T, typename = void>
        struct HasPendingState : std::false_type
        {
        };
        template <typename T>
        struct HasPendingState<T, std::void_t<decltype( std::declval<const T&>().getPendingStateBytes() )>> : std::true_type
        {
        };
    } // namespace ComponentStateCodecTraits

    /**
     * @struct ComponentStateCodec
     * @brief 형식 하나의 코덱입니다. 등록한 쪽의 저장소에 있고 등록이 빠질 때까지 산다(정적 초기화 · 게임 인스턴스 생성자가 올리고 씬이 읽는다, 게임 스레드).
     */
    struct SW_API ComponentStateCodec
    {
        const utf8* _pTypeName;
        const TypeInfo* ( *_pGetType )(); ///< 정적 초기화 순서를 피하려고 쓸 때 묻는다
        void ( *_pWrite )( Component& component, vector<uint8>& outBytes );
        void ( *_pRestore )( Component& component, vector<uint8>&& bytes );

        /** @brief @p TComponent(`serializeState( StateIO& )` · `restoreState( vector<uint8>&& )`)의 코덱을 만듭니다. */
        template <typename TComponent>
        static ComponentStateCodec make( const utf8* pTypeName )
        {
            return ComponentStateCodec{ pTypeName, &TComponent::StaticType, &ComponentStateCodec::writeState<TComponent>, &ComponentStateCodec::restoreState<TComponent> };
        }

        /** @brief 코덱을 더합니다. 같은 코덱을 두 번 등록하면 무시합니다(true). 빈 코덱은 거절하고 false 입니다. 같은 형식의 다른 코덱(인스턴스가 둘인 테스트)은 먼저 것을 쓴다. */
        [[nodiscard]] static bool add( const ComponentStateCodec* pCodec );
        /** @brief 코덱을 뺍니다. 올라 있지 않으면 할 일이 없다. */
        static void remove( const ComponentStateCodec* pCodec );
        /**
         * @brief @p object 의 컴포넌트 가운데 코덱이 있는 것마다 지금 상태를 @p outBytes 에 뜹니다(비우고 채운다). 파생 형식은 올린 기반 코덱으로도 뜬다. 뜰 것이 없으면 빈 바이트입니다.
         * @details 짝짓기는 (코덱 형식 이름, 지역 id 키 — `ComponentStableKey`)다.
         */
        static void captureObject( const GameObject& object, vector<uint8>& outBytes );
        /** @brief @p type 의 컴포넌트를 뜨는 코덱이 하나라도 있으면 true 입니다 — 큰 씬을 뜨는 쪽이 형식마다 한 번만 묻는다. */
        [[nodiscard]] static bool hasCodecFor( const TypeInfo& type );
        /**
         * @brief 떠 둔 상태를 다시 지은 @p object 의 컴포넌트에 돌려줍니다(등록을 마친 뒤 — 시작 전이면 컴포넌트가 보관했다가 시작할 때 적용한다).
         * @details 컴포넌트가 없거나(데이터가 바뀌었다) 코덱이 내려갔으면 그 상태를 경고하고 버린다. 바이트가 깨졌으면 오류를 남기고 남은 상태를 버린다.
         */
        static void restoreObject( GameObject& object, const vector<uint8>& bytes );

    private:
        template <typename TComponent>
        static void writeState( Component& component, vector<uint8>& outBytes )
        {
            TComponent& typed = static_cast<TComponent&>( component );
            if constexpr ( ComponentStateCodecTraits::HasPendingState<TComponent>::value )
            {
                // 보관한 동안은 그 바이트가 곧 상태다 — 지금 값을 쓰면 세이브를 기본값으로 덮는다.
                if ( typed.getPendingStateBytes().empty() == false )
                {
                    outBytes = typed.getPendingStateBytes();
                    return;
                }
            }
            Archive archive;
            StateIO::writeObject( typed, archive );
            archive.writeData( outBytes );
        }
        template <typename TComponent>
        static void restoreState( Component& component, vector<uint8>&& bytes )
        {
            static_cast<TComponent&>( component ).restoreState( std::move( bytes ) );
        }
    };

    /** @brief `SW_COMPONENT_STATE_CODEC` 이 두는 정적 등록자입니다(공통 틀 `StaticRegistrar` — 모듈이 내려가면 뺀다). */
    SW_DEFINE_STATIC_REGISTRAR( ComponentStateCodecRegistrar, const ComponentStateCodec*, &ComponentStateCodec::add, &ComponentStateCodec::remove );
} // namespace sw

/** @brief 엔진 컴포넌트 @p Type 의 상태 코덱을 등록합니다(그 컴포넌트의 .cpp, 파일 범위, `namespace sw` 안). */
#define SW_COMPONENT_STATE_CODEC( Type )                                                                                            \
    static const ::sw::ComponentStateCodec          sw_componentStateCodec_##Type = ::sw::ComponentStateCodec::make<Type>( #Type ); \
    static const ::sw::ComponentStateCodecRegistrar sw_componentStateCodecRegistrar_##Type{ &sw_componentStateCodec_##Type, ::sw::SourceLocation::current() };
