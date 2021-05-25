#version 330 core
out vec4 FragColor;

in vec3 vsOutNormal;
in vec4 vsOutPosition;

uniform vec3 camPos;

vec3 lightDir2 = vec3(1.0, 1.0, 0.0);
vec3 lightCol1 = vec3(2.0, 1.9, 1.7);
vec3 lightCol2 = vec3(0.5, 0.6, 0.7);

float shininess = 16;

float near = 0.1; 
float far  = 100.0; 

float LinearizeDepth(float depth) 
{
    float z = depth * 2.0 - 1.0; // back to NDC 
    return (2.0 * near * far) / (far + near - z * (far - near));	
}

float getAttenuation(float distance) {
return 1.0 / ((0.09 + 0.032 * distance) * distance + 1.0);
}

vec3 calcLight(vec3 normal, vec3 camDir, vec3 lightDir, vec3 lightCol) {
    // diffuse
    float diffuseStrength = abs(dot(normal, lightDir));
    // specular (blinn-phong)
    vec3 bisector = normalize(camDir + lightDir);
    float specularStrength = pow(abs(dot(bisector, normal)), shininess);
    return lightCol * (0.1 + diffuseStrength * 0.5 + specularStrength);
}

void main()
{
	vec3 camDir = normalize(camPos - vec3(vsOutPosition));

	vec3 FragColorVec3 = calcLight(vsOutNormal, camDir, camDir, lightCol1) * getAttenuation(length(camPos - vec3(vsOutPosition)))
		+ calcLight(vsOutNormal, camDir, normalize(lightDir2), lightCol2);

	FragColor = vec4(FragColorVec3, 1.0);
}