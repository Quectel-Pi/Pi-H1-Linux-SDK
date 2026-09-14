# Qualcomm 平台只有 GLES，没有完整的 OpenGL，禁用 GLX 后端
PACKAGECONFIG:remove = "glx"
