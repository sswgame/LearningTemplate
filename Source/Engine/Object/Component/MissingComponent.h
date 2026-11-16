/**
 * @file MissingComponent.h
 * @brief 타입이 올라와 있지 않은 컴포넌트의 저장된 값을 들고 있다가 그대로 다시 쓰는 자리입니다(유니티 "Missing Script").
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/SerializeContext.h"

namespace sw
{
    /**
     * @class MissingComponent
     * @brief 읽을 때 타입을 모른 컴포넌트(모듈이 안 뜸 · 지운 타입)의 원문입니다. 저장할 때 같은 형식이면 원문을 그대로 다시 씁니다.
     * @details 이것이 없으면 게임 모듈이 안 뜬 채 에디터가 씬을 저장할 때 그 컴포넌트의 값이 **영영** 사라진다.
     *          다른 형식으로 저장될 때(플레이 스냅샷 · 되돌리기 = 바이너리)는 이 컴포넌트 자체가 저장되고, 다시 원래 형식으로 저장할 때 원문이 돌아간다.
     *          틱하지 않고 그리지 않습니다. 모듈이 올라온 뒤 씬을 다시 열면 원래 컴포넌트로 읽힙니다.
     */
    REFLECT( Category = "Core", DisplayName = "Missing Component", Tooltip = "A component whose type is not loaded; its saved data is kept and written back", Meta = "Icon=Missing" )
    class SW_API MissingComponent : public Component
    {
    public:
        REFLECT_BODY();
        MissingComponent();
        virtual ~MissingComponent() override = default;

        /** @brief 맡을 원문을 적습니다(타입 이름 · 형식 · 원문). */
        void setOriginalElement( const SerializeContext::OpaqueElementView& element );
        /** @brief 맡은 원문을 돌려줍니다. 아무것도 맡지 않았으면 false 입니다. 돌려준 보기는 이 컴포넌트가 살아 있는 동안만 유효합니다. */
        [[nodiscard]] bool tryGetOriginalElement( SerializeContext::OpaqueElementView& outElement ) const;
        /** @brief 원래 타입 이름입니다. */
        const string& getOriginalTypeName() const { return _originalTypeName; }

        /**
         * @brief 이 오브젝트의 컴포넌트를 가리켰지만 대상이 이 자리(모르는 타입)라 풀지 못한 참조의 키(`타입#n`) 하나를 맡습니다. 같은 키는 한 번만 맡습니다.
         * @details 읽은 핸들은 키의 해시만 들고 있어, 다시 글로 저장할 때 원래 키 글이 필요하다(`ObjectStateBatch::resolveComponentReference` 가 맡기고
         *          저장 처리기가 해시로 찾는다). 유니티 Missing Script 가 fileID 를 그대로 들고 있어 참조가 남는 것과 같은 몫이다.
         */
        void addReferencedKey( string_view key );
        /** @brief 맡은 참조 키들입니다. */
        const vector<string>& getReferencedKeys() const { return _listReferencedKey; }

    private:
        PROPERTY( ReadOnly, Category = "Missing", DisplayName = "Missing Type", Tooltip = "The type this component had when it was saved" )
        string _originalTypeName;
        /** @brief 원문 형식(`SerializeContext::OpaqueFormat` 의 값)입니다. */
        PROPERTY( HideInInspector )
        uint8 _originalFormat;
        /** @brief XML · JSON 원문입니다. */
        PROPERTY( HideInInspector )
        string _originalText;
        /** @brief 바이너리 본문입니다. */
        PROPERTY( HideInInspector )
        vector<uint8> _originalBytes;
        /** @brief 풀지 못한 참조의 키 글입니다. 바이너리 스냅샷(플레이 · 되돌리기)을 지나도 남게 저장합니다. */
        PROPERTY( HideInInspector )
        vector<string> _listReferencedKey;
    };
} // namespace sw
