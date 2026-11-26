/**
 * @file FeatureDemoCommandUtil.h
 * @brief 기능 시연 컴포넌트의 개발 명령이 함께 쓰는 대상 찾기 · 답 적기입니다.
 */
#pragma once
#include "Core/String/Name/hashed_string.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Scene/SceneManager.h"

namespace sw
{
    /** @struct FeatureDemoCommandUtil @brief 시연 명령 도우미입니다(전부 static). */
    struct FeatureDemoCommandUtil
    {
        /** @brief 활성 씬에서 이름 @p objectName 의 열린 시연(@p TDemo)입니다. 없으면 까닭을 @p outReply 에 적고 nullptr 입니다. */
        template <typename TDemo>
        [[nodiscard]] static TDemo* findStarted( const string& objectName, string& outReply )
        {
            GameObject* pObject = engine::findActiveObject( hashed_string( objectName ) );
            TDemo*      pDemo   = pObject != nullptr ? pObject->getComponent<TDemo>() : nullptr;
            if ( pDemo != nullptr && pDemo->isStarted() )
                return pDemo;
            reply( outReply, "no object named '%#' with a started demo component", objectName );
            return nullptr;
        }

        /** @brief 형식 글로 답을 적습니다. */
        template <typename... TArgs>
        static void reply( string& outReply, const utf8* pFormat, const TArgs&... args )
        {
            StringBuilder<constant::buffer::kMax256> builder;
            builder.appendFormat( pFormat, args... );
            outReply = string( builder.view() );
        }
    };
} // namespace sw
