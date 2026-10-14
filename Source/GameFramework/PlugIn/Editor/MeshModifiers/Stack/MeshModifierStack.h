/**
 * @file MeshModifierStack.h
 * @brief 순서가 있는 메시 모디파이어 목록과 그 저장 문서(`<메시>.modifiers.json`)를 다룹니다. ImGui 와 씬을 모릅니다.
 * @details 저장 문서는 모디파이어마다 종류 id · 이름 · 켜짐 · 기즈모 · 매개변수(선언 이름 → 값) · 추가 상태를 적는다.
 *          읽을 때는 id 로 레지스트리에서 만들고 아는 이름의 값만 채운다 — 모르는 id 는 경고하고 건너뛰고, 모르는 매개변수 이름은 버린다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"

namespace sw::editor
{
    /**
     * @class MeshModifierStack
     * @brief 모디파이어를 위(0 번)에서 아래로 차례로 겁니다. 순서를 바꾸면 결과가 달라집니다(예: 비틀고 휘기 ≠ 휘고 비틀기).
     */
    class MeshModifierStack
    {
    public:
        /** @brief 저장 문서의 형식 버전입니다. */
        static constexpr uint32 kVersion = 1;
        /** @brief 사이드카 문서의 확장자입니다(`<메시 이름>.modifiers.json`). */
        static constexpr string_view kSidecarExtension = ".modifiers.json";
        /** @brief 굽기 결과 메시 이름에 붙는 접미입니다(`<메시 이름>_modified.mesh`). */
        static constexpr string_view kBakedSuffix = "_modified";

        MeshModifierStack();

        /** @brief id 의 기본값 모디파이어를 맨 아래에 더하고 그 번호를 반환합니다. 모르는 id 면 -1 입니다. */
        int32 addModifier( string_view typeID );
        /** @brief 이미 만든 모디파이어를 맨 아래에 더하고 그 번호를 반환합니다. */
        int32 addModifier( unique_ptr<MeshModifier> modifier );
        /** @brief 번호의 모디파이어를 뺍니다. 범위 밖이면 false 입니다. */
        [[nodiscard]] bool removeModifier( int32 index );
        /** @brief 한 칸 위(먼저 걸리는 쪽)로 옮깁니다. 맨 위거나 범위 밖이면 false 입니다. */
        [[nodiscard]] bool moveUp( int32 index );
        /** @brief 한 칸 아래로 옮깁니다. 맨 아래거나 범위 밖이면 false 입니다. */
        [[nodiscard]] bool moveDown( int32 index );
        void clear();

        /**
         * @brief 켜진 모디파이어를 차례로 겁니다. 법선 재계산을 고르면 끝에 면 법선으로 다시 만듭니다.
         * @details 메시는 제자리에서 바뀐다 — 모디파이어마다 새 정점 배열을 잡지 않는다(각 모디파이어도 정점 배열을 제자리에서 덩어리로 나눠 병렬로 고친다).
         */
        void apply( MeshModifierMesh& inoutMesh, const MeshModifierEvaluation& evaluation ) const;

        /** @brief 저장 문서(JSON 글)를 만듭니다. @p indent 가 음수면 한 줄입니다. */
        string makeJSONText( int32 indent = 2 ) const;
        /** @brief 저장 문서를 읽습니다. 문서가 틀리면 false 이고 스택은 비웁니다. 모르는 종류는 경고하고 건너뜁니다. */
        [[nodiscard]] bool readJSONText( string_view jsonText );
        /** @brief 절대 경로에 저장합니다. */
        [[nodiscard]] bool saveFile( string_view absolutePath ) const;
        /** @brief 절대 경로에서 읽습니다. */
        [[nodiscard]] bool loadFile( string_view absolutePath );

        /** @brief 메시 경로(`.../x.mesh`)에 맞는 사이드카 경로(`.../x.modifiers.json`)입니다. */
        static string makeSidecarPath( string_view meshPath );
        /** @brief 메시 경로에 맞는 굽기 결과 경로(`.../x_modified.mesh`)입니다. */
        static string makeBakedMeshPath( string_view meshPath );

        int32               getModifierCount() const { return static_cast<int32>( _listModifier.size() ); }
        MeshModifier*       findModifier( int32 index );
        const MeshModifier* findModifier( int32 index ) const;
        bool                isRecomputingNormals() const { return _bRecomputeNormals; }
        void                setRecomputingNormals( bool bRecompute ) { _bRecomputeNormals = bRecompute; }

    private:
        vector<unique_ptr<MeshModifier>> _listModifier;
        bool                             _bRecomputeNormals;
    };
} // namespace sw::editor
