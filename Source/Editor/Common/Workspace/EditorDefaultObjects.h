/**
 * @file EditorDefaultObjects.h
 * @brief 컴포넌트 타입마다 기본값 인스턴스 하나를 만들어 둡니다(언리얼 CDO — Class Default Object). 인스펙터가 "기본값과 다름" 을 판정합니다(ImGui 없음).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/EditorExports.h"

namespace sw
{
    struct PropertyInfo;
    struct TypeInfo;

    class Component;
    class GameObjectManager;
} // namespace sw

namespace sw::editor
{
    /**
     * @class EditorDefaultObjects
     * @brief 씬 밖의 오브젝트 매니저에 컴포넌트 타입마다 하나씩 기본 생성 컴포넌트를 둡니다. 처음 물을 때 만듭니다.
     * @details 기본값은 생성자가 정하므로(이 저장소는 초기값을 생성자에 둔다) 리플렉션 메타의 기본값 글보다 정확합니다.
     *          모듈이 언로드되면(핫 리로드) 그 모듈 코드의 컴포넌트가 남지 않게 `clear` 로 모두 지웁니다(EditorModuleUnloadListener).
     */
    class SW_EDITOR_API EditorDefaultObjects
    {
    public:
        EditorDefaultObjects();
        ~EditorDefaultObjects();
        EditorDefaultObjects( const EditorDefaultObjects& )            = delete;
        EditorDefaultObjects& operator=( const EditorDefaultObjects& ) = delete;

        /** @brief @p type 의 기본 인스턴스입니다. 만들 수 없는 타입(추상 · 컴포넌트가 아님)이면 nullptr 입니다. */
        const Component* findDefault( const TypeInfo& type );
        /** @brief 만들어 둔 기본 인스턴스를 모두 지웁니다. */
        void clear();

        /**
         * @brief 프로퍼티 값이 기본값과 같은지 묻습니다. @p pDefaultInstance 가 있으면 프로퍼티 글로 비교하고, 없으면 리플렉션 메타의 기본값 글과 비교합니다.
         * @return 기본값을 모르면(인스턴스도 메타도 없음) true 입니다 — 표시할 차이가 없다.
         */
        static bool isDefaultValue( const PropertyInfo& prop, const void* pInstance, const void* pDefaultInstance );

    private:
        unique_ptr<GameObjectManager> _pManager;
        vector<const TypeInfo*>       _listType;
        vector<Component*>            _listDefault;
    };
} // namespace sw::editor
