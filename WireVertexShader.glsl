#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

layout (std140) uniform Matrices
{
	uniform mat4 model;
    mat4 view;
    mat4 projection;
};

uniform vec3 camPos;

out vec3 vsOutNormal;
out vec4 vsOutPosition;

void main()
{
	vec4 pos = model * vec4(aPos, 1.0f);
	vec3 camDir = 0.001 * (camPos - vec3(pos));
	gl_Position = projection * view * (pos + vec4(camDir, 0.0));
	vsOutPosition = model * vec4(aPos, 1.0f);
	vsOutNormal = aNormal;
}