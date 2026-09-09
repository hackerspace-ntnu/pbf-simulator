// src/main.cpp
#include <cstdio>
#include <iostream>
#include <filesystem>
#include <algorithm>

#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "file_io.h"
#include "gl_shader.h"
#include "gl_texture.h"
#include "pingpong.h"
#include "shader_timestamp.h"

static void glfwErrorCallback(int code, const char* msg) {
    std::fprintf(stderr, "GLFW error %d: %s\n", code, msg);
}

int main() {
    glfwSetErrorCallback(glfwErrorCallback);

    if (!glfwInit()) {
        std::fprintf(stderr, "Failed to init GLFW\n");
        return 1;
    }

    // Request OpenGL 4.3 core (compute shaders require 4.3+)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* win = glfwCreateWindow(960, 540, "OpenGL 4.3 Compute Minimal", nullptr, nullptr);
    if (!win) {
        std::fprintf(stderr, "Failed to create window (GPU/driver may not support GL 4.3)\n");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        std::fprintf(stderr, "Failed to load OpenGL via glad\n");
        glfwDestroyWindow(win);
        glfwTerminate();
        return 1;
    }

    std::fprintf(stdout, "GL Vendor:   %s\n", reinterpret_cast<const char*>(glGetString(GL_VENDOR)));
    std::fprintf(stdout, "GL Renderer: %s\n", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    std::fprintf(stdout, "GL Version:  %s\n", reinterpret_cast<const char*>(glGetString(GL_VERSION)));

    int texW = 960, texH = 540;
    GLuint tex = createTextureRGBA8(texW, texH);

    GLuint progCompute = 0, progBlit = 0;
    PingPong pp;

    std::filesystem::path shadersDir = "shaders";
    std::string shaderName = "3dblock.comp";

    try {
        std::string shaderFile = fetchShaderFile(shaderName);

        if (shaderFile.empty()) {
            std::cerr << "No .comp files found in shaders directory :/\n";
            glDeleteTextures(1, &tex);
            glfwDestroyWindow(win);
            glfwTerminate();
            return 0;
        }

        progCompute = buildComputeProgramFromFile(shaderFile.c_str());
        progBlit = buildProgramFromFiles("shaders/fullscreen.vert", "shaders/blit.frag");
        pp.load(shaderFile);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Shader build error: %s\n", e.what());
        glDeleteTextures(1, &tex);
        pp.destroy();
        glfwDestroyWindow(win);
        glfwTerminate();
        return 1;
    }

    GLuint inputTex = 0;

    GLint uTimeLoc = glGetUniformLocation(progCompute, "uTime");
    const GLint uTexLoc = glGetUniformLocation(progBlit, "uTex");

    GLuint vao = 0;
    glGenVertexArrays(1, &vao);

    // Mouse rotation state
    double mouseLastX = 0.0, mouseLastY = 0.0;
    float uYaw = 0.0f, uPitch = 0.0f;
    bool mouseFirstClick = true;

    auto prevTime = static_cast<float>(glfwGetTime());

    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();

        int fbW = 0, fbH = 0;
        glfwGetFramebufferSize(win, &fbW, &fbH);
        if (fbW <= 0 || fbH <= 0) continue;

        if (fbW != texW || fbH != texH) {
            texW = fbW; texH = fbH;
            resizeTextureRGBA8(tex, texW, texH);
        }

        glViewport(0, 0, fbW, fbH);

        const auto t = static_cast<float>(glfwGetTime());
        const float dt = t - prevTime;
        prevTime = t;

        pp.update(t, dt);

        glUseProgram(progCompute);
        if (uTimeLoc >= 0) glUniform1f(uTimeLoc, t);
        GLint dtLoc = glGetUniformLocation(progCompute, "dt");
        if (dtLoc >= 0) glUniform1f(dtLoc, dt);

        // Set mouse rotation uniforms (ignored by shaders that don't use them)
        GLint yawLoc = glGetUniformLocation(progCompute, "uYaw");
        GLint pitchLoc = glGetUniformLocation(progCompute, "uPitch");

        if (yawLoc >= 0) glUniform1f(yawLoc, uYaw);
        if (pitchLoc >= 0) glUniform1f(pitchLoc, uPitch);

        pp.bindBuffers();
        
        // Bind input texture only if shader has the uniform (for shaders that use it)
        GLint inputImageLoc = glGetUniformLocation(progCompute, "inputImage");
        if (inputTex != 0 && inputImageLoc >= 0) {
            glBindImageTexture(0, inputTex, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA8);
        }
        
        // Always bind output at binding 1 for new shaders, but also check binding 0 for old ones
        GLint outImageLoc1 = glGetUniformLocation(progCompute, "outputImage");
        GLint outImageLoc0 = glGetUniformLocation(progCompute, "img");
        
        if (outImageLoc1 >= 0) {
            // New style: output at binding 1
            glBindImageTexture(1, tex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
        } else if (outImageLoc0 >= 0) {
            // Old style: output at binding 0
            glBindImageTexture(0, tex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
        }

        const auto gx = static_cast<GLuint>((texW + 15) / 16);
        const auto gy = static_cast<GLuint>((texH + 15) / 16);
        glDispatchCompute(gx, gy, 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(progBlit);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);
        if (uTexLoc >= 0) glUniform1i(uTexLoc, 0);
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // Update rotation from mouse (hold left button to rotate)
        if (glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
            double mx, my;
            glfwGetCursorPos(win, &mx, &my);

            if (!mouseFirstClick) {
                uYaw += static_cast<float>(mx - mouseLastX) * 0.005f;
                uPitch += static_cast<float>(my - mouseLastY) * 0.005f;
                uPitch = std::clamp(uPitch, -1.5f, 1.5f);
            }
            mouseLastX = mx;
            mouseLastY = my;
            mouseFirstClick = false;
        } else {
            mouseFirstClick = true;
        }

        glfwSwapBuffers(win);
    }
    glDeleteVertexArrays(1, &vao);
    glDeleteTextures(1, &tex);
    pp.destroy();
    glDeleteProgram(progCompute);
    glDeleteProgram(progBlit);

    glfwDestroyWindow(win);
    glfwTerminate();

    return 0;
}
