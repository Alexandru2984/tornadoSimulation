#pragma once

#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_INCLUDE_STB_IMAGE
#include "tiny_gltf.h"
#include "stb_image.h"

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <iostream>

struct GLTFModel {
    GLuint vao=0;
    GLuint vbo=0;
    GLuint ebo=0;
    GLsizei indexCount=0;
    GLuint texture = 0;
};

static bool loadSimpleGLTF(const std::string &path, GLTFModel &out) {
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err; std::string warn;
    bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, path);
    if (!warn.empty()) std::cerr << "gltf warn: " << warn << std::endl;
    if (!err.empty()) std::cerr << "gltf err: " << err << std::endl;
    if (!ret) return false;
    if (model.meshes.empty()) return false;
    const tinygltf::Mesh &mesh = model.meshes[0];
    if (mesh.primitives.empty()) return false;
    const tinygltf::Primitive &prim = mesh.primitives[0];
    // positions
    const tinygltf::Accessor &accPos = model.accessors[prim.attributes.at("POSITION")];
    const tinygltf::BufferView &bvPos = model.bufferViews[accPos.bufferView];
    const tinygltf::Buffer &bufPos = model.buffers[bvPos.buffer];
    const unsigned char* posData = bufPos.data.data() + bvPos.byteOffset + accPos.byteOffset;
    size_t posByteLen = accPos.count * sizeof(float) * 3;
    // normals
    const tinygltf::Accessor &accNorm = model.accessors[prim.attributes.at("NORMAL")];
    const tinygltf::BufferView &bvNorm = model.bufferViews[accNorm.bufferView];
    const tinygltf::Buffer &bufNorm = model.buffers[bvNorm.buffer];
    const unsigned char* normData = bufNorm.data.data() + bvNorm.byteOffset + accNorm.byteOffset;
    // texcoords (optional)
    const unsigned char* uvData = nullptr;
    if (prim.attributes.find("TEXCOORD_0") != prim.attributes.end()) {
        const tinygltf::Accessor &accUV = model.accessors[prim.attributes.at("TEXCOORD_0")];
        const tinygltf::BufferView &bvUV = model.bufferViews[accUV.bufferView];
        const tinygltf::Buffer &bufUV = model.buffers[bvUV.buffer];
        uvData = bufUV.data.data() + bvUV.byteOffset + accUV.byteOffset;
    }
    // indices
    const tinygltf::Accessor &accIdx = model.accessors[prim.indices];
    const tinygltf::BufferView &bvIdx = model.bufferViews[accIdx.bufferView];
    const tinygltf::Buffer &bufIdx = model.buffers[bvIdx.buffer];
    const unsigned char* idxData = bufIdx.data.data() + bvIdx.byteOffset + accIdx.byteOffset;
    size_t idxCount = accIdx.count;

    // interleave pos/norm/uv
    struct Vert { float px,py,pz; float nx,ny,nz; float u,v; };
    std::vector<Vert> verts(accPos.count);
    const float* pf = (const float*)posData;
    const float* nf = (const float*)normData;
    const float* uvf = uvData ? (const float*)uvData : nullptr;
    for (size_t i=0;i<accPos.count;++i) {
        verts[i].px = pf[i*3+0]; verts[i].py = pf[i*3+1]; verts[i].pz = pf[i*3+2];
        verts[i].nx = nf[i*3+0]; verts[i].ny = nf[i*3+1]; verts[i].nz = nf[i*3+2];
        if (uvf) { verts[i].u = uvf[i*2+0]; verts[i].v = uvf[i*2+1]; } else { verts[i].u = verts[i].v = 0.0f; }
    }
    std::vector<unsigned int> indices(idxCount);
    if (accIdx.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
        const unsigned short* s = (const unsigned short*)idxData;
        for (size_t i=0;i<idxCount;++i) indices[i] = s[i];
    } else if (accIdx.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
        const unsigned int* u = (const unsigned int*)idxData;
        for (size_t i=0;i<idxCount;++i) indices[i] = u[i];
    } else if (accIdx.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
        const unsigned char* c = (const unsigned char*)idxData;
        for (size_t i=0;i<idxCount;++i) indices[i] = c[i];
    }

    // create GL buffers
    glGenVertexArrays(1, &out.vao);
    glGenBuffers(1, &out.vbo);
    glGenBuffers(1, &out.ebo);
    glBindVertexArray(out.vao);
    glBindBuffer(GL_ARRAY_BUFFER, out.vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size()*sizeof(Vert), verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, out.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size()*sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);
    // positions -> location 0
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(Vert),(void*)offsetof(Vert,px));
    // normals -> location 1
    glEnableVertexAttribArray(1); glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(Vert),(void*)offsetof(Vert,nx));
    // shader expects aCol at location 2; glTF doesn't provide color so set a default constant color here
    glEnableVertexAttribArray(2); glVertexAttrib3f(2, 1.0f, 1.0f, 1.0f);
    // texcoords -> shader expects aUV at location 3
    glEnableVertexAttribArray(3); glVertexAttribPointer(3,2,GL_FLOAT,GL_FALSE,sizeof(Vert),(void*)offsetof(Vert,u));
    glBindVertexArray(0);
    out.indexCount = (GLsizei)indices.size();

    // load first image as texture if present; otherwise create a small procedural fallback texture
    out.texture = 0;
    if (!model.images.empty()) {
        const tinygltf::Image &img = model.images[0];
        if (!img.image.empty() && img.width > 0 && img.height > 0) {
            int w = img.width; int h = img.height; int comp = img.component;
            GLuint tex; glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
            GLenum format = GL_RGBA;
            if (comp == 3) format = GL_RGB;
            glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, format, GL_UNSIGNED_BYTE, img.image.data());
            glGenerateMipmap(GL_TEXTURE_2D);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            out.texture = tex;
        }
    }
    if (out.texture == 0) {
        // create a small 4x4 checker fallback texture (RGB)
        const int CX = 4, CY = 4;
        unsigned char pixels[CX * CY * 3];
        for (int y = 0; y < CY; ++y) {
            for (int x = 0; x < CX; ++x) {
                int idx = (y * CX + x) * 3;
                bool on = ((x / 2) % 2) ^ ((y / 2) % 2);
                unsigned char c = on ? 200 : 80;
                pixels[idx+0] = c; pixels[idx+1] = (unsigned char)(c * 0.9f); pixels[idx+2] = (unsigned char)(c * 0.8f);
            }
        }
        GLuint tex; glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, CX, CY, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        out.texture = tex;
    }

    return true;
}
