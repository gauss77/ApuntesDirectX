//--------------------------------------------------------------------------------------
// File: Model.cpp
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// http://go.microsoft.com/fwlink/?LinkId=248929
//--------------------------------------------------------------------------------------

#include "pch.h"
#include "Model.h"
#include "CommonStates.h"
#include "DirectXHelpers.h"
#include "Effects.h"
#include "PlatformHelpers.h"

using namespace DirectX;

#if !defined(_CPPRTTI) && !defined(__GXX_RTTI)
#error Model requires RTTI
#endif

//--------------------------------------------------------------------------------------
// ModelMeshPart
//--------------------------------------------------------------------------------------

ModelMeshPart::ModelMeshPart() noexcept :
    indexCount(0),
    startIndex(0),
    vertexOffset(0),
    vertexStride(0),
    primitiveType(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST),
    indexFormat(DXGI_FORMAT_R16_UINT),
    isAlpha(false)
{
}


ModelMeshPart::~ModelMeshPart()
{
}


_Use_decl_annotations_
void ModelMeshPart::Draw(
    ID3D11DeviceContext* deviceContext,
    IEffect* ieffect,
    ID3D11InputLayout* iinputLayout,
    std::function<void()> setCustomState) const
{
    deviceContext->IASetInputLayout(iinputLayout);

    auto vb = vertexBuffer.Get();
    UINT vbStride = vertexStride;
    UINT vbOffset = 0;
    deviceContext->IASetVertexBuffers(0, 1, &vb, &vbStride, &vbOffset);

    // Note that if indexFormat is DXGI_FORMAT_R32_UINT, this model mesh part requires a Feature Level 9.2 or greater device
    deviceContext->IASetIndexBuffer(indexBuffer.Get(), indexFormat, 0);

    assert(ieffect != nullptr);
    ieffect->Apply(deviceContext);

    // Hook lets the caller replace our shaders or state settings with whatever else they see fit.
    if (setCustomState)
    {
        setCustomState();
    }

    // Draw the primitive.
    deviceContext->IASetPrimitiveTopology(primitiveType);

    deviceContext->DrawIndexed(indexCount, startIndex, vertexOffset);
}


_Use_decl_annotations_
void ModelMeshPart::DrawInstanced(
    ID3D11DeviceContext* deviceContext,
    IEffect* ieffect,
    ID3D11InputLayout* iinputLayout,
    uint32_t instanceCount, uint32_t startInstanceLocation,
    std::function<void()> setCustomState) const
{
    deviceContext->IASetInputLayout(iinputLayout);

    auto vb = vertexBuffer.Get();
    UINT vbStride = vertexStride;
    UINT vbOffset = 0;
    deviceContext->IASetVertexBuffers(0, 1, &vb, &vbStride, &vbOffset);

    // Note that if indexFormat is DXGI_FORMAT_R32_UINT, this model mesh part requires a Feature Level 9.2 or greater device
    deviceContext->IASetIndexBuffer(indexBuffer.Get(), indexFormat, 0);

    assert(ieffect != nullptr);
    ieffect->Apply(deviceContext);

    // Hook lets the caller replace our shaders or state settings with whatever else they see fit.
    if (setCustomState)
    {
        setCustomState();
    }

    // Draw the primitive.
    deviceContext->IASetPrimitiveTopology(primitiveType);

    deviceContext->DrawIndexedInstanced(
        indexCount, instanceCount, startIndex,
        vertexOffset,
        startInstanceLocation);
}


_Use_decl_annotations_
void ModelMeshPart::CreateInputLayout(ID3D11Device* d3dDevice, IEffect* ieffect, ID3D11InputLayout** iinputLayout) const
{
    if (iinputLayout)
    {
        *iinputLayout = nullptr;
    }

    if (!vbDecl || vbDecl->empty())
        throw std::runtime_error("Model mesh part missing vertex buffer input elements data");

    if (vbDecl->size() > D3D11_IA_VERTEX_INPUT_STRUCTURE_ELEMENT_COUNT)
        throw std::runtime_error("Model mesh part input layout size is too large for DirectX 11");

    ThrowIfFailed(
        CreateInputLayoutFromEffect(d3dDevice, ieffect, vbDecl->data(), vbDecl->size(), iinputLayout)
    );

    assert(iinputLayout != nullptr && *iinputLayout != nullptr);
    _Analysis_assume_(iinputLayout != nullptr && *iinputLayout != nullptr);
}


_Use_decl_annotations_
void ModelMeshPart::ModifyEffect(ID3D11Device* d3dDevice, std::shared_ptr<IEffect>& ieffect, bool isalpha)
{
    if (!vbDecl || vbDecl->empty())
        throw std::runtime_error("Model mesh part missing vertex buffer input elements data");

    if (vbDecl->size() > D3D11_IA_VERTEX_INPUT_STRUCTURE_ELEMENT_COUNT)
        throw std::runtime_error("Model mesh part input layout size is too large for DirectX 11");

    assert(ieffect != nullptr);
    this->effect = ieffect;
    this->isAlpha = isalpha;

    assert(d3dDevice != nullptr);

    ThrowIfFailed(
        CreateInputLayoutFromEffect(d3dDevice, effect.get(), vbDecl->data(), vbDecl->size(), inputLayout.ReleaseAndGetAddressOf())
    );
}


//--------------------------------------------------------------------------------------
// ModelMesh
//--------------------------------------------------------------------------------------

ModelMesh::ModelMesh() noexcept :
    boneIndex(ModelBone::c_Invalid),
    ccw(true),
    pmalpha(true)
{
}


ModelMesh::~ModelMesh()
{
}


_Use_decl_annotations_
void ModelMesh::PrepareForRendering(
    ID3D11DeviceContext* deviceContext,
    const CommonStates& states,
    bool alpha,
    bool wireframe) const
{
    assert(deviceContext != nullptr);

    // Set the blend and depth stencil state.
    ID3D11BlendState* blendState;
    ID3D11DepthStencilState* depthStencilState;

    if (alpha)
    {
        if (pmalpha)
        {
            blendState = states.AlphaBlend();
            depthStencilState = states.DepthRead();
        }
        else
        {
            blendState = states.NonPremultiplied();
            depthStencilState = states.DepthRead();
        }
    }
    else
    {
        blendState = states.Opaque();
        depthStencilState = states.DepthDefault();
    }

    deviceContext->OMSetBlendState(blendState, nullptr, 0xFFFFFFFF);
    deviceContext->OMSetDepthStencilState(depthStencilState, 0);

    // Set the rasterizer state.
    if (wireframe)
        deviceContext->RSSetState(states.Wireframe());
    else
        deviceContext->RSSetState(ccw ? states.CullCounterClockwise() : states.CullClockwise());

    // Set sampler state.
    ID3D11SamplerState* samplers[] =
    {
        states.LinearWrap(),
        states.LinearWrap(),
    };

    deviceContext->PSSetSamplers(0, 2, samplers);
}


_Use_decl_annotations_
void XM_CALLCONV ModelMesh::Draw(
    ID3D11DeviceContext* deviceContext,
    FXMMATRIX world,
    CXMMATRIX view,
    CXMMATRIX projection,
    bool alpha,
    std::function<void()> setCustomState) const
{
    assert(deviceContext != nullptr);

    for (const auto& it : meshParts)
    {
        auto part = it.get();
        assert(part != nullptr);

        if (part->isAlpha != alpha)
        {
            // Skip alpha parts when drawing opaque or skip opaque parts if drawing alpha
            continue;
        }

        auto imatrices = dynamic_cast<IEffectMatrices*>(part->effect.get());
        if (imatrices)
        {
            imatrices->SetMatrices(world, view, projection);
        }

        part->Draw(deviceContext, part->effect.get(), part->inputLayout.Get(), setCustomState);
    }
}


_Use_decl_annotations_
void XM_CALLCONV ModelMesh::DrawSkinned(
    ID3D11DeviceContext* deviceContext,
    size_t nbones, _In_reads_(nbones) const XMMATRIX* boneTransforms,
    CXMMATRIX view,
    CXMMATRIX projection,
    bool alpha,
    std::function<void()> setCustomState) const
{
    assert(deviceContext != nullptr);

    if (!nbones || !boneTransforms)
    {
        throw std::invalid_argument("Bone transforms array required");
    }

    for (auto it = meshParts.cbegin(); it != meshParts.cend(); ++it)
    {
        auto part = (*it).get();
        assert(part != nullptr);

        if (part->isAlpha != alpha)
        {
            // Skip alpha parts when drawing opaque or skip opaque parts if drawing alpha
            continue;
        }

        auto imatrices = dynamic_cast<IEffectMatrices*>(part->effect.get());
        if (imatrices)
        {
            imatrices->SetView(view);
            imatrices->SetProjection(projection);
        }

        auto iskinning = dynamic_cast<IEffectSkinning*>(part->effect.get());
        if (iskinning)
        {
            if (boneInfluences.empty())
            {
                DebugTrace("Model is missing bone influences which are required for skinning\n");
                throw std::runtime_error("Skinning a model requires bone influences");
            }

            // TODO - boneInfluences maps boneTransforms to bone indices
        }
        else if (imatrices)
        {
            imatrices->SetWorld(
                (boneIndex != ModelBone::c_Invalid && boneIndex < nbones)
                ? boneTransforms[boneIndex] : boneTransforms[0]);
        }

        part->Draw(deviceContext, part->effect.get(), part->inputLayout.Get(), setCustomState);
    }
}


//--------------------------------------------------------------------------------------
// Model
//--------------------------------------------------------------------------------------

Model::~Model()
{
}


_Use_decl_annotations_
void XM_CALLCONV Model::Draw(
    ID3D11DeviceContext* deviceContext,
    const CommonStates& states,
    FXMMATRIX world,
    CXMMATRIX view,
    CXMMATRIX projection,
    bool wireframe,
    std::function<void()> setCustomState) const
{
    assert(deviceContext != nullptr);

    // Draw opaque parts
    for (const auto& it : meshes)
    {
        auto mesh = it.get();
        assert(mesh != nullptr);

        mesh->PrepareForRendering(deviceContext, states, false, wireframe);

        mesh->Draw(deviceContext, world, view, projection, false, setCustomState);
    }

    // Draw alpha parts
    for (const auto& it : meshes)
    {
        auto mesh = it.get();
        assert(mesh != nullptr);

        mesh->PrepareForRendering(deviceContext, states, true, wireframe);

        mesh->Draw(deviceContext, world, view, projection, true, setCustomState);
    }
}


_Use_decl_annotations_
void XM_CALLCONV Model::Draw(
    ID3D11DeviceContext* deviceContext,
    const CommonStates& states,
    size_t nbones,
    const XMMATRIX* boneTransforms,
    CXMMATRIX view,
    CXMMATRIX projection,
    bool wireframe,
    std::function<void()> setCustomState) const
{
    assert(deviceContext != nullptr);

    if (!nbones || !boneTransforms)
    {
        if (bones.empty())
        {
            throw std::invalid_argument("Model contains no bones");
        }

        nbones = bones.size();
        boneTransforms = boneMatrices.get();
    }

    // Draw opaque parts
    for (auto it = meshes.cbegin(); it != meshes.cend(); ++it)
    {
        auto mesh = it->get();
        assert(mesh != nullptr);

        mesh->PrepareForRendering(deviceContext, states, false, wireframe);

        if (mesh->boneIndex != ModelBone::c_Invalid && mesh->boneIndex < nbones)
        {
            mesh->Draw(deviceContext, boneTransforms[mesh->boneIndex], view, projection, false, setCustomState);
        }
        else
        {
            mesh->Draw(deviceContext, boneTransforms[0], view, projection, false, setCustomState);
        }
    }

    // Draw alpha parts
    for (auto it = meshes.cbegin(); it != meshes.cend(); ++it)
    {
        auto mesh = it->get();
        assert(mesh != nullptr);

        mesh->PrepareForRendering(deviceContext, states, true, wireframe);

        if (mesh->boneIndex != ModelBone::c_Invalid && mesh->boneIndex < nbones)
        {
            mesh->Draw(deviceContext, boneTransforms[mesh->boneIndex], view, projection, true, setCustomState);
        }
        else
        {
            mesh->Draw(deviceContext, boneTransforms[0], view, projection, true, setCustomState);
        }
    }
}


_Use_decl_annotations_
void XM_CALLCONV Model::DrawSkinned(
    ID3D11DeviceContext* deviceContext,
    const CommonStates& states,
    size_t nbones,
    const XMMATRIX* boneTransforms,
    CXMMATRIX view,
    CXMMATRIX projection,
    bool wireframe,
    std::function<void()> setCustomState) const
{
    assert(deviceContext != nullptr);
    assert(boneTransforms != nullptr);

    if (!nbones || !boneTransforms)
    {
        throw std::invalid_argument("Bone transforms array required");
    }

    // Draw opaque parts
    for (auto it = meshes.cbegin(); it != meshes.cend(); ++it)
    {
        auto mesh = it->get();
        assert(mesh != nullptr);

        mesh->PrepareForRendering(deviceContext, states, false, wireframe);

        mesh->DrawSkinned(deviceContext, nbones, boneTransforms, view, projection, false, setCustomState);
    }

    // Draw alpha parts
    for (auto it = meshes.cbegin(); it != meshes.cend(); ++it)
    {
        auto mesh = it->get();
        assert(mesh != nullptr);

        mesh->PrepareForRendering(deviceContext, states, true, wireframe);

        mesh->DrawSkinned(deviceContext, nbones, boneTransforms, view, projection, true, setCustomState);
    }
}


void Model::UpdateEffects(_In_ std::function<void(IEffect*)> setEffect)
{
    if (mEffectCache.empty())
    {
        // This cache ensures we only set each effect once (could be shared)
        for (const auto& mit : meshes)
        {
            auto mesh = mit.get();
            assert(mesh != nullptr);

            for (const auto& it : mesh->meshParts)
            {
                if (it->effect)
                    mEffectCache.insert(it->effect.get());
            }
        }
    }

    assert(setEffect != nullptr);

    for (const auto it : mEffectCache)
    {
        setEffect(it);
    }
}
