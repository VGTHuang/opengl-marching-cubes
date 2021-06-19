#version 430 core

// uniforms
uniform ivec3 inImgShape; // x, y, z of original scanned Img
uniform float cubeRatio;     // size of a cube / size of an img pixel
uniform float sizeCompressRatio;     // how much do I want the cube to be resized
uniform float isoLevel; // the threshold
uniform int maxImgValue;

layout(r16, binding = 1) uniform readonly image3D inImg;

layout(std430, binding = 2) writeonly buffer OutPositions {
	vec4 data[];
} outPositions;
layout(std430, binding = 3) writeonly buffer OutNormals {
	vec4 data[];
} outNormals;
layout(std430, binding = 4) writeonly buffer OutTrianglesCount {
	int data[];
} outTrianglesCount;

layout(std430, binding = 6) readonly buffer EdgeTable {
	int data[];
} edgeTable;
layout(std430, binding = 7) readonly buffer TriTable {
	int data[];
} triTable;

layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

float gridValue[8];
vec3 gridCoord[8];
// is it correct??
int edgeToGridDict[12][2] = {
	{0, 1},
	{1, 2},
	{2, 3},
	{0, 3},
	{4, 5},
	{5, 6},
	{6, 7},
	{4, 7},
	{0, 4},
	{1, 5},
	{2, 6},
	{3, 7}
};

bool isOutOfRange = false;

// get value of Img in case query is out of range
float getInputImgData(int x, int y, int z) {
	if(x >= inImgShape.x || y >= inImgShape.y || z >= inImgShape.z || x < 0 || y < 0 || z < 0) {
		isOutOfRange = true;
		return 0.0;
	}
	else {
		return imageLoad(inImg, ivec3(x, y, z)).r * 65536.0 / float(maxImgValue);
	}
}

// interpolations
// https://stackoverflow.com/questions/19271568/trilinear-interpolation
float interpolate1D(float v1, float v2, float x){
    return v1*(1-x) + v2*x;
}
float interpolate2D(float v1, float v2, float v3, float v4, float x, float y){

    float s = interpolate1D(v1, v2, x);
    float t = interpolate1D(v3, v4, x);
    return interpolate1D(s, t, y);
}
float interpolate3D(float v1, float v2, float v3, float v4, float v5, float v6, float v7, float v8, float x, float y, float z)
{
    float s = interpolate2D(v1, v2, v3, v4, x, y);
    float t = interpolate2D(v5, v6, v7, v8, x, y);
    return interpolate1D(s, t, z);
}

float getInterpImgData(vec3 query) {
	query = query * cubeRatio;
	ivec3 queryInt = ivec3(query);

	int imgIntX = queryInt.x;
	int imgIntY = queryInt.y;
	int imgIntZ = queryInt.z;

	float v1 = getInputImgData(imgIntX,   imgIntY,   imgIntZ  );
	float v2 = getInputImgData(imgIntX+1, imgIntY,   imgIntZ  );
	float v3 = getInputImgData(imgIntX,   imgIntY+1, imgIntZ  );
	float v4 = getInputImgData(imgIntX+1, imgIntY+1, imgIntZ  );
	float v5 = getInputImgData(imgIntX,   imgIntY,   imgIntZ+1);
	float v6 = getInputImgData(imgIntX+1, imgIntY,   imgIntZ+1);
	float v7 = getInputImgData(imgIntX,   imgIntY+1, imgIntZ+1);
	float v8 = getInputImgData(imgIntX+1, imgIntY+1, imgIntZ+1);
	
	float x = query.x - float(imgIntX);
	float y = query.y - float(imgIntY);
	float z = query.z - float(imgIntZ);

	return interpolate3D(v1, v2, v3, v4, v5, v6, v7, v8, x, y, z);
}

vec3 interpCubePositions(int index1, int index2) {
	float v1Weight = abs(gridValue[index2] - isoLevel) /  abs(gridValue[index2] - gridValue[index1]);
	// v1Weight = 0.5;
	return gridCoord[index1] * v1Weight + gridCoord[index2] * (1 - v1Weight);
}

vec3 getNormal(vec3 position) {
	float delta = 2.1;
	delta /= cubeRatio;
	float vx1 = getInterpImgData(vec3(position.x - delta, position.y, position.z));
	float vx2 = getInterpImgData(vec3(position.x + delta, position.y, position.z));
	float vy1 = getInterpImgData(vec3(position.x, position.y - delta, position.z));
	float vy2 = getInterpImgData(vec3(position.x, position.y + delta, position.z));
	float vz1 = getInterpImgData(vec3(position.x, position.y, position.z - delta));
	float vz2 = getInterpImgData(vec3(position.x, position.y, position.z + delta));
	return normalize(vec3(vx1 - vx2, vy1 - vy2, vz1-vz2));
}

void main1() {
	vec4 aaa = vec4(gl_GlobalInvocationID.x,   gl_GlobalInvocationID.y,   gl_GlobalInvocationID.z, 0);
	float val = getInputImgData(int(gl_GlobalInvocationID.x*2+200),   int(gl_GlobalInvocationID.y*2+200),   int(gl_GlobalInvocationID.z*2+200));
	uint index_offset = atomicAdd(outTrianglesCount.data[0], 1);

	outPositions.data[index_offset * 3] = (aaa+vec4(0,0,0,1.0)) * sizeCompressRatio;
	outPositions.data[index_offset * 3+1] = (aaa+vec4(0,val,0,1.0)) *  sizeCompressRatio;
	outPositions.data[index_offset * 3+2] = (aaa+vec4(0,0,val,1.0)) *  sizeCompressRatio;
		
	outNormals.data[index_offset * 3] = vec4(1.0);
	outNormals.data[index_offset * 3+1] = vec4(1.0);
	outNormals.data[index_offset * 3+2] = vec4(1.0);
}

void main() {
	gridCoord[0] = vec3(gl_GlobalInvocationID.x,   gl_GlobalInvocationID.y,   gl_GlobalInvocationID.z  );
	gridCoord[1] = vec3(gl_GlobalInvocationID.x+1, gl_GlobalInvocationID.y,   gl_GlobalInvocationID.z  );
	gridCoord[2] = vec3(gl_GlobalInvocationID.x+1,   gl_GlobalInvocationID.y+1, gl_GlobalInvocationID.z  );
	gridCoord[3] = vec3(gl_GlobalInvocationID.x, gl_GlobalInvocationID.y+1, gl_GlobalInvocationID.z  );
	gridCoord[4] = vec3(gl_GlobalInvocationID.x,   gl_GlobalInvocationID.y,   gl_GlobalInvocationID.z+1);
	gridCoord[5] = vec3(gl_GlobalInvocationID.x+1, gl_GlobalInvocationID.y,   gl_GlobalInvocationID.z+1);
	gridCoord[6] = vec3(gl_GlobalInvocationID.x+1,   gl_GlobalInvocationID.y+1, gl_GlobalInvocationID.z+1);
	gridCoord[7] = vec3(gl_GlobalInvocationID.x, gl_GlobalInvocationID.y+1, gl_GlobalInvocationID.z+1);

	for(int i = 0; i < 8; i++) {
		gridValue[i] = getInterpImgData(gridCoord[i]);
	}

	if(isOutOfRange == true) {
		return;
	}
	
	int cubeindex = 0;
	if (gridValue[0] < isoLevel) cubeindex |= 1;
	if (gridValue[1] < isoLevel) cubeindex |= 2;
	if (gridValue[2] < isoLevel) cubeindex |= 4;
	if (gridValue[3] < isoLevel) cubeindex |= 8;
	if (gridValue[4] < isoLevel) cubeindex |= 16;
	if (gridValue[5] < isoLevel) cubeindex |= 32;
	if (gridValue[6] < isoLevel) cubeindex |= 64;
	if (gridValue[7] < isoLevel) cubeindex |= 128;

	vec3 triVerticeCandidates[12];
	vec3 triNormalCandidates[12];

	int edgeCode = edgeTable.data[cubeindex];

	// generate interpolations
	for(int i = 0; i < 12; i++) {
		triVerticeCandidates[i] = interpCubePositions(edgeToGridDict[i][0], edgeToGridDict[i][1]);
		triNormalCandidates[i] = getNormal(triVerticeCandidates[i]);
	}

	for (int i = 0; triTable.data[cubeindex*16 + i] != -1; i += 3)
	{
		uint index_offset = atomicAdd(outTrianglesCount.data[0], 1);

		uint triVertice1 = triTable.data[cubeindex*16 + i];
		uint triVertice2 = triTable.data[cubeindex*16 + i+1];
		uint triVertice3 = triTable.data[cubeindex*16 + i+2];

		outPositions.data[index_offset * 3] = vec4(triVerticeCandidates[triVertice1], 1.0) * sizeCompressRatio;
		outPositions.data[index_offset * 3+1] = vec4(triVerticeCandidates[triVertice2], 1.0) * sizeCompressRatio;
		outPositions.data[index_offset * 3+2] = vec4(triVerticeCandidates[triVertice3], 1.0) * sizeCompressRatio;
		
		outNormals.data[index_offset * 3] = vec4(triNormalCandidates[triVertice1], 1.0);
		outNormals.data[index_offset * 3+1] = vec4(triNormalCandidates[triVertice2], 1.0);
		outNormals.data[index_offset * 3+2] = vec4(triNormalCandidates[triVertice3], 1.0);
	}
}