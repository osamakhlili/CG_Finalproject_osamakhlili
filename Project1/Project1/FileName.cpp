// ============================================================
//  Ancient Temple Escape — OpenGL 3.3 Core Profile
//  Libraries: GLEW, GLFW, GLM, STB_image
//
//  CONTROLS:
//    W A S D  — Move
//    Mouse    — Look around
//    ESC      — Quit
//
//  GOAL:
//    Collect 3 golden artifacts hidden in the temple,
//    then reach the altar (exit) before the torch timer runs out.
// ============================================================
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <GLM/glm.hpp>
#include <GLM/gtc/matrix_transform.hpp>
#include <GLM/gtc/type_ptr.hpp>
#include <GL/glew.h>         
#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>
#include <string>
#include <cmath>
// ============================================================
//  SETTINGS
// ============================================================
const int SCR_W = 1024;
const int SCR_H = 768;
// ============================================================
//  CAMERA  (First-person)
// ============================================================
glm::vec3 camPos = glm::vec3(1.5f, 0.6f, 1.5f);
glm::vec3 camFront = glm::vec3(0.0f, 0.0f, 1.0f);
glm::vec3 camUp = glm::vec3(0.0f, 1.0f, 0.0f);
float yaw = 90.0f;
float pitch = 0.0f;
float lastX = SCR_W / 2.0f;
float lastY = SCR_H / 2.0f;
bool  firstMouse = true;
float deltaTime = 0.0f;
float lastFrame = 0.0f;
// ============================================================
//  GAME STATE
// ============================================================
int   artifactsCollected = 0;
const int TOTAL_ARTIFACTS = 6;
bool  altarOpen = false;
bool  gameWon = false;
bool  gameLost = false;
float timeLimit = 60.0f;
float elapsedTime = 0.0f;
// ============================================================
//  MAP  12×12
//  1 = stone wall
//  0 = walkable floor
//  A = artifact (golden relic)
//  X = altar / exit
//  S = start position
// ============================================================
const int MAP_W = 20;
const int MAP_H = 20;
char gameMap[MAP_H][MAP_W + 1] = {
   "111111111A1111111111",
"1S000000000000000001",
"101111011110A1110101",
"10100A01000010000101",
"1010111A010110100101",
"10100000010000100001",
"11110111011101111101",
"10000100A10001000001",
"10111101110111011101",
"10000000000000000001",
"10111011101110111001",
"10A01010001010001001",
"10101010101010101001",
"10100000000000000001",
"10111111111111110101",
"10000000A00000000001",
"10111011101110111101",
"100010100010100X0001",
"100000000000A0000001",
"11111111111111111111"
};
// Artifact positions
struct Artifact {
    float x, z;
    bool  collected;
    float bobOffset;
};
std::vector<Artifact> artifacts;
float altarX = 0.0f, altarZ = 0.0f;
// ============================================================
//  COLLISION
// ============================================================
bool isWall(float px, float pz)
{
    int col = (int)floorf(px);
    int row = (int)floorf(pz);
    if (col < 0 || col >= MAP_W || row < 0 || row >= MAP_H) return true;
    char c = gameMap[row][col];
    return (c == '1');
}
// ============================================================
//  SHADERS
// ============================================================
const char* vertSrc = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
out vec3 vFragPos;
out vec3 vNormal;
out vec2 vUV;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
void main()
{
    vec4 worldPos = model * vec4(aPos, 1.0);
    vFragPos      = vec3(worldPos);
    vNormal       = mat3(transpose(inverse(model))) * aNormal;
    vUV           = aUV;
    gl_Position   = projection * view * worldPos;
}
)";
const char* fragLitSrc = R"(
#version 330 core
in vec3 vFragPos;
in vec3 vNormal;
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D tex0;
uniform vec3  lightPos;
uniform vec3  lightColor;
uniform float ambientStr;
uniform vec3  viewPos;
uniform float fogDensity;
void main()
{
    // Ambient
    vec3 ambient = ambientStr * lightColor;
    // Diffuse
    vec3  norm     = normalize(vNormal);
    vec3  lightDir = normalize(lightPos - vFragPos);
    float diff     = max(dot(norm, lightDir), 0.0);
    float dist     = length(lightPos - vFragPos);
    float attn     = 1.0 / (1.0 + 0.14 * dist + 0.07 * dist * dist);
    vec3  diffuse  = diff * lightColor * attn;
    // Specular
    vec3  viewDir  = normalize(viewPos - vFragPos);
    vec3  halfDir  = normalize(lightDir + viewDir);
    float spec     = pow(max(dot(norm, halfDir), 0.0), 16.0);
    vec3  specular = spec * lightColor * 0.15 * attn;
    vec4 texColor = texture(tex0, vUV);
    vec3 result   = (ambient + diffuse + specular) * vec3(texColor);
    // Distance fog
    float fogFactor = exp(-fogDensity * dist);
    fogFactor       = clamp(fogFactor, 0.0, 1.0);
    vec3  fogColor  = vec3(0.04, 0.01, 0.08);
    result          = mix(fogColor, result, fogFactor);
    fragColor = vec4(result, texColor.a);
}
)";
const char* fragTintSrc = R"(
#version 330 core
in vec3 vFragPos;
in vec3 vNormal;
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D tex0;
uniform vec3  tint;
uniform float glowStr;
void main()
{
    vec4 texColor = texture(tex0, vUV);
    fragColor     = vec4(vec3(texColor) * tint * glowStr, texColor.a);
}
)";
// ============================================================
//  HELPER: compile + link shader program
// ============================================================
GLuint makeProgram(const char* vs, const char* fs)
{
    auto compile = [](GLenum type, const char* src) -> GLuint {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        int ok; char log[512];
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) { glGetShaderInfoLog(s, 512, nullptr, log); std::cout << "Shader error:\n" << log << "\n"; }
        return s;
        };
    GLuint v = compile(GL_VERTEX_SHADER, vs);
    GLuint f = compile(GL_FRAGMENT_SHADER, fs);
    GLuint p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f);
    glLinkProgram(p);
    int ok; char log[512];
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { glGetProgramInfoLog(p, 512, nullptr, log); std::cout << "Link error:\n" << log << "\n"; }
    glDeleteShader(v); glDeleteShader(f);
    return p;
}
// ============================================================
//  HELPER: load texture
// ============================================================
GLuint loadTex(const char* path)
{
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    int w, h, ch;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(path, &w, &h, &ch, 0);
    if (data) {
        GLenum fmt = (ch == 4) ? GL_RGBA : GL_RGB;
        glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        stbi_image_free(data);
        std::cout << "Loaded: " << path << "\n";
    }
    else {
        unsigned char px[] = { 200, 180, 120, 255 };
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        std::cout << "Warning: texture not found: " << path << "\n";
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return tex;
}
// ============================================================
//  GEOMETRY: unit cube  (pos + normal + uv)
// ============================================================
GLuint buildCubeVAO()
{
    float v[] = {
        // pos          normal       uv
        // Front (z=1)
        0,0,1,  0,0,1,  0,0,
        1,0,1,  0,0,1,  1,0,
        1,1,1,  0,0,1,  1,1,
        0,1,1,  0,0,1,  0,1,
        // Back (z=0)
        1,0,0,  0,0,-1, 0,0,
        0,0,0,  0,0,-1, 1,0,
        0,1,0,  0,0,-1, 1,1,
        1,1,0,  0,0,-1, 0,1,
        // Left (x=0)
        0,0,0, -1,0,0,  0,0,
        0,0,1, -1,0,0,  1,0,
        0,1,1, -1,0,0,  1,1,
        0,1,0, -1,0,0,  0,1,
        // Right (x=1)
        1,0,1,  1,0,0,  0,0,
        1,0,0,  1,0,0,  1,0,
        1,1,0,  1,0,0,  1,1,
        1,1,1,  1,0,0,  0,1,
        // Top (y=1)
        0,1,1,  0,1,0,  0,0,
        1,1,1,  0,1,0,  1,0,
        1,1,0,  0,1,0,  1,1,
        0,1,0,  0,1,0,  0,1,
        // Bottom (y=0)
        0,0,0,  0,-1,0, 0,0,
        1,0,0,  0,-1,0, 1,0,
        1,0,1,  0,-1,0, 1,1,
        0,0,1,  0,-1,0, 0,1,
    };
    unsigned int idx[] = {
         0, 1, 2,  2, 3, 0,
         4, 5, 6,  6, 7, 4,
         8, 9,10, 10,11, 8,
        12,13,14, 14,15,12,
        16,17,18, 18,19,16,
        20,21,22, 22,23,20
    };
    GLuint VAO, VBO, EBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);
    int stride = 8 * sizeof(float);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);
    return VAO;
}
// ============================================================
//  GEOMETRY: flat quad for floor / ceiling
// ============================================================
GLuint buildQuadVAO()
{
    float v[] = {
        0,0,0,  0,1,0,  0,0,
        1,0,0,  0,1,0,  1,0,
        1,0,1,  0,1,0,  1,1,
        0,0,1,  0,1,0,  0,1,
    };
    unsigned int idx[] = { 0,1,2, 2,3,0 };
    GLuint VAO, VBO, EBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);
    int stride = 8 * sizeof(float);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);
    return VAO;
}
// ============================================================
//  DRAW HELPERS
// ============================================================
void setMVP(GLuint prog, glm::mat4& model, glm::mat4& view, glm::mat4& proj)
{
    glUniformMatrix4fv(glGetUniformLocation(prog, "model"), 1, GL_FALSE, glm::value_ptr(model));
    glUniformMatrix4fv(glGetUniformLocation(prog, "view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(prog, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
}
void drawCube(GLuint cubeVAO, GLuint prog,
    glm::vec3 pos, glm::vec3 scale,
    GLuint tex, glm::mat4& view, glm::mat4& proj)
{
    glm::mat4 model = glm::scale(glm::translate(glm::mat4(1.0f), pos), scale);
    setMVP(prog, model, view, proj);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(glGetUniformLocation(prog, "tex0"), 0);
    glBindVertexArray(cubeVAO);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
}
void drawQuad(GLuint quadVAO, GLuint prog,
    float col, float row, float yOff,
    GLuint tex, glm::mat4& view, glm::mat4& proj)
{
    glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(col, yOff, row));
    setMVP(prog, model, view, proj);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(glGetUniformLocation(prog, "tex0"), 0);
    glBindVertexArray(quadVAO);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
}
// ============================================================
//  CALLBACKS
// ============================================================
void mouseCallback(GLFWwindow*, double xpos, double ypos)
{
    if (firstMouse) { lastX = (float)xpos; lastY = (float)ypos; firstMouse = false; }
    float dx = (float)xpos - lastX;
    float dy = lastY - (float)ypos;
    lastX = (float)xpos;
    lastY = (float)ypos;
    yaw += dx * 0.1f;
    pitch += dy * 0.1f;
    pitch = glm::clamp(pitch, -89.0f, 89.0f);
    glm::vec3 front;
    front.x = cosf(glm::radians(yaw)) * cosf(glm::radians(pitch));
    front.y = sinf(glm::radians(pitch));
    front.z = sinf(glm::radians(yaw)) * cosf(glm::radians(pitch));
    camFront = glm::normalize(front);
}
void processInput(GLFWwindow* window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);
    if (gameWon || gameLost) return;
    float speed = 2.5f * deltaTime;
    glm::vec3 flatFront = glm::normalize(glm::vec3(camFront.x, 0.0f, camFront.z));
    glm::vec3 right = glm::normalize(glm::cross(flatFront, camUp));
    glm::vec3 newPos = camPos;
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) newPos += speed * flatFront;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) newPos -= speed * flatFront;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) newPos -= speed * right;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) newPos += speed * right;
    float r = 0.28f;
    if (!isWall(newPos.x + r, camPos.z) && !isWall(newPos.x - r, camPos.z))
        camPos.x = newPos.x;
    if (!isWall(camPos.x, newPos.z + r) && !isWall(camPos.x, newPos.z - r))
        camPos.z = newPos.z;
    camPos.y = 0.6f;
}
// ============================================================
//  MAIN
// ============================================================
int main()
{
    // ---- GLFW ---
    if (!glfwInit()) { std::cout << "GLFW init failed\n"; return -1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(SCR_W, SCR_H,
        "Ancient Temple — OpenGL 3.3",
        nullptr, nullptr);
    if (!window) { std::cout << "Window failed\n"; glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    glfwSetCursorPosCallback(window, mouseCallback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    // ---- GLEW Init  ---
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK)
    {
        std::cout << "GLEW init failed\n";
        return -1;
    }
    glEnable(GL_DEPTH_TEST);
    glViewport(0, 0, SCR_W, SCR_H);
    // ---- Geometry ---
    GLuint cubeVAO = buildCubeVAO();
    GLuint quadVAO = buildQuadVAO();
    // ---- Shaders ---
    GLuint litProg = makeProgram(vertSrc, fragLitSrc);
    GLuint tintProg = makeProgram(vertSrc, fragTintSrc);
    // ---- Textures ---
    GLuint texStone = loadTex("stone.bmp");
    GLuint texFloor = loadTex("sand.bmp");
    GLuint texCeil = loadTex("ceiling.bmp");
    GLuint texAltar = loadTex("altar.bmp");
    GLuint texRelic = loadTex("Crate.bmp");
    // ---- Parse map ---
    for (int row = 0; row < MAP_H; row++) {
        for (int col = 0; col < MAP_W; col++) {
            int slen = (int)strlen(gameMap[row]);
            char c = (col < slen) ? gameMap[row][col] : '0';
            if (c == 'A') {
                float phase = (float)(artifacts.size()) * 1.2f;
                artifacts.push_back({ (float)col + 0.5f, (float)row + 0.5f, false, phase });
            }
            if (c == 'X') { altarX = (float)col; altarZ = (float)row; }
            if (c == 'S') camPos = glm::vec3((float)col + 0.5f, 0.6f, (float)row + 0.5f);
        }
    }
    glm::vec3 lightColor = glm::vec3(0.4f, 0.3f, 1.0f); // warm torch orange
    // ================================================================
    //  RENDER LOOP
    // ================================================================
    while (!glfwWindowShouldClose(window))
    {
        float now = (float)glfwGetTime();
        deltaTime = now - lastFrame;
        lastFrame = now;
        if (!gameWon && !gameLost) elapsedTime += deltaTime;
        if (elapsedTime >= timeLimit && !gameWon) gameLost = true;
        processInput(window);
        // Torch flicker
        float flicker = 0.9f + 0.1f * sinf(now * 13.7f) * cosf(now * 7.3f);
        glm::vec3 lightPos = camPos + glm::vec3(0.0f, 0.3f, 0.0f) + camFront * 0.5f;
        // ---- Artifact pickup ---
        for (auto& a : artifacts) {
            if (!a.collected) {
                float dx = camPos.x - a.x;
                float dz = camPos.z - a.z;
                if (sqrtf(dx * dx + dz * dz) < 0.65f) {
                    a.collected = true;
                    artifactsCollected++;
                    if (artifactsCollected >= TOTAL_ARTIFACTS) {
                        altarOpen = true;
                        std::cout << "All relics found! Reach the altar!\n";
                    }
                    else {
                        std::cout << "Relic " << artifactsCollected
                            << "/" << TOTAL_ARTIFACTS << " collected!\n";
                    }
                }
            }
        }
        // ---- Win condition ---
        if (altarOpen && !gameWon) {
            float dx = camPos.x - (altarX + 0.5f);
            float dz = camPos.z - (altarZ + 0.5f);
            if (sqrtf(dx * dx + dz * dz) < 0.9f) {
                gameWon = true;
                std::cout << "YOU ESCAPED THE TEMPLE!\n";
            }
        }
        // ---- Clear ---
        glm::vec3 sky = gameWon ? glm::vec3(0.6f, 0.5f, 0.1f)
            : gameLost ? glm::vec3(0.0f, 0.0f, 0.0f)
            : glm::vec3(0.01f, 0.005f, 0.0f);
        glClearColor(sky.r, sky.g, sky.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        // ---- Matrices ---
        glm::mat4 view = glm::lookAt(camPos, camPos + camFront, camUp);
        glm::mat4 proj = glm::perspective(glm::radians(70.0f),
            (float)SCR_W / (float)SCR_H,
            0.05f, 30.0f);
        // ---- Lighting uniforms ---
        glUseProgram(litProg);
        glUniform3fv(glGetUniformLocation(litProg, "lightPos"), 1, glm::value_ptr(lightPos));
        glUniform3fv(glGetUniformLocation(litProg, "lightColor"), 1, glm::value_ptr(lightColor * flicker));
        glUniform1f(glGetUniformLocation(litProg, "ambientStr"), 0.08f);
        glUniform3fv(glGetUniformLocation(litProg, "viewPos"), 1, glm::value_ptr(camPos));
        glUniform1f(glGetUniformLocation(litProg, "fogDensity"), 0.18f);
        // ---- Draw map ---
        for (int row = 0; row < MAP_H; row++) {
            for (int col = 0; col < MAP_W; col++) {
                int slen = (int)strlen(gameMap[row]);
                char c = (col < slen) ? gameMap[row][col] : '0';
                glUseProgram(litProg);
                drawQuad(quadVAO, litProg, (float)col, (float)row, 0.0f, texFloor, view, proj);
                drawQuad(quadVAO, litProg, (float)col, (float)row, 1.2f, texCeil, view, proj);
                if (c == '1') {
                    drawCube(cubeVAO, litProg,
                        glm::vec3((float)col, 0.0f, (float)row),
                        glm::vec3(1.0f, 1.2f, 1.0f),
                        texStone, view, proj);

                }
                if (c == 'X') {
                    glm::vec3 altarTint = altarOpen
                        ? glm::vec3(1.0f, 0.1f, 0.1f)
                        : glm::vec3(0.4f, 0.3f, 0.2f);
                    float glow = altarOpen ? (1.5f + 0.5f * sinf(now * 3.0f)) : 0.8f;
                    glUseProgram(tintProg);
                    glUniform3fv(glGetUniformLocation(tintProg, "tint"), 1, glm::value_ptr(altarTint));
                    glUniform1f(glGetUniformLocation(tintProg, "glowStr"), glow);
                    drawCube(cubeVAO, tintProg,
                        glm::vec3((float)col, 0.0f, (float)row),
                        glm::vec3(1.0f, 0.4f, 1.0f),
                        texAltar, view, proj);
                }
            }
        }
        // ---- Draw artifacts (bobbing + rotating) ---
        float rotAngle = now * 80.0f;
        for (auto& a : artifacts) {
            if (a.collected) continue;
            float bob = 0.25f + 0.12f * sinf(now * 3.0f + a.bobOffset);
            float glow = 1.6f + 0.6f * sinf(now * 0.6f + a.bobOffset);
            glm::vec3 relicTint = glm::vec3(0.0f, 1.0f, 0.9f);
            glUseProgram(tintProg);
            glUniform3fv(glGetUniformLocation(tintProg, "tint"), 1, glm::value_ptr(relicTint));
            glUniform1f(glGetUniformLocation(tintProg, "glowStr"), glow);
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, glm::vec3(a.x, bob, a.z));
            model = glm::rotate(model, glm::radians(rotAngle + a.bobOffset * 30.0f), glm::vec3(0, 1, 0));
            model = glm::scale(model, glm::vec3(0.22f));
            model = glm::translate(model, glm::vec3(-0.5f, 0.0f, -0.5f));
            setMVP(tintProg, model, view, proj);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texRelic);
            glUniform1i(glGetUniformLocation(tintProg, "tex0"), 0);
            glBindVertexArray(cubeVAO);
            glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
        }
        // ---- Window title HUD ---
        float timeLeft = timeLimit - elapsedTime;
        float torchPct = timeLeft / timeLimit * 100.0f;
        std::string title;
        if (gameWon)
            title = "*** GLORY! You escaped the Ancient Temple! Press ESC ***";
        else if (gameLost)
            title = "*** The torch went out... You are lost forever. Press ESC ***";
        else
            title = "Ancient Temple  |  Relics: "
            + std::to_string(artifactsCollected) + "/" + std::to_string(TOTAL_ARTIFACTS)
            + "  |  Torch: " + std::to_string((int)torchPct) + "%"
            + "  |  Time: " + std::to_string((int)timeLeft) + "s";
        glfwSetWindowTitle(window, title.c_str());
        glfwSwapBuffers(window);
        glfwPollEvents();
    }
    glfwTerminate();
    return 0;
}