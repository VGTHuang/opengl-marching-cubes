#include <main.h>
// SSBOs
GLuint inImgSSBO, outPositionsSSBO, outNormalsSSBO, outTrianglesCountSSBO, edgeTableSSBO, triTableSSBO;

int imageX, imageY, imageZ;
// SSBOs can only read 128MB, so an input image is sliced into batches (eg. 507 * 512 * 512 -> batchSize * batchMaxThickness * 512 * 512)
int batchMaxThickness;

// has to be there so as to clear SSBO buffers properly?
int outTrianglesBuffer;

// count the total number of triangles from all batches
glm::uint outTrianglesCount = 0;
float *outPositions;
float *outNormals;

// if true, we have properly set edgetable and tritable for compute shader; no need to pass them to it again
bool hasInitializdMarchingCubes = false;

struct OffsetParams {
	int outputWidth;
	int outputOffset;
	float imgOffset;
	int imgStart;
	int imgWidth;
};

std::vector<OffsetParams> calcOffsets(const int batchMaxThickness, const float cubeRatio, const int imgWidth, const int outputWidth) {
	std::vector<OffsetParams> v;
	int outputMaxThickness = (int)((float)batchMaxThickness / cubeRatio);
	int tempOutputStart = 0;
	while (tempOutputStart < outputWidth - 1) {
		int tempOutputEnd = tempOutputStart + outputMaxThickness;
		float tempImgStartF = (tempOutputStart - 1) * cubeRatio;
		int tempImgStartI = int(tempImgStartF);
		float tempImgEndF = (tempOutputEnd + 1) * cubeRatio;
		int tempImgEndI = int(tempImgEndF) + 2;
		if (tempImgStartI < 0) {
			tempImgStartI = 0;
		}
		if (tempImgEndI > imgWidth - 1) {
			tempImgEndI = imgWidth - 1;
		}
		OffsetParams p;
		p.imgStart = tempImgStartI;
		p.imgWidth = tempImgEndI - tempImgStartI;
		p.outputOffset = tempOutputStart;
		// TODO -? +?
		p.imgOffset = tempOutputStart * cubeRatio - (float)tempImgStartI;
		p.outputWidth = outputMaxThickness;
		if (tempImgEndI - tempImgStartI > 1) {
			v.push_back(p);
		}

		tempOutputStart = tempOutputEnd;
	}
	return v;
}

class Timer
{
private:
	// Type aliases to make accessing nested type easier
	using clock_t = std::chrono::high_resolution_clock;
	using second_t = std::chrono::duration<double, std::ratio<1> >;

	std::chrono::time_point<clock_t> m_beg;

public:
	Timer() : m_beg(clock_t::now())
	{
	}

	void reset()
	{
		m_beg = clock_t::now();
	}

	double elapsed() const
	{
		return std::chrono::duration_cast<second_t>(clock_t::now() - m_beg).count();
	}
};

Timer timer;

void printTimer(const char *info) {
	double a = timer.elapsed();
	timer.reset();
	printf("%s %f\n", info, a);
}

void createMarchingCubes(const int outputShape, const float isoLevel, const glm::ivec3 inShape, float *const image3D, unsigned int &VAO, unsigned int &VBO) {

	int inMaxDim = std::max({ inShape.x, inShape.y, inShape.z }); // in 3 dimensions of input image3D, which dimension has the largest index?
	float cubeRatio = inMaxDim * 1.0f / outputShape;
	std::vector<OffsetParams> offsets = calcOffsets(batchMaxThickness, cubeRatio, inShape.x, outputShape);

	outTrianglesCount = 0;

	// TODO how large?
	int preservedPosMemorySize = sizeof(glm::vec4) * outputShape * outputShape * outputShape * 2;
	int preservedNormalMemorySize = sizeof(glm::vec4) * outputShape * outputShape * outputShape * 2;

	computeShader->use();

	// release buffer if marching cubes have already been created once
	glDeleteVertexArrays(1, &VAO);
	glDeleteBuffers(1, &VBO);
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);


	computeShader->setFloat("cubeRatio", cubeRatio);
	computeShader->setFloat("sizeCompressRatio", 10.0 / outputShape);
	computeShader->setFloat("isoLevel", isoLevel);

	if (hasInitializdMarchingCubes == false) {
		// edgeTable
		createSSBO(edgeTableSSBO, 256 * sizeof(int), 6, &edgeTable[0], computeShader, "EdgeTable");
		// triTable
		createSSBO(triTableSSBO, 256 * 16 * sizeof(int), 7, &triTable[0], computeShader, "triTable");
	}
	// outPositions
	createSSBO(outPositionsSSBO, preservedPosMemorySize, 2, outPositions, computeShader, "OutPositions");
	// outNormals
	createSSBO(outNormalsSSBO, preservedNormalMemorySize, 3, outNormals, computeShader, "OutNormals");
	// outTrianglesCount
	createSSBO(outTrianglesCountSSBO, sizeof(int), 4, &outTrianglesBuffer, computeShader, "OutTrianglesCount");


	for (int batchCount = 0; batchCount < offsets.size(); batchCount += 1) {
		timer.reset();
		// inImg
		computeShader->setIVec3("inImgShape", offsets[batchCount].imgWidth, inShape.y, inShape.z);
		createSSBO(inImgSSBO, inShape.y*inShape.z*offsets[batchCount].imgWidth * sizeof(float), 1, image3D + (inShape.y * inShape.z) * offsets[batchCount].imgStart, computeShader, "InImg");

		computeShader->setInt("outputOffset", offsets[batchCount].outputOffset);
		computeShader->setFloat("imgOffset", offsets[batchCount].imgOffset);


		// TODO how large?
		glDispatchCompute(offsets[batchCount].outputWidth, outputShape / 4, outputShape / 4);
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

	}

	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, outTrianglesCountSSBO);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, outTrianglesCountSSBO);
	outTrianglesCount = ((glm::uint *)glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, sizeof(glm::uint), GL_MAP_READ_BIT))[0];

	int totalPositionSize = sizeof(glm::vec4) * 3 * outTrianglesCount;
	int totalNormalSize = sizeof(glm::vec4) * 3 * outTrianglesCount;

	glBindBuffer(GL_SHADER_STORAGE_BUFFER, outPositionsSSBO);
	float *positions = (float *)glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, totalPositionSize, GL_MAP_READ_BIT);


	glBindBuffer(GL_SHADER_STORAGE_BUFFER, outNormalsSSBO);
	float *normals = (float *)glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, totalNormalSize, GL_MAP_READ_BIT);
	

	// total size of the buffer in bytes
	glGenVertexArrays(1, &VAO);
	glGenBuffers(1, &VBO);
	glBindVertexArray(VAO);
	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferData(GL_ARRAY_BUFFER, totalPositionSize + totalNormalSize, nullptr, GL_STATIC_DRAW);

	glBufferSubData(GL_ARRAY_BUFFER, 0, totalPositionSize, positions);
	glBufferSubData(GL_ARRAY_BUFFER, totalPositionSize, totalNormalSize, normals);


	// position attribute
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);
	// normal attribute
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(totalPositionSize));
	glEnableVertexAttribArray(1);

	hasInitializdMarchingCubes = true;
}



char * getImage3DConfig(int &x, int &y, int &z) {
	char data[1000];
	std::ifstream rfile;

	rfile.open("file_config.txt", std::ios::in | std::ios::out);

	rfile >> data;

	rfile >> x >> y >> z;

	rfile.close();

	return data;
}


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
// int main()
{
	// config
	char *path;
	path = getImage3DConfig(imageX, imageY, imageZ);
	batchMaxThickness = 20000000 / (imageY * imageZ);

	// glfw: initialize and configure
	// ------------------------------
	glfwInit();
	std::string glsl_version = "";
#ifdef __APPLE__
	// GL 3.2 + GLSL 150
	glsl_version = "#version 150";
	glfwWindowHint( // required on Mac OS
		GLFW_OPENGL_FORWARD_COMPAT,
		GL_TRUE
	);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
#elif __linux__
	// GL 3.2 + GLSL 150
	glsl_version = "#version 150";
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
#elif _WIN32
	// GL 3.0 + GLSL 130 (???)
	glsl_version = "#version 130";
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
#endif
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	// glfw window creation
	// --------------------
	GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "marching cubes", NULL, NULL);
	if (window == NULL)
	{
		std::cout << "Failed to create GLFW window" << std::endl;
		glfwTerminate();
		return -1;
	}
	glfwMakeContextCurrent(window);

	// camera
	glm::vec3 camPos(-10.0f, -5.0f, -10.0f);
	camera = new Camera(camPos, SCR_WIDTH, SCR_HEIGHT, 8.0f, 0.5f);

	// glfw events
	glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
	glfwSetMouseButtonCallback(window, mouse_button_callback);
	glfwSetCursorPosCallback(window, mouse_callback);
	glfwSetScrollCallback(window, scroll_callback);

	// glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

	// glad: load all OpenGL function pointers
	// ---------------------------------------
	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
	{
		std::cout << "Failed to initialize GLAD" << std::endl;
		return -1;
	}

	std::cout << glGetString(GL_VERSION) << std::endl;

	// draw shader
	drawShader = new Shader("VertexShader.glsl", "FragmentShader.glsl");
	drawWireframeShader = new Shader("WireVertexShader.glsl", "WireFragmentShader.glsl");

	// compute shader
	computeShader = new Shader("ComputeShader.glsl");

	// read medical data
	glm::ivec3 imgShape(imageX, imageY, imageZ);
	FILE *fpsrc = NULL;
	unsigned short *imgValsUINT;
	imgValsUINT = (unsigned short *)malloc(sizeof(unsigned short) * imageX * imageY * imageZ);
	float *imgValsFLOAT;
	imgValsFLOAT = (float *)malloc(sizeof(float) * imageX * imageY * imageZ);
	errno_t err = fopen_s(&fpsrc, path, "r");
	if (err != 0)
	{
		printf("can not open the raw image");
		return 0;
	}
	else
	{
		printf("IMAGE read OK\n");
	}
	fread(imgValsUINT, sizeof(unsigned short), imageX * imageY * imageZ, fpsrc);
	fclose(fpsrc);

	unsigned short maxImgValue = 0;

	for (int i = 0; i < imageX * imageY * imageZ; i++) {
		if (imgValsUINT[i] > maxImgValue) {
			maxImgValue = imgValsUINT[i];
		}
	}

	for (int i = 0; i < imageX * imageY * imageZ; i++) {
		imgValsFLOAT[i] = (float)imgValsUINT[i] / (float)maxImgValue;
	}

	free(imgValsUINT);


	int outputShape = 30;
	int oldOutputShape = outputShape;

	float isoLevel = 0.31;
	float oldIsoLevel = isoLevel;

	unsigned int VAO, VBO;

	createMarchingCubes(outputShape, isoLevel, imgShape, imgValsFLOAT, VAO, VBO);
	

	// uniform buffer for draw & draw wireframe
	unsigned int drawMatIndex = glGetUniformBlockIndex(drawShader->ID, "Matrices");
	unsigned int drawWireframeMatIndex = glGetUniformBlockIndex(drawWireframeShader->ID, "Matrices");
	glUniformBlockBinding(drawShader->ID, drawMatIndex, 0);
	glUniformBlockBinding(drawWireframeShader->ID, drawWireframeMatIndex, 0);

	unsigned int uboMatrices;
	glGenBuffers(1, &uboMatrices);

	glBindBuffer(GL_UNIFORM_BUFFER, uboMatrices);
	glBufferData(GL_UNIFORM_BUFFER, 3 * sizeof(glm::mat4), NULL, GL_STATIC_DRAW);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);
	glBindBufferRange(GL_UNIFORM_BUFFER, 0, uboMatrices, 0, 3 * sizeof(glm::mat4));

	glm::mat4 modelMat = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f));
	// to correct the coord problem from input image
	float angle = glm::pi<float>() / 2;
	modelMat = glm::rotate(modelMat, angle, glm::vec3(0.0, 0.0, 1.0));
	modelMat = glm::scale(modelMat, glm::vec3(1.0f, -1.0f, 1.0f));

	glBindBuffer(GL_UNIFORM_BUFFER, uboMatrices);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(glm::mat4), glm::value_ptr(modelMat));
	glBindBuffer(GL_UNIFORM_BUFFER, 0);

	glEnable(GL_DEPTH_TEST);

	// imgui
	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	//ImGui::StyleColorsClassic();

	// Setup Platform/Renderer backends
	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init(glsl_version.c_str());

	bool doRenderWireframe = false;

	// render loop
	// -----------
	while (!glfwWindowShouldClose(window))
	{
		// imgui
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		{
			ImGui::Begin("controller");
			ImGui::Text("Use AWSD to control pitch/yaw; drag to pan camera");
			ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
			ImGui::SliderFloat("iso level", &isoLevel, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
			ImGui::SliderInt("num of cubes", &outputShape, 16, 256);            // Edit 1 float using a slider from 0.0f to 1.0f
			ImGui::Checkbox("render wireframe", &doRenderWireframe);
			ImGui::End();
		}

		// Rendering
		ImGui::Render();

		if (isoLevel != oldIsoLevel || outputShape != oldOutputShape) {
			createMarchingCubes(outputShape, isoLevel, imgShape, imgValsFLOAT, VAO, VBO);
			oldIsoLevel = isoLevel;
			oldOutputShape = outputShape;
		}

		float currentFrame = glfwGetTime();
		deltaTime = currentFrame - lastFrame;
		lastFrame = currentFrame;

		processInput(window);

		glClearColor(0.8f, 0.8f, 0.8f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glBindBuffer(GL_UNIFORM_BUFFER, uboMatrices);
		glBufferSubData(GL_UNIFORM_BUFFER, sizeof(glm::mat4), sizeof(glm::mat4), glm::value_ptr(camera->GetViewMat4()));
		glBufferSubData(GL_UNIFORM_BUFFER, 2 * sizeof(glm::mat4), sizeof(glm::mat4), glm::value_ptr(camera->GetProjectionMat4()));
		glBindBuffer(GL_UNIFORM_BUFFER, 0);

		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		drawShader->use();
		drawShader->setVec3("camPos", camera->GetCameraPos());
		// render boxes
		glBindVertexArray(VAO);
		glDrawArrays(GL_TRIANGLES, 0, outTrianglesCount * 3);

		if (doRenderWireframe == true) {
			glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
			drawWireframeShader->use();
			drawWireframeShader->setVec3("camPos", camera->GetCameraPos());
			glBindVertexArray(VAO);
			glDrawArrays(GL_TRIANGLES, 0, outTrianglesCount * 3);
		}



		// render imgui
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		// glfw: swap buffers and poll IO events (keys pressed/released, mouse moved etc.)
		// -------------------------------------------------------------------------------
		glfwSwapBuffers(window);
		glfwPollEvents();
	}


	// imgui Cleanup
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	// de-allocate all resources
	glDeleteVertexArrays(1, &VAO);
	glDeleteBuffers(1, &VBO);

	// glfw: terminate, clearing all previously allocated GLFW resources.
	// ------------------------------------------------------------------
	glfwTerminate();

	//system("pause");
	return 0;
}