#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include <cmath>
#include <map>
#include <random>
#include <chrono>
#include <string>
#include <unistd.h>
#include <limits.h>
// glTF loader
#include "gltf_loader.h"

static double g_mouseX = 0.0f;
static double g_mouseY = 0.0f;

// Simple shader loader/compilation
static std::string loadFile(const char* path) {
    // Try a few sensible locations so running from build/ or project root works
    std::vector<std::string> tries;
    tries.push_back(std::string(path));
    tries.push_back(std::string("./") + path);
    tries.push_back(std::string("../") + path);
    // absolute cwd variants
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) != nullptr) {
        tries.push_back(std::string(cwd) + "/" + path);
        tries.push_back(std::string(cwd) + "/../" + path);
    }
    for (const auto &p : tries) {
        std::ifstream in(p);
        if (in) {
            std::ostringstream ss;
            ss << in.rdbuf();
            return ss.str();
        }
    }
    std::cerr << "Failed to open (tried multiple): " << path << std::endl;
    return std::string();
}

static GLuint compileShader(GLenum type, const char* src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0; glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
        std::string log(len, '\0');
        glGetShaderInfoLog(shader, len, nullptr, &log[0]);
        std::cerr << "Shader compile error: " << log << std::endl;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint linkProgram(GLuint v, GLuint f) {
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0; glGetProgramiv(p, GL_INFO_LOG_LENGTH, &len);
        std::string log(len, '\0');
        glGetProgramInfoLog(p, len, nullptr, &log[0]);
        std::cerr << "Program link error: " << log << std::endl;
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

// Minimal OBJ loader: supports 'v' and 'f' (with optional normals) for our simple assets
struct SimpleModel { GLuint vao=0, vbo=0, ebo=0; GLsizei indexCount=0; };

SimpleModel loadSimpleOBJ(const std::string &path) {
    std::ifstream in(path);
    SimpleModel m;
    if (!in) return m;
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    struct Vert { glm::vec3 p; glm::vec3 n; };
    std::vector<Vert> verts;
    std::vector<unsigned int> indices;
    std::map<std::string, unsigned int> cache;
    std::string line;
    while (std::getline(in, line)) {
        if (line.size() < 2) continue;
        std::istringstream ss(line);
        std::string tag; ss >> tag;
        if (tag == "v") { float x,y,z; ss>>x>>y>>z; positions.push_back({x,y,z}); }
        else if (tag == "vn") { float x,y,z; ss>>x>>y>>z; normals.push_back({x,y,z}); }
        else if (tag == "f") {
            std::string a,b,c;
            ss>>a>>b>>c;
            std::string arr[3]={a,b,c};
            for (int i=0;i<3;++i) {
                auto it = cache.find(arr[i]);
                if (it!=cache.end()) { indices.push_back(it->second); continue; }
                // parse format like v//vn or v
                std::string s = arr[i];
                int vIdx=0, nIdx=0;
                size_t p1 = s.find('/');
                if (p1==std::string::npos) { vIdx = std::stoi(s); }
                else {
                    std::string sv = s.substr(0,p1);
                    vIdx = std::stoi(sv);
                    size_t p2 = s.find('/', p1+1);
                    if (p2!=std::string::npos) {
                        std::string sn = s.substr(p2+1);
                        if (!sn.empty()) nIdx = std::stoi(sn);
                    }
                }
                glm::vec3 p = positions[vIdx-1];
                glm::vec3 n = nIdx>0 ? normals[nIdx-1] : glm::vec3(0,1,0);
                verts.push_back({p,n});
                unsigned int id = (unsigned int)verts.size()-1;
                cache[arr[i]] = id;
                indices.push_back(id);
            }
        }
    }
    // create GL buffers
    glGenVertexArrays(1, &m.vao);
    glGenBuffers(1, &m.vbo);
    glGenBuffers(1, &m.ebo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size()*sizeof(Vert), verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size()*sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);
    // pos
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(Vert),(void*)offsetof(Vert,p));
    // normal
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(Vert),(void*)offsetof(Vert,n));
    // color: we do not store per-vertex colors in our simple OBJ; we'll use a constant default
    glEnableVertexAttribArray(2);
    glVertexAttrib3f(2, 0.85f, 0.85f, 0.85f);
    glBindVertexArray(0);
    m.indexCount = (GLsizei)indices.size();
    return m;
}

// Camera
struct Camera {
    glm::vec3 pos{0.0f,2.0f,8.0f};
    float yaw = -90.0f;
    float pitch = 0.0f;
    float speed = 5.0f;
    float sensitivity = 0.12f;
    glm::mat4 getView() const {
        glm::vec3 dir;
        dir.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        dir.y = sin(glm::radians(pitch));
        dir.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
        return glm::lookAt(pos, pos + glm::normalize(dir), glm::vec3(0,1,0));
    }
};

// Particles
struct Particle { glm::vec3 pos; glm::vec3 vel; float life; };


void cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    int w, h;
    glfwGetWindowSize(window, &w, &h);
    // Normalize to -1..1
    g_mouseX = (xpos / (double)w) * 2.0 - 1.0;
    // map window Y (top=0) to NDC where up is +1
    g_mouseY = -((ypos / (double)h) * 2.0 - 1.0);
}

struct Vertex { glm::vec3 pos; glm::vec3 normal; glm::vec3 col; };

int main() {
    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "Tornada 3D - urmareste mouse", nullptr, nullptr);
    if (!window) { glfwTerminate(); return -1; }

    glfwMakeContextCurrent(window);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to init GLAD" << std::endl;
        return -1;
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Load shaders from ./shaders (binary dir will have copied them by CMake)
    std::string vertexSrc = loadFile("shaders/vertex.glsl");
    std::string fragmentSrc = loadFile("shaders/fragment.glsl");
    if (vertexSrc.empty() || fragmentSrc.empty()) {
        // fallback: try source dir
        vertexSrc = loadFile("../shaders/vertex.glsl");
        fragmentSrc = loadFile("../shaders/fragment.glsl");
    }
    if (vertexSrc.empty() || fragmentSrc.empty()) {
        std::cerr << "Couldn't load shader files. Ensure shaders/vertex.glsl and shaders/fragment.glsl exist." << std::endl;
        return -1;
    }

    GLuint vs = compileShader(GL_VERTEX_SHADER, vertexSrc.c_str());
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragmentSrc.c_str());
    GLuint program = linkProgram(vs, fs);
    glDeleteShader(vs); glDeleteShader(fs);

    // Create small procedural textures: brick (walls), wood (roof), leaf (foliage)
    GLuint brickTex = 0, woodTex = 0, leafTex = 0;
    // Brick: simple red/orange brick grid
    {
        const int CX = 32, CY = 32;
        std::vector<unsigned char> pixels(CX * CY * 3);
        for (int y = 0; y < CY; ++y) {
            for (int x = 0; x < CX; ++x) {
                int idx = (y * CX + x) * 3;
                int bx = (x / 6);
                int by = (y / 6);
                bool mortar = ((bx + by) % 2 == 0) ? (x % 6 == 0 || y % 6 == 0) : (x % 6 == 0);
                if (mortar) {
                    pixels[idx+0] = 200; pixels[idx+1] = 180; pixels[idx+2] = 165;
                } else {
                    pixels[idx+0] = 155 + (bx % 3) * 10; pixels[idx+1] = 60 + (by % 2) * 15; pixels[idx+2] = 40;
                }
            }
        }
        glGenTextures(1, &brickTex); glBindTexture(GL_TEXTURE_2D, brickTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, CX, CY, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    // Wood: simple vertical grain
    {
        const int CX = 32, CY = 32;
        std::vector<unsigned char> pixels(CX * CY * 3);
        for (int y = 0; y < CY; ++y) {
            for (int x = 0; x < CX; ++x) {
                int idx = (y * CX + x) * 3;
                float v = 140.0f + 30.0f * sinf((float)x * 0.6f + (y%3));
                pixels[idx+0] = (unsigned char)glm::clamp(v + 10.0f, 0.0f, 255.0f);
                pixels[idx+1] = (unsigned char)glm::clamp(v - 20.0f, 0.0f, 255.0f);
                pixels[idx+2] = (unsigned char)glm::clamp(v - 45.0f, 0.0f, 255.0f);
            }
        }
        glGenTextures(1, &woodTex); glBindTexture(GL_TEXTURE_2D, woodTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, CX, CY, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    // Leaf: green noise
    {
        const int CX = 32, CY = 32;
        std::vector<unsigned char> pixels(CX * CY * 3);
        for (int y = 0; y < CY; ++y) {
            for (int x = 0; x < CX; ++x) {
                int idx = (y * CX + x) * 3;
                unsigned char g = 80 + (unsigned char)((x * 37 + y * 23) % 120);
                pixels[idx+0] = (unsigned char)(g * 0.4f);
                pixels[idx+1] = g;
                pixels[idx+2] = (unsigned char)(g * 0.6f);
            }
        }
        glGenTextures(1, &leafTex); glBindTexture(GL_TEXTURE_2D, leafTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, CX, CY, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // particle shaders
    std::string pvs = loadFile("shaders/particle_vertex.glsl");
    std::string pfs = loadFile("shaders/particle_fragment.glsl");
    if (pvs.empty() || pfs.empty()) {
        pvs = loadFile("../shaders/particle_vertex.glsl");
        pfs = loadFile("../shaders/particle_fragment.glsl");
    }
    GLuint pvsId = compileShader(GL_VERTEX_SHADER, pvs.c_str());
    GLuint pfsId = compileShader(GL_FRAGMENT_SHADER, pfs.c_str());
    GLuint particleProgram = linkProgram(pvsId, pfsId);
    glDeleteShader(pvsId); glDeleteShader(pfsId);

    // Build a proper 3D tornado mesh: stacked rings, unique vertex per (ring,segment), indexed triangles
    const int segments = 128; // more segments for smoother roundness
    const int rings = 80;
    const float height = 6.0f;
    const float baseRadius = 1.5f;
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;

    // Create vertices (inverted: radius increases with height so tornado narrows near ground)
    for (int r = 0; r <= rings; ++r) {
        float t = float(r) / float(rings);
        float y = t * height;
        // inverted: small at bottom (t=0) -> larger at top (t=1)
        float rad = baseRadius * t + 0.05f;
        for (int s = 0; s <= segments; ++s) {
            float ang = s / (float)segments * 2.0f * M_PI;
            float ca = cosf(ang);
            float sa = sinf(ang);
            glm::vec3 pos = glm::vec3(rad * ca, y, rad * sa);
            // approximate normal for inverted cone surface
            float dr_dy = ( baseRadius ) / height; // derivative of radius wrt height (positive)
            glm::vec3 normal = glm::normalize(glm::vec3(ca, dr_dy, sa));
            glm::vec3 col = glm::vec3(0.5f, 0.5f, 0.6f) * (0.4f + 0.6f * t);
            vertices.push_back({ pos, normal, col });
        }
    }

    // Create indices (two triangles per quad between rings)
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < segments; ++s) {
            unsigned int i0 = r * (segments + 1) + s;
            unsigned int i1 = (r + 1) * (segments + 1) + s;
            unsigned int i2 = r * (segments + 1) + (s + 1);
            unsigned int i3 = (r + 1) * (segments + 1) + (s + 1);
            // triangle 1
            indices.push_back(i0);
            indices.push_back(i1);
            indices.push_back(i2);
            // triangle 2
            indices.push_back(i2);
            indices.push_back(i1);
            indices.push_back(i3);
        }
    }
    GLuint vao, vbo, ebo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);
    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    // pos
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));
    // normal
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
    // col
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, col));

    glBindVertexArray(0);

    // --- Simple scene objects: ground plane and a few building cubes ---
    // Ground (two triangles forming a large quad)
    struct SimpleVert { glm::vec3 pos; glm::vec3 normal; glm::vec3 col; };
    std::vector<SimpleVert> groundVerts = {
        {{-50.0f, 0.0f, -50.0f}, {0,1,0}, {0.15f,0.45f,0.2f}},
        {{50.0f, 0.0f, -50.0f}, {0,1,0}, {0.15f,0.45f,0.2f}},
        {{50.0f, 0.0f, 50.0f}, {0,1,0}, {0.15f,0.45f,0.2f}},
        {{-50.0f, 0.0f, 50.0f}, {0,1,0}, {0.15f,0.45f,0.2f}},
    };
    std::vector<unsigned int> groundIdx = {0,1,2, 0,2,3};

    GLuint groundVAO, groundVBO, groundEBO;
    glGenVertexArrays(1, &groundVAO);
    glGenBuffers(1, &groundVBO);
    glGenBuffers(1, &groundEBO);
    glBindVertexArray(groundVAO);
    glBindBuffer(GL_ARRAY_BUFFER, groundVBO);
    glBufferData(GL_ARRAY_BUFFER, groundVerts.size()*sizeof(SimpleVert), groundVerts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, groundEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, groundIdx.size()*sizeof(unsigned int), groundIdx.data(), GL_STATIC_DRAW);
    // pos
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(SimpleVert),(void*)offsetof(SimpleVert,pos));
    // normal
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(SimpleVert),(void*)offsetof(SimpleVert,normal));
    // col
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2,3,GL_FLOAT,GL_FALSE,sizeof(SimpleVert),(void*)offsetof(SimpleVert,col));
    glBindVertexArray(0);

    // Simple cube geometry for buildings (8 verts, 36 indices)
    std::vector<SimpleVert> cubeVerts = {
        {{-0.5f, -0.5f, -0.5f},{0,0,-1},{0.7f,0.7f,0.7f}},
        {{0.5f, -0.5f, -0.5f},{0,0,-1},{0.7f,0.7f,0.7f}},
        {{0.5f,  0.5f, -0.5f},{0,0,-1},{0.7f,0.7f,0.7f}},
        {{-0.5f,  0.5f, -0.5f},{0,0,-1},{0.7f,0.7f,0.7f}},
        {{-0.5f, -0.5f,  0.5f},{0,0,1},{0.7f,0.7f,0.7f}},
        {{0.5f, -0.5f,  0.5f},{0,0,1},{0.7f,0.7f,0.7f}},
        {{0.5f,  0.5f,  0.5f},{0,0,1},{0.7f,0.7f,0.7f}},
        {{-0.5f,  0.5f,  0.5f},{0,0,1},{0.7f,0.7f,0.7f}},
    };
    std::vector<unsigned int> cubeIdx = {
        0,1,2, 0,2,3, // back
        4,5,6, 4,6,7, // front
        3,2,6, 3,6,7, // top
        0,1,5, 0,5,4, // bottom
        1,2,6, 1,6,5, // right
        0,3,7, 0,7,4  // left
    };

    // create simple procedural house model (slightly improved) with UVs
    SimpleModel house; 
    {
        struct PVert { glm::vec3 p; glm::vec3 n; glm::vec2 uv; };
        std::vector<PVert> hv;
        std::vector<unsigned int> hi;
        // cube base (scaled) - center at 0
        float hw = 0.6f, hh = 1.0f, hd = 0.6f;
        glm::vec3 baseColor(0.9f,0.78f,0.6f);
        // 8 verts
        std::vector<glm::vec3> pos = {{-hw,0,-hd},{hw,0,-hd},{hw,0,hd},{-hw,0,hd},{-hw,hh,-hd},{hw,hh,-hd},{hw,hh,hd},{-hw,hh,hd}};
        for (auto &p: pos) hv.push_back({p, glm::vec3(0,1,0), glm::vec2((p.x+hw)/(2.0f*hw), p.y/(hh+0.6f))});
        unsigned int cidx[] = {0,1,2, 0,2,3, 4,5,6, 4,6,7, 0,1,5, 0,5,4, 1,2,6, 1,6,5, 2,3,7, 2,7,6, 3,0,4, 3,4,7};
        hi.insert(hi.end(), std::begin(cidx), std::end(cidx));
        // roof pyramid
        glm::vec3 apex(0.0f, hh + 0.6f, 0.0f);
        hv.push_back({apex, glm::vec3(0,1,0), glm::vec2(0.5f,1.0f)});
        unsigned int apexIdx = (unsigned int)hv.size()-1;
        // add roof faces
        unsigned int roof[] = {4,apexIdx,5, 5,apexIdx,6, 6,apexIdx,7, 7,apexIdx,4};
        hi.insert(hi.end(), std::begin(roof), std::end(roof));
        // upload
        glGenVertexArrays(1, &house.vao);
        glGenBuffers(1, &house.vbo);
        glGenBuffers(1, &house.ebo);
        glBindVertexArray(house.vao);
        glBindBuffer(GL_ARRAY_BUFFER, house.vbo);
        glBufferData(GL_ARRAY_BUFFER, hv.size()*sizeof(PVert), hv.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, house.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, hi.size()*sizeof(unsigned int), hi.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(PVert),(void*)offsetof(PVert,p));
        glEnableVertexAttribArray(1); glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(PVert),(void*)offsetof(PVert,n));
        glEnableVertexAttribArray(2); glVertexAttrib3f(2, 0.85f, 0.85f, 0.85f);
        // UV attribute at location 3
        glEnableVertexAttribArray(3); glVertexAttribPointer(3,2,GL_FLOAT,GL_FALSE,sizeof(PVert),(void*)offsetof(PVert,uv));
        glBindVertexArray(0);
        house.indexCount = (GLsizei)hi.size();
    }

    // create simple procedural tree (trunk + layered cones) with UVs
    SimpleModel tree;
    {
        struct PVert { glm::vec3 p; glm::vec3 n; glm::vec2 uv; };
        std::vector<PVert> tv;
        std::vector<unsigned int> ti;
        // trunk: simple box
        float tw=0.15f, th=0.5f;
        std::vector<glm::vec3> tpos = {{-tw,0,-tw},{tw,0,-tw},{tw,0,tw},{-tw,0,tw},{-tw,th,-tw},{tw,th,-tw},{tw,th,tw},{-tw,th,tw}};
        for (auto &p: tpos) tv.push_back({p,glm::vec3(0,1,0), glm::vec2((p.x + tw)/(2.0f*tw), p.y/(th+0.4f))});
        unsigned int tidx[] = {0,1,2,0,2,3,4,5,6,4,6,7,0,1,5,0,5,4,1,2,6,1,6,5,2,3,7,2,7,6,3,0,4,3,4,7};
        ti.insert(ti.end(), std::begin(tidx), std::end(tidx));
        // foliage layers: cones approximated by 6-seg rings
        int seg = 6;
        for (int layer=0; layer<3; ++layer) {
            float baseY = th + layer*0.25f + 0.1f;
            float radius = 0.6f - layer*0.15f;
            int start = (int)tv.size();
            for (int s=0;s<seg;++s) {
                float a = s/(float)seg * 2.0f * M_PI;
                glm::vec3 vp = glm::vec3(cosf(a)*radius, baseY, sinf(a)*radius);
                tv.push_back({vp, glm::vec3(0,1,0), glm::vec2((cosf(a)*0.5f)+0.5f, (baseY)/(th+1.2f))});
            }
            // apex
            tv.push_back({glm::vec3(0.0f, baseY + 0.4f, 0.0f), glm::vec3(0,1,0), glm::vec2(0.5f,1.0f)});
            int apex = (int)tv.size()-1;
            for (int s=0;s<seg;++s) {
                int a = start + s;
                int b = start + ((s+1)%seg);
                ti.push_back(a); ti.push_back(b); ti.push_back(apex);
            }
        }
        // upload
        glGenVertexArrays(1, &tree.vao);
        glGenBuffers(1, &tree.vbo);
        glGenBuffers(1, &tree.ebo);
        glBindVertexArray(tree.vao);
        glBindBuffer(GL_ARRAY_BUFFER, tree.vbo);
        glBufferData(GL_ARRAY_BUFFER, tv.size()*sizeof(PVert), tv.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, tree.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, ti.size()*sizeof(unsigned int), ti.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(PVert),(void*)offsetof(PVert,p));
        glEnableVertexAttribArray(1); glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(PVert),(void*)offsetof(PVert,n));
        glEnableVertexAttribArray(2); glVertexAttrib3f(2, 0.8f, 0.9f, 0.8f);
        // UV attribute at location 3
        glEnableVertexAttribArray(3); glVertexAttribPointer(3,2,GL_FLOAT,GL_FALSE,sizeof(PVert),(void*)offsetof(PVert,uv));
        glBindVertexArray(0);
        tree.indexCount = (GLsizei)ti.size();
    }

    // Attempt to load downloaded glTF models (BoxTextured.gltf and Avocado.gltf)
    GLTFModel boxModel; bool boxLoaded=false;
    GLTFModel avocadoModel; bool avoLoaded=false;
    std::string boxPath = std::string("assets/models/BoxTextured.gltf");
    std::string avoPath = std::string("assets/models/Avocado.gltf");
    if (loadSimpleGLTF(boxPath, boxModel)) {
        std::cout << "Loaded glTF model: " << boxPath << std::endl;
        boxLoaded = true;
    } else {
        // try one level up
        if (loadSimpleGLTF(std::string("../") + boxPath, boxModel)) { boxLoaded = true; }
    }
    if (loadSimpleGLTF(avoPath, avocadoModel)) {
        std::cout << "Loaded glTF model: " << avoPath << std::endl;
        avoLoaded = true;
    } else {
        if (loadSimpleGLTF(std::string("../") + avoPath, avocadoModel)) { avoLoaded = true; }
    }

    float startTime = (float)glfwGetTime();
    glm::vec2 tornadoPos(0.0f, 0.0f);
    Camera camera;
    glm::vec3 camPosVec = camera.pos;

    // --- particles ---
    const int MAX_PARTICLES = 2200;
    // We'll partition the particle pool into inner (dense small dust) and outer (larger debris)
    const int INNER_PARTICLES = 1400;
    const int OUTER_PARTICLES = MAX_PARTICLES - INNER_PARTICLES;
    std::vector<Particle> particles(MAX_PARTICLES);
    // GPU buffer: vec3 pos + float life
    GLuint particleVAO=0, particleVBO=0;
    glGenVertexArrays(1, &particleVAO);
    glGenBuffers(1, &particleVBO);
    glBindVertexArray(particleVAO);
    glBindBuffer(GL_ARRAY_BUFFER, particleVBO);
    glBufferData(GL_ARRAY_BUFFER, MAX_PARTICLES * (sizeof(float)*4), nullptr, GL_STREAM_DRAW);
    // pos
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE, sizeof(float)*4, (void*)0);
    // life
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1,1,GL_FLOAT,GL_FALSE, sizeof(float)*4, (void*)(sizeof(float)*3));
    glBindVertexArray(0);

    std::mt19937 rng((unsigned)std::chrono::high_resolution_clock::now().time_since_epoch().count());
    std::uniform_real_distribution<float> rnd01(0.0f,1.0f);
    auto respawn = [&](Particle &p, bool inner){
        float a = rnd01(rng) * 2.0f * 3.14159f;
        float r = inner ? (0.02f + rnd01(rng) * 0.6f) : (0.6f + rnd01(rng) * (0.6f + rnd01(rng)*1.4f));
        float y = inner ? (0.05f + rnd01(rng)*0.6f) : (0.0f + rnd01(rng)*2.0f);
        p.pos = glm::vec3(r * cosf(a), y, r * sinf(a));
        // inner particles move faster around axis, lighter upwards; outer are heavier and more chaotic
        if (inner) p.vel = glm::vec3(-p.pos.z*3.0f, 2.0f + rnd01(rng)*1.5f, p.pos.x*3.0f);
        else p.vel = glm::vec3(-p.pos.z*1.2f + (rnd01(rng)-0.5f)*2.0f, 0.6f + rnd01(rng)*1.5f, p.pos.x*1.2f + (rnd01(rng)-0.5f)*2.0f);
        p.life = inner ? (0.4f + rnd01(rng)*1.2f) : (0.8f + rnd01(rng)*2.0f);
    };
    for (int i=0;i<INNER_PARTICLES;++i) respawn(particles[i], true);
    for (int i=INNER_PARTICLES;i<MAX_PARTICLES;++i) respawn(particles[i], false);

    // fps counter
    double fpsTimer = glfwGetTime();
    int fpsFrames = 0;

    while (!glfwWindowShouldClose(window)) {
        float now = (float)glfwGetTime();
        float t = now - startTime;

        // Smoothly move tornado toward mouse position in world XZ plane
    glm::vec2 target(g_mouseX * 2.5f, (g_mouseY * -1.0f) * 2.5f); // invert Y so mouse down -> tornado down
        tornadoPos = tornadoPos * 0.92f + target * 0.08f;

        // --- camera input ---
        float dt = 0.016f; // fallback
        static double lastT = glfwGetTime();
        double nowT = glfwGetTime();
        dt = (float)(nowT - lastT);
        lastT = nowT;
        // mouse look when RMB pressed
        if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
            double mx, my; glfwGetCursorPos(window, &mx, &my);
            static double lastMx = mx, lastMy = my;
            double dx = mx - lastMx; double dy = lastMy - my; // inverted
            lastMx = mx; lastMy = my;
            camera.yaw += (float)dx * camera.sensitivity;
            camera.pitch += (float)dy * camera.sensitivity;
            if (camera.pitch > 89.0f) camera.pitch = 89.0f;
            if (camera.pitch < -89.0f) camera.pitch = -89.0f;
        }
        // WASD movement
        glm::vec3 forward;
        forward.x = cos(glm::radians(camera.yaw)) * cos(glm::radians(camera.pitch));
        forward.y = sin(glm::radians(camera.pitch));
        forward.z = sin(glm::radians(camera.yaw)) * cos(glm::radians(camera.pitch));
        forward = glm::normalize(forward);
        glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0,1,0)));
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) camera.pos += forward * camera.speed * dt;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) camera.pos -= forward * camera.speed * dt;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) camera.pos -= right * camera.speed * dt;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) camera.pos += right * camera.speed * dt;
        camPosVec = camera.pos;

        // clear
        int width, heightW;
        glfwGetFramebufferSize(window, &width, &heightW);
    glViewport(0,0,width,heightW);
    // sky gradient-ish background (single color here)
    glClearColor(0.18f, 0.22f, 0.45f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(program);

        // set uniforms
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), width / (float)heightW, 0.1f, 100.0f);
        glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 2.0f, 8.0f), glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(0.0f,1.0f,0.0f));
        glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(tornadoPos.x, -1.0f, tornadoPos.y));
        model = glm::rotate(model, t * 1.6f, glm::vec3(0.0f,1.0f,0.0f));

    GLint locProj = glGetUniformLocation(program, "uProj");
    GLint locView = glGetUniformLocation(program, "uView");
    GLint locModel = glGetUniformLocation(program, "uModel");
    GLint locTime = glGetUniformLocation(program, "uTime");
    GLint locCam = glGetUniformLocation(program, "uCamPos");

    glUniformMatrix4fv(locProj, 1, GL_FALSE, glm::value_ptr(proj));
    // update view from camera
    view = camera.getView();
    glUniformMatrix4fv(locView, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(locModel, 1, GL_FALSE, glm::value_ptr(model));
    glUniform1f(locTime, t);
    if (locCam >= 0) glUniform3f(locCam, camPosVec.x, camPosVec.y, camPosVec.z);

        // Set common uniforms
        GLint locEnableSwirl = glGetUniformLocation(program, "uEnableSwirl");
        GLint locTint = glGetUniformLocation(program, "uTint");

    // Draw ground
        glm::mat4 groundModel = glm::mat4(1.0f);
        GLint locModelG = glGetUniformLocation(program, "uModel");
    GLint locOpacity = glGetUniformLocation(program, "uOpacity");
        if (locModelG >= 0) glUniformMatrix4fv(locModelG, 1, GL_FALSE, glm::value_ptr(groundModel));
        if (locEnableSwirl >= 0) glUniform1f(locEnableSwirl, 0.0f);
        if (locTint >= 0) glUniform3f(locTint, 0.6f, 0.9f, 0.6f);
    if (locOpacity >= 0) glUniform1f(locOpacity, 0.95f);
        GLint locObjType = glGetUniformLocation(program, "uObjType");
        if (locObjType >= 0) glUniform1i(locObjType, 3); // ground
        glBindVertexArray(groundVAO);
        glDrawElements(GL_TRIANGLES, (GLsizei)groundIdx.size(), GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);

        // Draw shadow under tornado (simple dark disc)
        static GLuint shadowVAO = 0, shadowVBO = 0;
        if (!shadowVAO) {
            const int SSEG = 48;
            std::vector<glm::vec3> sverts(SSEG+2);
            sverts[0] = glm::vec3(0.0f, 0.001f, 0.0f);
            for (int i=0;i<=SSEG;i++) {
                float a = (float)i / (float)SSEG * 2.0f * (float)M_PI;
                sverts[i+1] = glm::vec3(cosf(a)*1.6f, 0.001f, sinf(a)*1.6f);
            }
            std::vector<unsigned int> sidx;
            for (int i=1;i<=SSEG;i++) { sidx.push_back(0); sidx.push_back(i); sidx.push_back(i+1); }
            glGenVertexArrays(1, &shadowVAO);
            glGenBuffers(1, &shadowVBO);
            GLuint sEBO; glGenBuffers(1, &sEBO);
            glBindVertexArray(shadowVAO);
            glBindBuffer(GL_ARRAY_BUFFER, shadowVBO);
            glBufferData(GL_ARRAY_BUFFER, sverts.size()*sizeof(glm::vec3), sverts.data(), GL_STATIC_DRAW);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sEBO);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, sidx.size()*sizeof(unsigned int), sidx.data(), GL_STATIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(glm::vec3),(void*)0);
            glBindVertexArray(0);
        }
        // shadow draw uses program with a dark tint
        glm::mat4 shadowM = glm::translate(glm::mat4(1.0f), glm::vec3(tornadoPos.x, -1.0f + 0.01f, tornadoPos.y));
        if (locModelG >= 0) glUniformMatrix4fv(locModelG, 1, GL_FALSE, glm::value_ptr(shadowM));
        if (locTint >= 0) glUniform3f(locTint, 0.02f, 0.03f, 0.02f);
        if (locOpacity >= 0) glUniform1f(locOpacity, 0.6f);
        if (locObjType >= 0) glUniform1i(locObjType, 0);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBindVertexArray(shadowVAO);
        // shadow index count SSEG*3
        glDrawElements(GL_TRIANGLES, 48*3, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);

        // Draw a few simple buildings (stacked boxes)
        std::vector<glm::vec3> bpos = {{-3.0f,0.0f,-2.0f},{2.5f,0.0f,1.0f},{-1.2f,0.0f,3.0f}};
    for (size_t i=0;i<bpos.size();++i) {
            float h = 1.0f + i * 0.8f;
            // place house base at ground (model base y == 0), then scale height
            glm::mat4 bm = glm::translate(glm::mat4(1.0f), bpos[i] + glm::vec3(0.0f, 0.0f, 0.0f));
            bm = glm::scale(bm, glm::vec3(1.0f, h, 1.0f));
            if (locModelG >= 0) glUniformMatrix4fv(locModelG, 1, GL_FALSE, glm::value_ptr(bm));
            if (locEnableSwirl >= 0) glUniform1f(locEnableSwirl, 0.0f);
            if (locTint >= 0) glUniform3f(locTint, 0.8f - i*0.1f, 0.8f - i*0.1f, 0.85f - i*0.05f);
            if (locOpacity >= 0) glUniform1f(locOpacity, 0.95f);
            // Use boxModel for all houses when available, otherwise fallback to procedural
            if (boxLoaded) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, boxModel.texture);
                GLint locHasTex = glGetUniformLocation(program, "uHasAlbedo"); if (locHasTex>=0) glUniform1i(locHasTex, 1);
                GLint locAlbedo = glGetUniformLocation(program, "uAlbedo"); if (locAlbedo>=0) glUniform1i(locAlbedo, 0);
                glBindVertexArray(boxModel.vao);
                glDrawElements(GL_TRIANGLES, boxModel.indexCount, GL_UNSIGNED_INT, 0);
                glBindVertexArray(0);
                GLint locHasTex0 = glGetUniformLocation(program, "uHasAlbedo"); if (locHasTex0>=0) glUniform1i(locHasTex0, 0);
            } else {
                if (locObjType >= 0) glUniform1i(locObjType, 1); // house
                // fallback: procedural house (walls = brick)
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, brickTex);
                GLint locHasTex = glGetUniformLocation(program, "uHasAlbedo"); if (locHasTex>=0) glUniform1i(locHasTex, 1);
                GLint locAlbedo = glGetUniformLocation(program, "uAlbedo"); if (locAlbedo>=0) glUniform1i(locAlbedo, 0);
                glBindVertexArray(house.vao);
                glDrawElements(GL_TRIANGLES, house.indexCount, GL_UNSIGNED_INT, 0);
                glBindVertexArray(0);
                GLint locHasTex0 = glGetUniformLocation(program, "uHasAlbedo"); if (locHasTex0>=0) glUniform1i(locHasTex0, 0);
            }
            // small roof box
            // position roof at top of scaled house
            glm::mat4 roof = glm::translate(glm::mat4(1.0f), bpos[i] + glm::vec3(0.0f, h + 0.15f, 0.0f));
            roof = glm::scale(roof, glm::vec3(0.9f, 0.3f, 0.9f));
            if (locModelG >= 0) glUniformMatrix4fv(locModelG, 1, GL_FALSE, glm::value_ptr(roof));
            if (locTint >= 0) glUniform3f(locTint, 0.6f,0.35f,0.35f);
            if (locOpacity >= 0) glUniform1f(locOpacity, 0.95f);
            if (locObjType >= 0) glUniform1i(locObjType, 1); // roof also house
            // draw roof using wood texture only when not using glTF box
            if (!boxLoaded) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, woodTex);
                GLint locHasRoof = glGetUniformLocation(program, "uHasAlbedo"); if (locHasRoof>=0) glUniform1i(locHasRoof, 1);
                GLint locRoofAlbedo = glGetUniformLocation(program, "uAlbedo"); if (locRoofAlbedo>=0) glUniform1i(locRoofAlbedo, 0);
                glBindVertexArray(house.vao);
                glDrawElements(GL_TRIANGLES, house.indexCount, GL_UNSIGNED_INT, 0);
                glBindVertexArray(0);
                GLint locHasRoof0 = glGetUniformLocation(program, "uHasAlbedo"); if (locHasRoof0>=0) glUniform1i(locHasRoof0, 0);
            }
        }

        // Draw some simple trees (cone + trunk)
        std::vector<glm::vec3> tpos = {{-4.0f,0.0f,-3.5f},{3.0f,0.0f,-1.5f},{1.8f,0.0f,4.0f}};
        for (size_t i=0;i<tpos.size();++i) {
            // trunk
            // trunk base at ground (model base y == 0)
            glm::mat4 tm = glm::translate(glm::mat4(1.0f), tpos[i] + glm::vec3(0.0f,0.0f,0.0f));
            tm = glm::scale(tm, glm::vec3(0.2f,0.5f,0.2f));
            if (locModelG >= 0) glUniformMatrix4fv(locModelG, 1, GL_FALSE, glm::value_ptr(tm));
            if (locTint >= 0) glUniform3f(locTint, 0.45f,0.25f,0.15f);
            if (locEnableSwirl >= 0) glUniform1f(locEnableSwirl, 0.0f);
            if (locObjType >= 0) glUniform1i(locObjType, 2); // trunk/tree
            if (avoLoaded) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, avocadoModel.texture);
                GLint locHasTex = glGetUniformLocation(program, "uHasAlbedo"); if (locHasTex>=0) glUniform1i(locHasTex, 1);
                GLint locAlbedo = glGetUniformLocation(program, "uAlbedo"); if (locAlbedo>=0) glUniform1i(locAlbedo, 0);
                glBindVertexArray(avocadoModel.vao);
                glDrawElements(GL_TRIANGLES, avocadoModel.indexCount, GL_UNSIGNED_INT, 0);
                glBindVertexArray(0);
                GLint locHasTex0 = glGetUniformLocation(program, "uHasAlbedo"); if (locHasTex0>=0) glUniform1i(locHasTex0, 0);
            } else {
                // fallback: procedural tree (trunk = wood)
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, woodTex);
                GLint locHasTexT = glGetUniformLocation(program, "uHasAlbedo"); if (locHasTexT>=0) glUniform1i(locHasTexT, 1);
                GLint locAlbedoT = glGetUniformLocation(program, "uAlbedo"); if (locAlbedoT>=0) glUniform1i(locAlbedoT, 0);
                glBindVertexArray(tree.vao);
                glDrawElements(GL_TRIANGLES, tree.indexCount, GL_UNSIGNED_INT, 0);
                glBindVertexArray(0);
                GLint locHasTexT0 = glGetUniformLocation(program, "uHasAlbedo"); if (locHasTexT0>=0) glUniform1i(locHasTexT0, 0);
            }
            glBindVertexArray(0);
            // foliage: simple stacked scaled cubes approximating cones
            // place foliage above trunk height (trunk scaled to 0.5)
            glm::mat4 fm = glm::translate(glm::mat4(1.0f), tpos[i] + glm::vec3(0.0f,0.5f,0.0f));
            fm = glm::scale(fm, glm::vec3(0.6f,0.6f,0.6f));
            if (locModelG >= 0) glUniformMatrix4fv(locModelG, 1, GL_FALSE, glm::value_ptr(fm));
            if (locTint >= 0) glUniform3f(locTint, 0.1f,0.6f,0.15f);
            if (locOpacity >= 0) glUniform1f(locOpacity, 0.95f);
            if (locObjType >= 0) glUniform1i(locObjType, 2); // foliage
            // draw foliage with leaf texture
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, leafTex);
            GLint locHasLeaf = glGetUniformLocation(program, "uHasAlbedo"); if (locHasLeaf>=0) glUniform1i(locHasLeaf, 1);
            GLint locLeafAlbedo = glGetUniformLocation(program, "uAlbedo"); if (locLeafAlbedo>=0) glUniform1i(locLeafAlbedo, 0);
            glBindVertexArray(tree.vao);
            glDrawElements(GL_TRIANGLES, tree.indexCount, GL_UNSIGNED_INT, 0);
            glBindVertexArray(0);
            GLint locHasLeaf0 = glGetUniformLocation(program, "uHasAlbedo"); if (locHasLeaf0>=0) glUniform1i(locHasLeaf0, 0);
        }

    // Restore tornado model matrix and enable swirl only for tornado
    if (locModel >= 0) glUniformMatrix4fv(locModel, 1, GL_FALSE, glm::value_ptr(model));
    if (locEnableSwirl >= 0) glUniform1f(locEnableSwirl, 1.0f);
    if (locTint >= 0) glUniform3f(locTint, 0.95f,0.95f,1.0f);
    if (locOpacity >= 0) glUniform1f(locOpacity, 0.9f);
    if (locObjType >= 0) glUniform1i(locObjType, 0); // tornado default path
    glBindVertexArray(vao);
    // Draw using indices
    glDrawElements(GL_TRIANGLES, (GLsizei)indices.size(), GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);

        // --- update particles ---
        for (int i=0;i<MAX_PARTICLES;++i) {
            Particle &p = particles[i];
            p.life -= dt;
            bool isInner = (i < INNER_PARTICLES);
            if (p.life <= 0.0f) respawn(p, isInner);
            // motion: inner particles are more tightly bound and accelerate more around axis
            glm::vec3 acc = isInner ? glm::vec3(-p.pos.z*3.0f, -1.2f, p.pos.x*3.0f) : glm::vec3(-p.pos.z*1.0f, -0.6f, p.pos.x*1.0f);
            p.vel += acc * dt * 0.6f;
            // slight random jitter for outer particles
            if (!isInner) {
                p.vel += glm::vec3((rnd01(rng)-0.5f)*0.8f, (rnd01(rng)-0.5f)*0.6f, (rnd01(rng)-0.5f)*0.8f) * dt * 6.0f;
            }
            p.pos += p.vel * dt;
        }
        // upload particle buffer
        std::vector<float> buf(MAX_PARTICLES * 4);
        for (int i=0;i<MAX_PARTICLES;++i) {
            buf[i*4+0] = particles[i].pos.x + tornadoPos.x;
            buf[i*4+1] = particles[i].pos.y - 1.0f;
            buf[i*4+2] = particles[i].pos.z + tornadoPos.y;
            buf[i*4+3] = glm::clamp(particles[i].life / 3.0f, 0.0f, 1.0f); // scale life for smoother fade
        }
        glBindBuffer(GL_ARRAY_BUFFER, particleVBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, buf.size()*sizeof(float), buf.data());

    // render particles (additive)
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glUseProgram(particleProgram);
        GLint plocProj = glGetUniformLocation(particleProgram, "uProj");
        GLint plocView = glGetUniformLocation(particleProgram, "uView");
        GLint plocModel = glGetUniformLocation(particleProgram, "uModel");
        GLint plocColor = glGetUniformLocation(particleProgram, "uColor");
    if (plocProj>=0) glUniformMatrix4fv(plocProj,1,GL_FALSE,glm::value_ptr(proj));
    if (plocView>=0) glUniformMatrix4fv(plocView,1,GL_FALSE,glm::value_ptr(view));
    glm::mat4 pm = glm::translate(glm::mat4(1.0f), glm::vec3(tornadoPos.x, -1.0f, tornadoPos.y));
    if (plocModel>=0) glUniformMatrix4fv(plocModel,1,GL_FALSE,glm::value_ptr(pm));
    // Draw inner particle layer (dense, lighter dust)
    if (plocColor>=0) glUniform3f(plocColor, 0.72f,0.66f,0.55f); // dusty light gray-brown
    GLint plocPoint = glGetUniformLocation(particleProgram, "uPointScale");
    if (plocPoint>=0) glUniform1f(plocPoint, 1.0f);
    glBindVertexArray(particleVAO);
    glEnable(GL_PROGRAM_POINT_SIZE);
    glDrawArrays(GL_POINTS, 0, INNER_PARTICLES);
    // Draw outer particle layer (larger, darker debris)
    if (plocColor>=0) glUniform3f(plocColor, 0.35f,0.30f,0.28f);
    if (plocPoint>=0) glUniform1f(plocPoint, 2.2f);
    glDrawArrays(GL_POINTS, INNER_PARTICLES, OUTER_PARTICLES);
    glBindVertexArray(0);
    // restore blending for opaque geometry
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // restore default program for objects
        glUseProgram(program);

        // FPS counter update
        fpsFrames++;
        double nowF = glfwGetTime();
        if (nowF - fpsTimer >= 1.0) {
            int fps = (int)((double)fpsFrames / (nowF - fpsTimer));
            fpsFrames = 0;
            fpsTimer = nowF;
            std::string title = "Tornada 3D - FPS: " + std::to_string(fps);
            glfwSetWindowTitle(window, title.c_str());
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteBuffers(1, &vbo);
    glDeleteBuffers(1, &ebo);
    glDeleteVertexArrays(1, &vao);
    // cleanup ground
    glDeleteBuffers(1, &groundVBO);
    glDeleteBuffers(1, &groundEBO);
    glDeleteVertexArrays(1, &groundVAO);
    // cleanup loaded models
    if (house.vao) { glDeleteBuffers(1, &house.vbo); glDeleteBuffers(1, &house.ebo); glDeleteVertexArrays(1, &house.vao); }
    if (tree.vao) { glDeleteBuffers(1, &tree.vbo); glDeleteBuffers(1, &tree.ebo); glDeleteVertexArrays(1, &tree.vao); }
    // particles cleanup
    glDeleteBuffers(1, &particleVBO);
    glDeleteVertexArrays(1, &particleVAO);
    glDeleteProgram(particleProgram);
    glDeleteProgram(program);

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
