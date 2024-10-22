/*
 * Copyright MediaZ Teknoloji A.S. All Rights Reserved.
 */


#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <random>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

 // Nodos
#include "CommonEvents_generated.h"
#include <nosFlatBuffersCommon.h>
#include <Nodos/AppAPI.h>
#include "nosVulkanSubsystem/nosVulkanSubsystem.h"

#if defined(_WIN32)
#define NOMINMAX 1
#define WIN32_LEAN_AND_MEAN 1
#include "Windows.h"
#	define glImportSemaphore glImportSemaphoreWin32HandleEXT
#	define glImportMemory glImportMemoryWin32HandleEXT
#	define GL_HANDLE_TYPE GL_HANDLE_TYPE_OPAQUE_WIN32_EXT
#	define VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT
#	define VK_EXTERNAL_MEMORY_HANDLE_TYPE VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT
#else
#include <dlfcn.h>
#include <sys/eventfd.h>
#	define glImportSemaphore glImportSemaphoreFdEXT
#	define glImportMemory glImportMemoryFdEXT
#	define GL_HANDLE_TYPE GL_HANDLE_TYPE_OPAQUE_FD_EXT
#endif

GLFWwindow* window;
const uint32_t WIDTH = 1920;
const uint32_t HEIGHT = 1080;
nos::app::IAppServiceClient* client;
struct SampleEventDelegates* eventDelegates;

void CreateTexturePinsInNodos(const nos::fb::Node& appNode);

struct ImportedOSHandle
{
	std::optional<NOS_HANDLE> OSHandle = std::nullopt;
	ImportedOSHandle(ImportedOSHandle const& other) = delete;
	ImportedOSHandle& operator=(ImportedOSHandle const& other) = delete;
	ImportedOSHandle(ImportedOSHandle&& other) noexcept : OSHandle(std::move(other.OSHandle)) {
		other.OSHandle = std::nullopt;
	}
	ImportedOSHandle& operator=(ImportedOSHandle&& other) noexcept
	{
		CloseHandle();
		OSHandle = std::move(other.OSHandle);
		other.OSHandle = std::nullopt;
		return *this;
	}
	ImportedOSHandle() : OSHandle(std::nullopt) {}
	ImportedOSHandle(NOS_HANDLE fromHandle) : OSHandle(client->DuplicateHandle(fromHandle)) {}
	~ImportedOSHandle()
	{
		CloseHandle();
	}
	void CloseHandle()
	{
		if (OSHandle)
			client->CloseHandle(*OSHandle);
	}

	operator NOS_HANDLE() const
	{
		return OSHandle.value_or(0);
	}
};

struct GLImportedTexture
{
	ImportedOSHandle OSHandle {};
	GLuint Image{};
	GLuint Memory{};
	GLImportedTexture() {}
	GLImportedTexture(ImportedOSHandle osHandle, GLuint image, GLuint memory) : OSHandle(std::move(osHandle)), Image(image), Memory(memory) {}
	GLImportedTexture(GLImportedTexture&& other) 
	{
		Image = other.Image;
		Memory = other.Memory;
		OSHandle = std::move(other.OSHandle);
		other.Image = 0;
		other.Memory = 0;
	}
	GLImportedTexture& operator=(GLImportedTexture&& other) {
		if (Image)
			glDeleteTextures(1, &Image);
		if (Memory)
			glDeleteMemoryObjectsEXT(1, &Memory);
		Image = other.Image;
		Memory = other.Memory;
		OSHandle = std::move(other.OSHandle);
		other.Image = 0;
		other.Memory = 0;
		return *this;
	}
	~GLImportedTexture()
	{
		if (Image)
			glDeleteTextures(1, &Image);
		if (Memory)
			glDeleteMemoryObjectsEXT(1, &Memory);
	}
};

struct GLImportedSemaphore
{
	ImportedOSHandle OSHandle{};
	GLuint Semaphore{};

	GLImportedSemaphore() {}
	GLImportedSemaphore(ImportedOSHandle osHandle, GLuint semaphore) : OSHandle(std::move(osHandle)), Semaphore(Semaphore) {}
	GLImportedSemaphore(GLImportedSemaphore&& other) 
	{
		Semaphore = other.Semaphore;
		OSHandle = std::move(other.OSHandle);
		other.Semaphore = 0;
	}
	GLImportedSemaphore& operator=(GLImportedSemaphore&& other) 
	{
		if (Semaphore)
			glDeleteSemaphoresEXT(1, &Semaphore);
		Semaphore = other.Semaphore;
		OSHandle = std::move(other.OSHandle);
		other.Semaphore = 0;
		return *this;
	}
	~GLImportedSemaphore()
	{
		if (Semaphore)
			glDeleteSemaphoresEXT(1, &Semaphore);
	}
};

GLenum VulkanToOpenGLFormat(nos::sys::vulkan::Format format)
{
	using vkFormat = nos::sys::vulkan::Format;
	switch (format)
	{
	case vkFormat::R8_UNORM:
		return GL_R8;
	case vkFormat::R8_UINT:
		return GL_R8UI;
	case vkFormat::R8_SRGB:
		return GL_NONE;
	case vkFormat::R8G8_UNORM:
		return GL_RG8;
	case vkFormat::R8G8_UINT:
		return GL_RG8UI;
	case vkFormat::R8G8_SRGB:
		return GL_NONE;
	case vkFormat::R8G8B8_UNORM:
		return GL_RGB8;
	case vkFormat::R8G8B8_SRGB:
		return GL_SRGB8;
	case vkFormat::B8G8R8_UNORM:
		return GL_RGB8;
	case vkFormat::B8G8R8_UINT:
		return GL_RGB8UI;
	case vkFormat::B8G8R8_SRGB:
		return GL_SRGB8;
	case vkFormat::R8G8B8A8_UNORM:
		return GL_RGBA8;
	case vkFormat::R8G8B8A8_UINT:
		return GL_RGBA8UI;
	case vkFormat::R8G8B8A8_SRGB:
		return GL_SRGB8_ALPHA8;
	case vkFormat::B8G8R8A8_UNORM:
		return GL_RGBA8;
	case vkFormat::B8G8R8A8_SRGB:
		return GL_SRGB8_ALPHA8;
	case vkFormat::A2R10G10B10_UNORM_PACK32:
		return GL_RGB10_A2;
	case vkFormat::A2R10G10B10_SNORM_PACK32:
		return GL_RGB10_A2;
	case vkFormat::A2R10G10B10_USCALED_PACK32:
		return GL_RGB10_A2UI;
	case vkFormat::A2R10G10B10_SSCALED_PACK32:
		return GL_RGB10_A2UI;
	case vkFormat::A2R10G10B10_UINT_PACK32:
		return GL_RGB10_A2UI;
	case vkFormat::A2R10G10B10_SINT_PACK32:
		return GL_RGB10_A2UI;
	case vkFormat::R16_UNORM:
		return GL_R16;
	case vkFormat::R16_SNORM:
		return GL_R16_SNORM;
	case vkFormat::R16_USCALED:
		return GL_R16UI;
	case vkFormat::R16_SSCALED:
		return GL_R16I;
	case vkFormat::R16_UINT:
		return GL_R16UI;
	case vkFormat::R16_SINT:
		return GL_R16I;
	case vkFormat::R16_SFLOAT:
		return GL_R16F;
	case vkFormat::R16G16_UNORM:
		return GL_RG16;
	case vkFormat::R16G16_SNORM:
		return GL_RG16_SNORM;
	case vkFormat::R16G16_USCALED:
		return GL_RG16UI;
	case vkFormat::R16G16_SSCALED:
		return GL_RG16I;
	case vkFormat::R16G16_UINT:
		return GL_RG16UI;
	case vkFormat::R16G16_SINT:
		return GL_RG16I;
	case vkFormat::R16G16_SFLOAT:
		return GL_RG16F;
	case vkFormat::R16G16B16_UNORM:
		return GL_RGB16;
	case vkFormat::R16G16B16_SNORM:
		return GL_RGB16_SNORM;
	case vkFormat::R16G16B16_USCALED:
		return GL_RGB16UI;
	case vkFormat::R16G16B16_SSCALED:
		return GL_RGB16I;
	case vkFormat::R16G16B16_UINT:
		return GL_RGB16UI;
	case vkFormat::R16G16B16_SINT:
		return GL_RGB16I;
	case vkFormat::R16G16B16_SFLOAT:
		return GL_RGB16F;
	case vkFormat::R16G16B16A16_UNORM:
		return GL_RGBA16;
	case vkFormat::R16G16B16A16_SNORM:
		return GL_RGBA16_SNORM;
	case vkFormat::R16G16B16A16_USCALED:
		return GL_RGBA16UI;
	case vkFormat::R16G16B16A16_SSCALED:
		return GL_RGBA16I;
	case vkFormat::R16G16B16A16_UINT:
		return GL_RGBA16UI;
	case vkFormat::R16G16B16A16_SINT:
		return GL_RGBA16I;
	case vkFormat::R16G16B16A16_SFLOAT:
		return GL_RGBA16F;
	case vkFormat::R32_UINT:
		return GL_R32UI;
	case vkFormat::R32_SINT:
		return GL_R32I;
	case vkFormat::R32_SFLOAT:
		return GL_R32F;
	case vkFormat::R32G32_UINT:
		return GL_RG32UI;
	case vkFormat::R32G32_SINT:
		return GL_RG32I;
	case vkFormat::R32G32_SFLOAT:
		return GL_RG32F;
	case vkFormat::R32G32B32_UINT:
		return GL_RGB32UI;
	case vkFormat::R32G32B32_SINT:
		return GL_RGB32I;
	case vkFormat::R32G32B32_SFLOAT:
		return GL_RGB32F;
	case vkFormat::R32G32B32A32_UINT:
		return GL_RGBA32UI;
	case vkFormat::R32G32B32A32_SINT:
		return GL_RGBA32I;
	case vkFormat::R32G32B32A32_SFLOAT:
		return GL_RGBA32F;
	case vkFormat::B10G11R11_UFLOAT_PACK32:
		return GL_R11F_G11F_B10F;
	case vkFormat::D16_UNORM:
		return GL_DEPTH_COMPONENT16;
	case vkFormat::X8_D24_UNORM_PACK32:
		return GL_DEPTH_COMPONENT24;
	case vkFormat::D32_SFLOAT:
		return GL_DEPTH_COMPONENT32F;
	case vkFormat::G8B8G8R8_422_UNORM:
		return GL_RGB;
	case vkFormat::B8G8R8G8_422_UNORM:
		return GL_RGB;
	default:
		return GL_NONE;
	}
}

std::optional<GLImportedTexture> ImportTexture(nos::sys::vulkan::TTexture const& tex)
{
	GLImportedTexture imported{};
	glCreateMemoryObjectsEXT(1, &imported.Memory);
	if (!glIsMemoryObjectEXT(imported.Memory))
	{
		std::cerr << "Failed to create memory object" << std::endl;
		return std::nullopt;
	}
	if (glGetError() != GL_NO_ERROR)
	{
		std::cerr << "Failed to create memory object" << std::endl;
		return std::nullopt;
	}
	auto handle = ImportedOSHandle(tex.external_memory.handle());
	if(!handle.OSHandle)
	{
		std::cerr << "Failed to duplicate handle" << std::endl;
		return std::nullopt;
	}
	glImportMemory(imported.Memory, tex.external_memory.allocation_size(), GL_HANDLE_TYPE, handle.OSHandle.value_or(0));
	glCreateTextures(GL_TEXTURE_2D, 1, &imported.Image);
	auto format = VulkanToOpenGLFormat(tex.format);
	assert(format != GL_NONE);
	glTextureStorageMem2DEXT(imported.Image, 1, format, tex.width, tex.height, imported.Memory, tex.offset);
	if (glGetError() != GL_NO_ERROR)
	{
		std::cerr << "Failed to create texture" << std::endl;
		return {};
	}
	return imported;
}

std::optional<GLImportedSemaphore> ImportSemaphore(uint64_t pid, uint64_t handle)
{
	GLImportedSemaphore imported{};
	glGenSemaphoresEXT(1, &imported.Semaphore);
	auto semaphoreHandle = client->DuplicateHandle(handle).value_or(0);
	glImportSemaphore(imported.Semaphore, GL_HANDLE_TYPE, semaphoreHandle);
	if (glGetError() != GL_NO_ERROR || !glIsSemaphoreEXT(imported.Semaphore))
	{
		std::cerr << "Failed to import semaphore" << std::endl;
		return std::nullopt;
	}
	return imported;
}

void UpdateSyncState(nos::app::ExecutionState newState);
void ResetState();
void DeleteSyncSemaphores();

struct ExternalTexture
{
	GLImportedTexture Image;
	nos::fb::UUID Id;
	nos::sys::vulkan::TTexture Texture;
};

struct NodosState
{
	std::optional<GLImportedSemaphore> InputSemaphore = {std::nullopt}, OutputSemaphore = {std::nullopt};
	std::optional<ImportedOSHandle> RenderSubmittedEvent = std::nullopt;
	ExternalTexture ShaderInput{}, ShaderOutput{};
	nos::app::ExecutionState ExecutionState = nos::app::ExecutionState::IDLE;
	nos::app::ExecutionState ExecutionStateMainThread = nos::app::ExecutionState::IDLE;
	std::optional<uint64_t> NodosFrameNumber = std::nullopt;
	// Protects execution state and frame number
	std::mutex ExecutionStateMutex;
	std::condition_variable ExecutionStateCV;

	std::uint64_t CurFrameNumber = 0;
} g_NodosState;

struct GLData
{
	GLuint ShaderProgram;
	GLuint VAO;
	GLuint VBO;
	GLuint FBO;
} glData;

struct TaskQueue
{
	void Push(std::function<void()> task)
	{
		std::lock_guard<std::mutex> lock(Mutex);
		Tasks.push(task);
	}
	void Process()
	{
		std::queue<std::move_only_function<void()>> tasks;
		{
			std::lock_guard<std::mutex> lock(Mutex);
			tasks = std::move(Tasks);
		}
		while (!tasks.empty())
		{
			auto& task = tasks.front();
			task();
			tasks.pop();
		}
	}
private:
	std::queue<std::move_only_function<void()>> Tasks;
	std::mutex Mutex;
} TaskQueue{};

struct SampleEventDelegates : nos::app::IEventDelegates
{
	SampleEventDelegates(nos::app::IAppServiceClient* client) : Client(client) {}

	nos::app::IAppServiceClient* Client;
	nos::fb::UUID NodeId{};

	void HandleEvent(const nos::app::EngineEvent* event)
	{
		using namespace nos::app;
		switch (event->event_type())
		{
		case EngineEventUnion::AppConnectedEvent: {
			OnAppConnected(event->event_as<AppConnectedEvent>()->node());
			break;
		}
		case EngineEventUnion::FullNodeUpdate: {
			OnNodeUpdated(*event->event_as<nos::FullNodeUpdate>()->node());
			break;
		}
		case EngineEventUnion::NodeRemovedEvent: {
			OnNodeRemoved();
			break;
		}
		case EngineEventUnion::AppPinValueChanged: {
			auto const& pinValueChanged = event->event_as<AppPinValueChanged>();
			OnPinValueChanged(*pinValueChanged->pin_id(),
				pinValueChanged->value()->Data(),
				pinValueChanged->value()->size(),
				pinValueChanged->reset(),
				pinValueChanged->frame_number());
			break;
		}
		case EngineEventUnion::NodeImported: {
			OnNodeImported(*event->event_as<nos::app::NodeImported>()->node());
			break;
		}

		case EngineEventUnion::StateChanged: {
			OnStateChanged(event->event_as<nos::app::StateChanged>()->state());
			break;
		}
		case EngineEventUnion::AppExecuteStart: {
			OnExecuteStart(event->event_as<nos::app::AppExecuteStart>());
			break;
		}
		case EngineEventUnion::SyncSemaphoresFromNodos: {
			OnSyncSemaphoresFromNodos(event->event_as<nos::app::SyncSemaphoresFromNodos>());
			break;
		}
		default:
			break;
		}
	}

	void OnAppConnected(const nos::fb::Node* appNode)
	{
		std::cout << "Connected to Nodos" << std::endl;
		if (appNode)
		{
			NodeId = *appNode->id();
			CreateTexturePinsInNodos(*appNode);
		}
	}
	void OnNodeUpdated(nos::fb::Node const& appNode)
	{
		std::cout << "Node updated from Nodos" << std::endl;
		NodeId = *appNode.id();

		CreateTexturePinsInNodos(appNode);
	}

	void OnNodeImported(nos::fb::Node const& appNode)
	{
		std::cout << "Node updated from Nodos" << std::endl;
		NodeId = *appNode.id();

		CreateTexturePinsInNodos(appNode);
	}

	void OnNodeRemoved()
	{
		std::cout << "Node removed from Nodos" << std::endl;
		TaskQueue.Push([]()
			{
				ResetState();
			});
	}
	void OnPinValueChanged(nos::fb::UUID const& pinId, uint8_t const* data, size_t size, bool reset, uint64_t frameNumber)
	{
		auto texRoot = flatbuffers::GetRoot<nos::sys::vulkan::Texture>(data);
		if (!texRoot)
		{
			std::cerr << "Failed to unpack texture" << std::endl;
			return;
		}
		nos::sys::vulkan::TTexture tex{};
		texRoot->UnPackTo(&tex);

		TaskQueue.Push([pinId, tex = std::move(tex)]()
			{
				std::cout << "Pin value changed" << std::endl;
				if (pinId == g_NodosState.ShaderInput.Id || pinId == g_NodosState.ShaderOutput.Id)
				{
					auto imported = ImportTexture(tex);
					if(!imported)
					{
						std::cerr << "Failed to import texture" << std::endl;
						return;
					}
					if (pinId == g_NodosState.ShaderInput.Id)
					{
						g_NodosState.ShaderInput.Texture = tex;
						g_NodosState.ShaderInput.Image = std::move(*imported);
					}
					else if (pinId == g_NodosState.ShaderOutput.Id)
					{
						g_NodosState.ShaderOutput.Texture = tex;
						g_NodosState.ShaderOutput.Image = std::move(*imported);
						glNamedFramebufferTexture(glData.FBO, GL_COLOR_ATTACHMENT0, g_NodosState.ShaderOutput.Image.Image, 0);
					}
				}
			});
	}
	void OnConnectionClosed() override
	{
		UpdateSyncState(nos::app::ExecutionState::IDLE);
		std::cout << "Connection to Nodos closed" << std::endl;
		TaskQueue.Push([]()
			{
				ResetState();
			});
	}
	void OnStateChanged(nos::app::ExecutionState newState)
	{
		UpdateSyncState(newState);
		TaskQueue.Push([newState]()
			{
				g_NodosState.ExecutionStateMainThread = newState;
				if (newState == nos::app::ExecutionState::SYNCED)
				{
					flatbuffers::FlatBufferBuilder mb;
					auto offset = nos::CreateAppEventOffset(mb, nos::app::CreateRequestSyncSemaphores(mb, false));
					mb.Finish(offset);
					auto buf = mb.Release();
					auto root = flatbuffers::GetRoot<nos::app::AppEvent>(buf.data());
					client->Send(*root);
				}
				else
				{
					DeleteSyncSemaphores();
				}
			});
	}
	void OnExecuteStart(nos::app::AppExecuteStart const* appExecuteStart)
	{
		{
			std::unique_lock<std::mutex> lock(g_NodosState.ExecutionStateMutex);
			//std::cout << "Execution started:" << appExecuteStart->frame_counter() << std::endl;
			if (appExecuteStart->reset())
				g_NodosState.NodosFrameNumber = std::nullopt;
			else
				g_NodosState.NodosFrameNumber = appExecuteStart->frame_counter();
		}
		g_NodosState.ExecutionStateCV.notify_all();
	}
	void OnSyncSemaphoresFromNodos(nos::app::SyncSemaphoresFromNodos const* syncSemaphoresFromNodos)
	{
		TaskQueue.Push([pid = syncSemaphoresFromNodos->pid(), inputSemaphoreHandle = syncSemaphoresFromNodos->input_semaphore(),
			outputSemaphoreHandle = syncSemaphoresFromNodos->output_semaphore(),
			renderSubmittedEvent = syncSemaphoresFromNodos->process_render_submitted_event()]()
			{
				DeleteSyncSemaphores();
				g_NodosState.InputSemaphore = ImportSemaphore(pid, inputSemaphoreHandle);
				g_NodosState.OutputSemaphore = ImportSemaphore(pid, outputSemaphoreHandle);
				g_NodosState.RenderSubmittedEvent = {renderSubmittedEvent};
			});
	}
};


nos::fb::UUID GenerateRandomUUID() {
	nos::fb::UUID uuid;
	std::vector<uint8_t> randomBytes(16);
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<uint16_t> dis(0, std::numeric_limits<uint8_t>::max());

	for (size_t i = 0; i < 16; ++i) {
		randomBytes[i] = static_cast<uint8_t>(dis(gen));
	}
	memcpy(&uuid, randomBytes.data(), 16);
	return uuid;
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
	glViewport(0, 0, width, height);
}

void APIENTRY debug_message_callback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* user_param)
{
	switch (severity)
	{
	case GL_DEBUG_SEVERITY_HIGH:
		std::cerr << "OpenGL: " << message << std::endl;
		break;
	case GL_DEBUG_SEVERITY_MEDIUM:
		std::cerr << "OpenGL: " << message << std::endl;
		break;
	case GL_DEBUG_SEVERITY_LOW:
		std::cerr << "OpenGL: " << message << std::endl;
		break;
	default:
	case GL_DEBUG_SEVERITY_NOTIFICATION:
		std::cout << "OpenGL: " << message << std::endl;
		break;
	}
}

bool InitWindow()
{
	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);


	window = glfwCreateWindow(WIDTH, HEIGHT, "OpenGLAppSample", nullptr, nullptr);
	if (!window)
	{
		std::cout << "Failed to create GLFW window" << std::endl;
		glfwTerminate();
		return false;
	}
	glfwMakeContextCurrent(window);
	glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
	gladLoadGL();
	if (glGenSemaphoresEXT == nullptr)
	{
		std::cout << "OpenGL extension GL_EXT_semaphore not supported" << std::endl;
		return false;
	}
#if defined(_WIN32)
	if (glImportMemoryWin32HandleEXT == nullptr)
	{
		std::cout << "OpenGL extension GL_EXT_memory_object_win32 not supported" << std::endl;
		return false;
	}
	if (glImportSemaphoreWin32HandleEXT == nullptr)
	{
		std::cout << "OpenGL extension GL_EXT_semaphore_win32 not supported" << std::endl;
		return false;
	}
#elif defined(__linux__)
	if (glImportMemoryFdEXT == nullptr)
	{
		std::cout << "OpenGL extension GL_EXT_memory_object_fd not supported" << std::endl;
		return false;
	}
	if (glImportSemaphoreFdEXT == nullptr)
	{
		std::cout << "OpenGL extension GL_EXT_semaphore_fd not supported" << std::endl;
		return false;
	}
#else
#error Unsupported platform
#endif
	
	glfwSwapInterval(1);
	return true;
}

GLuint CreateShaderProgram(const char* vertexShaderSource, const char* fragmentShaderSource)
{
	auto compileShader = [](const char* shaderSource, GLenum shaderType) -> GLuint
		{
			GLuint shader = glCreateShader(shaderType);
			glShaderSource(shader, 1, &shaderSource, nullptr);
			glCompileShader(shader);

			GLint success;
			glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
			if (!success)
			{
				GLint max_length = 0;
				glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &max_length);

				// The maxLength includes the NULL character
				std::vector<GLchar> error_log(max_length);
				glGetShaderInfoLog(shader, max_length, &max_length, &error_log[0]);
				std::string str_error;
				str_error.insert(str_error.end(), error_log.begin(), error_log.end());

				// Provide the infolog in whatever manor you deem best.
				// Exit with failure.
				glDeleteShader(shader);        // Don't leak the shader.
				std::cerr << "OpenGL: Shader compilation failed" << str_error << std::endl;
				return 0;
			}
			return shader;
};
	GLuint shaderProgram = glCreateProgram();
	auto vertexShader = compileShader(vertexShaderSource, GL_VERTEX_SHADER);
	auto fragmentShader = compileShader(fragmentShaderSource, GL_FRAGMENT_SHADER);
	glAttachShader(shaderProgram, vertexShader);
	glAttachShader(shaderProgram, fragmentShader);
	glLinkProgram(shaderProgram);
	glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);
	return shaderProgram;
}


void InitOpenGL()
{
	glEnable(GL_DEBUG_OUTPUT);
	glDebugMessageCallback(debug_message_callback, nullptr);
	glViewport(0, 0, WIDTH, HEIGHT);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

	glData = {};
	// Set up vertex data (and buffer(s)) and attribute pointers, upload triangle
	glCreateVertexArrays(1, &glData.VAO);
	// position & texture coordinates
	float vertices[] = {
		//bottom left
		-1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
		//bottom right
		  1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
		  //top
		  0.0f, 1.0f, 0.0f, 0.5f, 1.0f
	};
	glCreateBuffers(1, &glData.VBO);
	glNamedBufferStorage(glData.VBO, sizeof(vertices), vertices, 0);

	GLuint vaoBindingPoint = 0;
	glVertexArrayVertexBuffer(glData.VBO, 0, glData.VBO,
		0,                  // offset of the first element in the buffer hctVBO.
		5 * sizeof(float));   // stride == 3 position floats + 2 texture coordinate floats

	GLuint attribPos = 0;
	GLuint attribTexCoord = 1;
	glEnableVertexArrayAttrib(glData.VAO, attribPos);
	glEnableVertexArrayAttrib(glData.VAO, attribTexCoord);

	glVertexArrayAttribFormat(glData.VAO, attribPos, 3, GL_FLOAT, GL_FALSE, 0);
	glVertexArrayAttribFormat(glData.VAO, attribTexCoord, 2, GL_FLOAT, GL_FALSE, 3 * sizeof(float));

	glVertexArrayAttribBinding(glData.VAO, attribPos, vaoBindingPoint);
	glVertexArrayAttribBinding(glData.VAO, attribTexCoord, vaoBindingPoint);

	glData.ShaderProgram = CreateShaderProgram(
		R"(
		#version 450

		layout(location = 0) in vec3 aPos;
		layout(location = 1) in vec2 aTexCoord;

		out vec2 texCoord;
		void main ()
		{
		  gl_Position = vec4(aPos, 1.0);
		  texCoord = vec2(aTexCoord);
		}
	)",
		R"(
		#version 450 core
		in vec2 texCoord;
		uniform sampler2D inTexture;
		out vec4 FragColor;
		void main()
		{
			FragColor = texture(inTexture, vec2(texCoord.x, 1-texCoord.y)).rgba;
		}
	)");

	glCreateFramebuffers(1, &glData.FBO);

}

int InitNosSDK()
{
	// Initialize Nodos SDK
	nos::app::FN_CheckSDKCompatibility* pfnCheckSDKCompatibility = nullptr;
	nos::app::FN_MakeAppServiceClient* pfnMakeAppServiceClient = nullptr;
	nos::app::FN_ShutdownClient* pfnShutdownClient = nullptr;


#if defined(_WIN32)
	HMODULE sdkModule = LoadLibrary(NODOS_APP_SDK_DLL);
	if (sdkModule) {
		pfnCheckSDKCompatibility = (nos::app::FN_CheckSDKCompatibility*)GetProcAddress(sdkModule, "CheckSDKCompatibility");
		pfnMakeAppServiceClient = (nos::app::FN_MakeAppServiceClient*)GetProcAddress(sdkModule, "MakeAppServiceClient");
		pfnShutdownClient = (nos::app::FN_ShutdownClient*)GetProcAddress(sdkModule, "ShutdownClient");
	}
#elif defined(__linux__)
	std::cout << "Nodos app SDK path: " << NODOS_APP_SDK_DLL << std::endl;
	void* sdkModule = dlopen(NODOS_APP_SDK_DLL, RTLD_LAZY);
	if (sdkModule) {
		pfnCheckSDKCompatibility = (nos::app::FN_CheckSDKCompatibility*)dlsym(sdkModule, "CheckSDKCompatibility");
		pfnMakeAppServiceClient = (nos::app::FN_MakeAppServiceClient*)dlsym(sdkModule, "MakeAppServiceClient");
		pfnShutdownClient = (nos::app::FN_ShutdownClient*)dlsym(sdkModule, "ShutdownClient");
	}
#endif
	if (!sdkModule) {
		std::cerr << "Failed to load Nodos SDK" << std::endl;
		return -1;
	}

	if (!pfnCheckSDKCompatibility || !pfnMakeAppServiceClient || !pfnShutdownClient) {
		std::cerr << "Failed to load Nodos SDK functions" << std::endl;
		return -1;
	}

	if (!pfnCheckSDKCompatibility(NOS_APPLICATION_SDK_VERSION_MAJOR, NOS_APPLICATION_SDK_VERSION_MINOR, NOS_APPLICATION_SDK_VERSION_PATCH)) {
		std::cerr << "Incompatible Nodos SDK version" << std::endl;
		return -1;
	}

	client = pfnMakeAppServiceClient("localhost:50053", nos::app::ApplicationInfo{
		.AppKey = "Sample-OpenGL-App",
		.AppName = "Sample OpenGL App"
		});

	if (!client) {
		std::cerr << "Failed to create App Service Client" << std::endl;
		return -1;
	}
	// TODO: Shutdown client
	eventDelegates = new SampleEventDelegates(client);
	client->RegisterEventDelegates(eventDelegates);

	while (!client->TryConnect())
	{
		std::cout << "Connecting to Nodos..." << std::endl;
		std::this_thread::sleep_for(std::chrono::seconds(1));
	}
	return 0;
}

void CreateTexturePinsInNodos(const nos::fb::Node& appNode)
{
	std::cout << "Creating pins" << std::endl;
	bool createPins = !appNode.pins() || appNode.pins()->size() == 0;
	std::vector<flatbuffers::Offset<nos::fb::Pin>> pins;
	flatbuffers::FlatBufferBuilder fbb;
	if (createPins)
	{
		g_NodosState.ShaderInput.Id = GenerateRandomUUID();
		pins.push_back(nos::fb::CreatePinDirect(fbb, &g_NodosState.ShaderInput.Id, "Shader Input", nos::sys::vulkan::Texture::GetFullyQualifiedName(), nos::fb::ShowAs::INPUT_PIN, nos::fb::CanShowAs::INPUT_PIN_ONLY, "Shader Vars", 0, 0, 0, 0, 0, 0, 0, false, false, false, 0, 0, nos::fb::PinContents::JobPin, 0, 0, nos::fb::PinValueDisconnectBehavior::KEEP_LAST_VALUE, "Example tooltip", "Texture Input"));
		g_NodosState.ShaderOutput.Id = GenerateRandomUUID();
		pins.push_back(nos::fb::CreatePinDirect(fbb, &g_NodosState.ShaderOutput.Id, "Shader Output", nos::sys::vulkan::Texture::GetFullyQualifiedName(), nos::fb::ShowAs::OUTPUT_PIN, nos::fb::CanShowAs::OUTPUT_PIN_ONLY, "Shader Vars", 0, 0, 0, 0, 0, 0, 0, false, false, false, 0, 0, nos::fb::PinContents::JobPin, 0, 0, nos::fb::PinValueDisconnectBehavior::KEEP_LAST_VALUE, "Example tooltip", "Texture Output"));
	}
	else
	{
		for (auto pin : *appNode.pins())
		{
			if (pin->show_as() == nos::fb::ShowAs::INPUT_PIN)
				g_NodosState.ShaderInput.Id = *pin->id();
			else if (pin->show_as() == nos::fb::ShowAs::OUTPUT_PIN)
				g_NodosState.ShaderOutput.Id = *pin->id();
		}
	}

	auto offset = nos::CreatePartialNodeUpdateDirect(fbb, &eventDelegates->NodeId, nos::ClearFlags::NONE, 0, &pins, 0, 0, 0, 0);
	fbb.Finish(offset);
	auto buf = fbb.Release();
	auto root = flatbuffers::GetRoot<nos::PartialNodeUpdate>(buf.data());
	client->SendPartialNodeUpdate(*root);
}

void UpdateSyncState(nos::app::ExecutionState newState)
{
	TaskQueue.Push([newState]()
	{
		glfwSwapInterval(newState == nos::app::ExecutionState::IDLE ? 1 : 0);
	});
	std::unique_lock<std::mutex> lock(g_NodosState.ExecutionStateMutex);
	g_NodosState.ExecutionState = newState;
	g_NodosState.ExecutionStateCV.notify_all();
}

void DeleteSyncSemaphores()
{
	g_NodosState.InputSemaphore = std::nullopt;
	g_NodosState.OutputSemaphore = std::nullopt;
	if (g_NodosState.RenderSubmittedEvent)
	{
		if(g_NodosState.RenderSubmittedEvent)
		{
#if defined(_WIN32)
			SetEvent(g_NodosState.RenderSubmittedEvent);
#elif defined(__linux__)
			uint64_t dummy = 1;
			write(*g_NodosState.RenderSubmittedEvent->OSHandle, &dummy, sizeof(dummy));
#else 
#error Unsupported platform
#endif
		}
		g_NodosState.RenderSubmittedEvent = std::nullopt;
	}
	g_NodosState.CurFrameNumber = 0;
}

void ResetState()
{
	if (g_NodosState.ShaderInput.Image.Image)
	{
		glDeleteTextures(1, &g_NodosState.ShaderInput.Image.Image);
		glDeleteMemoryObjectsEXT(1, &g_NodosState.ShaderInput.Image.Memory);
	}
	if (g_NodosState.ShaderOutput.Image.Image)
	{
		glDeleteTextures(1, &g_NodosState.ShaderOutput.Image.Image);
		glDeleteMemoryObjectsEXT(1, &g_NodosState.ShaderOutput.Image.Memory);
	}
	DeleteSyncSemaphores();
	g_NodosState.ShaderInput = {};
	g_NodosState.ShaderOutput = {};
	g_NodosState.CurFrameNumber = 0;
	std::unique_lock lock(g_NodosState.ExecutionStateMutex);
	g_NodosState.NodosFrameNumber = std::nullopt;
	g_NodosState.ExecutionState = nos::app::ExecutionState::IDLE;
	g_NodosState.ExecutionStateMainThread = nos::app::ExecutionState::IDLE;
}

int main()
{
	InitWindow();
	InitOpenGL();
	if(InitNosSDK())
	{
		std::cerr << "Failed to initialize Nodos SDK" << std::endl;
		return -1;
	}

	int frame = 0;
	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();
		TaskQueue.Process();
		if (!client->IsConnected())
		{
			std::cout << "Reconnecting to Nodos..." << std::endl;
			while (!client->TryConnect() || !client->IsConnected())
			{
				std::this_thread::sleep_for(std::chrono::seconds(1));
			}
		}

		glClearColor(0.0f, 0.3f, 0.3f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		bool areTexturesReady = g_NodosState.ShaderInput.Image.Image && g_NodosState.ShaderOutput.Image.Image;
		bool areSemaphoresReady = g_NodosState.InputSemaphore && g_NodosState.OutputSemaphore && g_NodosState.RenderSubmittedEvent;
		if (g_NodosState.ExecutionStateMainThread == nos::app::ExecutionState::SYNCED && areTexturesReady && areSemaphoresReady)
		{
			bool isIdle = false;
			{
				std::unique_lock<std::mutex> lock(g_NodosState.ExecutionStateMutex);
				if (!g_NodosState.NodosFrameNumber || *g_NodosState.NodosFrameNumber < g_NodosState.CurFrameNumber)
				{
					//std::cout << "Waiting for Nodos to signal execution:" << g_NodosState.CurFrameNumber << std::endl;
					g_NodosState.ExecutionStateCV.wait(lock, [&]() { return g_NodosState.NodosFrameNumber && *g_NodosState.NodosFrameNumber >= g_NodosState.CurFrameNumber || g_NodosState.ExecutionState == nos::app::ExecutionState::IDLE; });
					isIdle = g_NodosState.ExecutionState == nos::app::ExecutionState::IDLE;
				}
			}
			if (!isIdle)
			{
				uint32_t imageIndex;
				//wait for input semaphore
				{
					GLenum srcLayout = GL_LAYOUT_TRANSFER_DST_EXT;
					//std::cout << "Waiting for input semaphore" << std::endl;
					glWaitSemaphoreEXT(g_NodosState.InputSemaphore->Semaphore, 0, nullptr, 1, &g_NodosState.ShaderInput.Image.Image, &srcLayout);
					glFlush();
					if (glGetError() != GL_NO_ERROR)
					{
						std::cerr << "Failed to wait for input semaphore" << std::endl;
						continue;
					}
				}
				//render to texture
				glBindFramebuffer(GL_FRAMEBUFFER, glData.FBO);
				glClipControl(GL_UPPER_LEFT, GL_NEGATIVE_ONE_TO_ONE);
				glViewport(0, 0, g_NodosState.ShaderOutput.Texture.width, g_NodosState.ShaderOutput.Texture.height);
				glUseProgram(glData.ShaderProgram);
				glBindVertexArray(glData.VAO);
				glBindBuffer(GL_ARRAY_BUFFER, glData.VBO);
				glBindTextureUnit(0, g_NodosState.ShaderInput.Image.Image);
				glUniform1i(glGetUniformLocation(glData.ShaderProgram, "inTexture"), 0);
				glDrawArrays(GL_TRIANGLES, 0, 3);
				glBindVertexArray(0);
				//signal output semaphore
				{
					GLenum dstLayouts = GL_LAYOUT_TRANSFER_SRC_EXT;
					glSignalSemaphoreEXT(g_NodosState.OutputSemaphore->Semaphore, 0, nullptr, 1, &g_NodosState.ShaderOutput.Image.Image, &dstLayouts);
					glFlush();

#if defined(_WIN32)
					SetEvent(g_NodosState.RenderSubmittedEvent);
#elif defined(__linux__)
					uint64_t dummy = 1;
					write(*g_NodosState.RenderSubmittedEvent, &dummy, sizeof(dummy));
#else
#error Unsupported platform
#endif
				}
				glClipControl(GL_LOWER_LEFT, GL_NEGATIVE_ONE_TO_ONE);
				//render to screen
				glBindFramebuffer(GL_READ_FRAMEBUFFER, glData.FBO);
				glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
				glBlitFramebuffer(0, 0, g_NodosState.ShaderOutput.Texture.width, g_NodosState.ShaderOutput.Texture.height, 0, 0, WIDTH, HEIGHT, GL_COLOR_BUFFER_BIT, GL_NEAREST);

				g_NodosState.CurFrameNumber++;
			}
		}
		glfwSwapBuffers(window);
	}

	ResetState();
	glfwDestroyWindow(window);
	glfwTerminate();

	return 0;
}