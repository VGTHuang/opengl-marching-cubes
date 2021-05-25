#include <main.h>
// SSBOs
GLuint inImgSSBO, outPositionsSSBO, outNormalsSSBO, outTrianglesCountSSBO, edgeTableSSBO, triTableSSBO;

// SSBOs can only read 128MB, so an input image is sliced into batches (eg. 507 * 512 * 512 -> batchSize * batchThickness * 512 * 512)
const int batchSize = 10;
const int batchThickness = 50;

// has to be there so as to clear SSBO buffers properly?
float *outPositionsBuffers[batchSize] = { nullptr, nullptr }, *outNormalsBuffers[batchSize] = { nullptr, nullptr };
int outTrianglesBuffer;

// count the total number of triangles from all batches
glm::uint outTrianglesCount = 0;

// stores these info...
std::vector<int> outTrianglesCountList = std::vector<int>();
std::vector<float *> positionList = std::vector<float *>();
std::vector<float *> normalList = std::vector<float *>();

// if true, we have properly set edgetable and tritable for compute shader; no need to pass them to it again
bool hasInitializdMarchingCubes = false;

void createMarchingCubes(const int outputShape, const float isoLevel, const glm::ivec3 inShape, float *const image3D, unsigned int &VAO, unsigned int &VBO) {
	outTrianglesCount = 0;
	for (float *tempPosition : positionList) {
		free(tempPosition);
	}
	for (float *tempNormal : normalList) {
		free(tempNormal);
	}
	positionList.clear();
	normalList.clear();
	outTrianglesCountList.clear();

	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
	computeShader->use();

	// release buffer if marching cubes have already been created once
	glDeleteVertexArrays(1, &VAO);


	computeShader->setIVec3("inImgShape", batchThickness, inShape.y, inShape.z);

	computeShader->setIVec3("outCubeShape", outputShape, outputShape, outputShape);
	int inMaxDim = std::max({ inShape.x, inShape.y, inShape.z }); // in 3 dimensions of input image3D, which dimension has the largest index
	computeShader->setFloat("cubeRatio", inMaxDim * 1.0f / outputShape);
	computeShader->setFloat("isoLevel", isoLevel);
	computeShader->setInt("batchThickness", batchThickness);

	if (hasInitializdMarchingCubes == false) {
		// edgeTable
		createSSBO(edgeTableSSBO, 256, sizeof(int), 6, &edgeTable[0], computeShader, "EdgeTable");
		// triTable
		createSSBO(triTableSSBO, 256 * 16, sizeof(int), 7, &triTable[0], computeShader, "triTable");
	}

	float *positions, *normals;

	for (int batchCount = 0; batchCount < batchSize; batchCount += 1) {
		// outPositions
		createSSBO(outPositionsSSBO, inShape.y*inShape.z*batchThickness * 4, sizeof(glm::vec4), 2, outPositionsBuffers[batchCount], computeShader, "OutPositions");
		// outNormals
		createSSBO(outNormalsSSBO, inShape.y*inShape.z*batchThickness * 4, sizeof(glm::vec4), 3, outNormalsBuffers[batchCount], computeShader, "OutNormals");
		// outTrianglesCount
		createSSBO(outTrianglesCountSSBO, 1, sizeof(float), 4, &outTrianglesBuffer, computeShader, "OutTrianglesCount");
		// inImg
		createSSBO(inImgSSBO, inShape.y*inShape.z*batchThickness, sizeof(float), 1, image3D + (inShape.y * inShape.z) * batchCount * batchThickness, computeShader, "InImg");


		computeShader->use();
		computeShader->setInt("batchCount", batchCount);
		// TODO how large?
		glDispatchCompute(outputShape / 16, outputShape / 4, outputShape / 4);
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
		// glUseProgram(0);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, outTrianglesCountSSBO);
		int tempOutTrianglesCount = ((glm::uint *)glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, sizeof(glm::uint), GL_MAP_READ_BIT))[0];
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, outPositionsSSBO);
		positions = (float *)glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, sizeof(glm::vec4) * tempOutTrianglesCount * 4, GL_MAP_READ_BIT);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, outNormalsSSBO);
		normals = (float *)glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, sizeof(glm::vec4) * tempOutTrianglesCount * 4, GL_MAP_READ_BIT);

		float *ps = (float *)malloc(sizeof(glm::vec4) * tempOutTrianglesCount * 4);
		float *ns = (float *)malloc(sizeof(glm::vec4) * tempOutTrianglesCount * 4);
		memcpy(ps, positions, sizeof(glm::vec4) * tempOutTrianglesCount * 4);
		memcpy(ns, normals, sizeof(glm::vec4) * tempOutTrianglesCount * 4);
		
		outTrianglesCountList.push_back(tempOutTrianglesCount);
		positionList.push_back(ps);
		normalList.push_back(ns);
		outTrianglesCount += tempOutTrianglesCount;
	}

	

	// **************************************************************************************************

	int totalPositionSize = sizeof(float) * 4 * 3 * outTrianglesCount;
	int totalNormalSize = sizeof(float) * 4 * 3 * outTrianglesCount;
	glGenVertexArrays(1, &VAO);
	glGenBuffers(1, &VBO);
	glBindVertexArray(VAO);
	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferData(GL_ARRAY_BUFFER, totalPositionSize + totalNormalSize, nullptr, GL_STATIC_DRAW);

	int allocatedSize = 0;

	// buffer all positions
	for (int batchCount = 0; batchCount < batchSize; batchCount += 1) {
		int tempSize = sizeof(float) * 4 * 3 * outTrianglesCountList[batchCount];
		glBufferSubData(GL_ARRAY_BUFFER, allocatedSize, tempSize, positionList[batchCount]);
		allocatedSize += tempSize;
	}
	// buffer all normals
	for (int batchCount = 0; batchCount < batchSize; batchCount += 1) {
		int tempSize = sizeof(float) * 4 * 3 * outTrianglesCountList[batchCount];
		glBufferSubData(GL_ARRAY_BUFFER, allocatedSize, tempSize, normalList[batchCount]);
		allocatedSize += tempSize;
	}

	// position attribute
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);
	// normal attribute
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(sizeof(float) * 4 * 3 * outTrianglesCount));
	glEnableVertexAttribArray(1);

	outTrianglesCount = outTrianglesCount;
	hasInitializdMarchingCubes = true;
}




int main()
{
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
	std::string rawFilePath = "H:/hhx/hhx_works/scientific_visualization_2021/visualization-data/raw_data/cbct_sample_z=507_y=512_x=512.raw";
	glm::ivec3 imgShape(507, 512, 512);
	FILE *fpsrc = NULL;
	unsigned short *imgValsUINT = new unsigned short[507 * 512 * 512];
	float *imgValsFLOAT = new float[507 * 512 * 512];
	errno_t err = fopen_s(&fpsrc, rawFilePath.c_str(), "r");
	if (err != 0)
	{
		printf("can not open the raw image");
		return 0;
	}
	else
	{
		printf("IMAGE read OK\n");
	}
	fread(imgValsUINT, sizeof(unsigned short), 507 * 512 * 512, fpsrc);
	fclose(fpsrc);

	unsigned short maxImgValue = 0;

	for (int i = 0; i < 507 * 512 * 512; i++) {
		if (imgValsUINT[i] > maxImgValue) {
			maxImgValue = imgValsUINT[i];
		}
	}

	for (int i = 0; i < 507 * 512 * 512; i++) {
		imgValsFLOAT[i] = (float)imgValsUINT[i] / (float)maxImgValue;
	}

	delete[] imgValsUINT;


	int outputShape = 220;
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
	float angle = glm::pi<float>() / 2;
	modelMat = glm::rotate(modelMat, angle, glm::vec3(0.0, 0.0, 1.0));

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