/**
 * @file ComponentStateCodec.h
 * @brief PROPERTY 밖의 컴포넌트 런타임 상태(`serializeState` · `restoreState`)를 오브젝트 하나씩 뜨고 되돌리는 형식마다의 코덱과 그 등록입니다.
 * @details 리플렉션 PROPERTY 는 오브젝트 상태가 이미 옮긴다. 보통 클래스로 든 시뮬레이션(지형 편집 · 부지 배치 · 디렉터)은 그 밖이라, 오브젝트를 내렸다 다시 짓는 길(스트리밍 셀 ·
 *          데이터 레이어 묶음 · 하위 레벨)과 그 세이브는 이 코덱으로 함께 싣는다(`Scene::captureCellNodes` → `SceneObjectNode::_componentStateBytes`). 언리얼 `UObject::Serialize` 오버라이드가
 *          레벨 스트리밍 · 세이브에 함께 실리는 자리다. 늘 올라 있는 오브젝트의 세이브 · 핫 리로드는 게임 프레임워크의 `ComponentStateStore` 가 같은 함수로 싣는다 — 등록 한 줄이 둘을 정한다
 *          (`StatefulComponentRegistrar` · `GameStateSnapshot::registerStatefulComponent`, 엔진 형식은 `SW_COMPONENT_STATE_CODEC`).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Registry/RegistrationList.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include <utility>

namespace sw
{
    struct TypeInfo;

    class Component;
    class GameObject;

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
        /**
         * @brief 떠 둔 상태를 다시 지은 @p object 의 컴포넌트에 돌려줍니다(등록을 마친 뒤 — 시작 전이면 컴포넌트가 보관했다가 시작할 때 적용한다).
         * @details 컴포넌트가 없거나(데이터가 바뀌었다) 코덱이 내려갔으면 그 상태를 경고하고 버린다. 바이트가 깨졌으면 오류를 남기고 남은 상태를 버린다.
         */
        static void restoreObject( GameObject& object, const vector<uint8>& bytes );

    private:
        template <typename TComponent>
        static void writeState( Component& component, vector<uint8>& outBytes )
        {
            Archive archive;
            StateIO::writeObject( static_cast<TComponent&>( component ), archive );
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
