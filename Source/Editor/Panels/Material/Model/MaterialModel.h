/**
 * @file MaterialModel.h
 * @brief 머티리얼 창의 모델 — 편집 사본(`Material`), 이름 · 셰이더 입력란, 읽기 · 쓰기 · 되돌리기 글, 선택 오브젝트 · 캐시에 바로 보이기, 미리보기 씬의 구 놓기 · 지금 문서 걸기,
 *        CPU 구 미리보기의 감시 값, 상태 줄. ImGui 를 모릅니다.
 * @details 패널(`MaterialPanel`)은 미리보기 씬 · 뷰포트를 쥐고(`EditorPreviewPanel`) 프로퍼티 위젯 · CPU 구를 그리며 문서 dirty · 되돌리기 기록을 남긴다 —
 *          메시 미리보기(`MeshPreviewModel`)와 같은 선이다(언리얼 머티리얼 인스턴스 에디터의 편집 사본과 디테일 뷰가 나뉜 것과 같다). 씬은 패널이 넘긴다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Workspace/Asset/EditorAssetType.h"

#include "Engine/Graphics/Material/MaterialTypes.h"

namespace sw
{
    class Material;
    class MeshComponent;
    class Scene;
} // namespace sw

namespace sw::editor
{
    class DocumentEditRecorder;
    /**
     * @class MaterialModel
     * @brief 머티리얼 창의 상태와 명령입니다. 파일 머리말 참고.
     */
    class MaterialModel
    {
    public:
        /** @brief 편집 알림 창구를 겁니다 — 문서 패널이 만들 때 한 번 건다(`EditorDocumentPanel::getDocumentEditRecorder`). 창구는 패널이 쥐어 이 모델보다 먼저 사라지지 않는다. */
        void bindDocumentEditRecorder( const DocumentEditRecorder& recorder ) { _pEditRecorder = &recorder; }
        MaterialModel();

        // ------------------------------------------------------------------------------
        // 1) 읽기 · 쓰기 · 되돌리기 글
        // ------------------------------------------------------------------------------
        /** @brief @p path 의 머티리얼을 읽고 입력란을 맞춥니다. 읽지 못하면 상태 줄에 알리고 Malformed 입니다. */
        ToolAssetLoadResult loadDocument( string_view path );
        /** @brief 입력란을 사본에 옮겨 @p path 에 쓰고, 엔진 캐시를 다시 읽혀 바로 보이게 합니다. 성공하면 true 입니다(dirty 해제 · 되돌리기 기준은 패널이 한다). */
        [[nodiscard]] bool saveDocument( string_view path );
        string             captureText() const;
        /** @brief 되돌리기 기록 글로 사본을 되돌리고 @p path 의 캐시 사본에 보입니다. 읽지 못하면 사본을 그대로 둡니다. */
        void applyText( string_view text, string_view path );

        // ------------------------------------------------------------------------------
        // 2) 편집
        // ------------------------------------------------------------------------------
        /** @brief 편집 사본입니다(읽기만 — 고치는 길은 아래 연산이다. 연산마다 모델이 되돌리기 이름으로 알린다, 감사 E-16). */
        const Material& getMaterial() const { return *_material; }
        /** @brief 이름 입력란의 글입니다(입력 중인 값 — 사본에는 `commitName` 이 옮긴다). */
        fixed_string<constant::buffer::kMax128>& editNameBuffer() { return _name; }
        /** @brief 이름 입력란을 사본에 옮기고 "Edit Material Name" 으로 알립니다. */
        void        commitName();
        const utf8* getShaderPath() const { return _shaderPath.c_str(); }
        /** @brief 셰이더를 바꾸고 "Edit Material Shader" 로 알립니다. */
        void setShaderPath( string_view shaderPath );
        /**
         * @brief 프로퍼티 @p value(이름이 대상)를 사본에 쓰고 "Edit Material Property" 로 알립니다. 없는 이름이면 false 입니다.
         * @details 위젯은 프로퍼티 사본을 고치고 바뀌었을 때 이것을 부른다 — 슬라이더 끌기 한 번은 상호작용 구간 하나 = 되돌리기 한 단계(감사 P0-1).
         */
        bool setProperty( const MaterialProperty& value );
        /** @brief 프로퍼티 @p name 을 기본값으로 되돌리고(텍스처 경로도 비운다) 바뀌었으면 "Edit Material Property" 로 알립니다. 바뀌었으면 true 입니다. */
        bool resetProperty( const hashed_string& name );
        /** @brief 편집 사본을 선택 오브젝트와 @p path 의 캐시 사본에 바로 보입니다(Apply to Selection · 편집마다). */
        void          applyLivePreview( string_view path ) const;
        const string& getStatus() const { return _status; }

        /** @brief @p pValue 의 @p count 개(1..4) 실수를 머티리얼 값 글("a,b,c")로 씁니다. */
        static void writeFloats( string& out, const float32* pValue, uint32 count );

        // ------------------------------------------------------------------------------
        // 3) 미리보기
        // ------------------------------------------------------------------------------
        /** @brief 새 미리보기 씬 @p scene 에 구 하나(`PreviewSubject`)를 스폰합니다. 머티리얼은 `syncPreviewSubject` 가 건다. */
        void populateScene( Scene& scene );
        /** @brief @p pScene 의 구가 @p path 의 머티리얼을 쓰게 합니다(경로가 바뀌었을 때만). 문서를 읽지 못했으면 부르는 쪽이 부르지 않는다. */
        void syncPreviewSubject( Scene* pScene, string_view path );
        /** @brief 그리기를 시작한다 — 이번 프레임에 CPU 구를 그리기 전까지 감시 값이 없다(창이 접히면 그대로 없다). */
        void beginFrame() { _bPreviewDrawn = SW_FALSE; }
        /** @brief CPU 구 미리보기를 그렸다 — 그 평균 (R - B) 를 감시 값으로 남깁니다. */
        void recordPreviewDrawn( float32 redMinusBlue );
        /** @brief 지난 그리기의 미리보기 평균 (R - B)(0..255)입니다. 지난 프레임에 그리지 않았으면 false 입니다. */
        [[nodiscard]] bool findPreviewRedMinusBlue( float32& outValue ) const;

    private:
        /**
         * @brief 문서를 고쳤음을 알립니다 — 되돌리기 한 단계와 미저장이 된다(위젯이 활성인 동안은 상호작용 구간 하나). 창구를 걸지 않았으면 멈춘다.
         * @details 이 모델의 편집 연산만 부른다(감사 E-16) — 무엇을 고쳤는지와 되돌리기 이름은 모델이 정하고, View 는 연산만 부른다.
         */
        void recordDocumentEdit( string_view label ) const;
        /** @brief 사본의 이름 · 셰이더를 입력란에 옮깁니다. */
        void syncNameBuffers();
        /** @brief @p pScene 의 구입니다. 씬이 없으면 nullptr 입니다(이 호출 안에서만 씁니다). */
        [[nodiscard]] static MeshComponent* findPreviewSubject( Scene* pScene );

    private:
        shared_ptr<Material>                    _material; ///< 편집 사본. Material 은 create() 로만 만들 수 있습니다
        fixed_string<constant::buffer::kMax128> _name;
        fixed_string<constant::buffer::kMax256> _shaderPath;
        string                                  _status;
        string                                  _previewMaterialPath;      ///< 미리보기 구에 건 머티리얼 경로(비면 다음 `syncPreviewSubject` 가 다시 건다)
        float32                                 _previewRedMinusBlue;      ///< 지난 그리기의 미리보기 평균 (R - B)
        uint8                                   _bPreviewDrawn : 1;        ///< 지난 그리기에서 미리보기를 그렸다
        const DocumentEditRecorder*             _pEditRecorder{ nullptr }; ///< 편집 알림 창구(`bindDocumentEditRecorder` — 문서 패널이 쥔다)
    };
} // namespace sw::editor
