/**
 * @file MissingComponent.h
 * @brief 타입이 올라와 있지 않은 컴포넌트의 저장된 값을 들고 있다가 그대로 다시 쓰는 자리입니다(유니티 "Missing Script").
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Core/SerializeContext.h"

namespace sw
{
    /**
     * @class MissingComponent
     * @brief 읽을 때 타입을 모른 컴포넌트(모듈이 안 뜸 · 지운 타입)의 원문입니다. 저장할 때 같은 형식이면 원문을 그대로 다시 씁니다.
     * @details 이것이 없으면 게임 모듈이 안 뜬 채 에디터가 씬을 저장할 때 그 컴포넌트의 값이 **영영** 사라진다.
     *          다른 형식으로 저장될 때(플레이 스냅샷 · 되돌리기 = 바이너리)는 이 컴포넌트 자체가 저장되고, 다시 원래 형식으로 저장할 때 원문이 돌아간다.
     *          틱하지 않고 그리지 않습니다. 모듈이 올라온 뒤 씬을 다시 열면 원래 컴포넌트로 읽힙니다.
     */
    REFLECT( Category = "Core", DisplayName = "Missing Component", Tooltip = "A component whose type is not loaded; its saved data is kept and written back" )
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
    };
} // namespace sw
